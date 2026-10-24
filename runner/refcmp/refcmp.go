// Package refcmp 实现双版本 git worktree + ABAB 交替运行对比的完整流程。
// 自 bench-runner 的 compare-ref 命令抽出，供 CLI 与 benchd CI 监控复用。
package refcmp

import (
	"context"
	"math/rand"
	"path/filepath"
	"time"

	"github.com/cpuperfom/cpuperfom/internal/compare"
	"github.com/cpuperfom/cpuperfom/internal/model"
	"github.com/cpuperfom/cpuperfom/runner"
	"github.com/cpuperfom/cpuperfom/runner/abtest"
	"github.com/cpuperfom/cpuperfom/runner/gitver"
)

// Options 是一次 before/after 对比的全部输入。
type Options struct {
	Repo      string // 被测项目 git 仓库路径
	BeforeRef string // 优化前 ref（commit SHA、HEAD~1 等）
	AfterRef  string // 优化后 ref
	BuildCmd  string // 构建命令（在每个 worktree 内执行），为空跳过
	// ConfigPath 是 bench.yaml 路径。ConfigInWorktree 为 false 时按原样解析；
	// 为 true 时相对 after worktree 根目录解析（配置随被测仓库提交演进）。
	ConfigPath       string
	ConfigInWorktree bool
	Rounds           int // ABAB 交替轮数
	Run              runner.RunOptions
	BuildTimeoutSec  int                        // 构建超时秒数，0 表示不限制
	Logf             func(string, ...any)       // 进度回调，nil 则不输出
}

// Result 是一次对比的全部产出。
type Result struct {
	Project   string // 来自 bench.yaml 的 project 字段
	BeforeSHA string
	AfterSHA  string
	Before    []runner.BenchOutput
	After     []runner.BenchOutput
	Compare   *runner.CompareResponse
}

func (o *Options) logf(format string, args ...any) {
	if o.Logf != nil {
		o.Logf(format, args...)
	}
}

// Run 执行：检出双 worktree → 可选构建 → ABAB 交替运行 → 统计对比。
// worktree 在函数返回前自动清理。
func Run(opts Options) (*Result, error) {
	rounds := opts.Rounds
	if rounds <= 0 {
		rounds = 3
	}

	// 配置在 worktree 外时先加载（快速失败）；在 worktree 内时检出后再读
	var cfg *runner.Config
	if !opts.ConfigInWorktree {
		var err error
		cfg, err = runner.LoadConfig(opts.ConfigPath)
		if err != nil {
			return nil, err
		}
	}

	opts.logf("检出 %s（before）与 %s（after）…\n", opts.BeforeRef, opts.AfterRef)
	pair, err := gitver.Setup(opts.Repo, opts.BeforeRef, opts.AfterRef)
	if err != nil {
		return nil, err
	}
	defer pair.Cleanup()

	if cfg == nil {
		cfg, err = runner.LoadConfig(filepath.Join(pair.AfterDir, opts.ConfigPath))
		if err != nil {
			return nil, err
		}
	}

	if opts.BuildCmd != "" {
		opts.logf("构建两个版本…\n")
		ctx := context.Background()
		if opts.BuildTimeoutSec > 0 {
			var cancel context.CancelFunc
			ctx, cancel = context.WithTimeout(ctx, time.Duration(opts.BuildTimeoutSec)*time.Second)
			defer cancel()
		}
		if err := pair.BuildContext(ctx, opts.BuildCmd); err != nil {
			return nil, err
		}
	}

	opts.logf("ABAB 交替运行 %d 轮…\n", rounds)
	before, after, err := abtest.Run(cfg, pair.BeforeDir, pair.AfterDir, rounds, opts.Run)
	if err != nil {
		return nil, err
	}

	// 本地统计对比
	rng := rand.New(rand.NewSource(time.Now().UnixNano()))
	resp := &runner.CompareResponse{}
	afterByName := map[string]runner.BenchOutput{}
	for _, a := range after {
		afterByName[a.Name] = a
	}
	for _, b := range before {
		if a, ok := afterByName[b.Name]; ok {
			resp.Results = append(resp.Results, compare.Samples(b.Name, b.Kind, b.Unit, b.Values, a.Values, rng))
		}
	}

	return &Result{
		Project:   cfg.Project,
		BeforeSHA: gitver.ResolveRef(opts.Repo, opts.BeforeRef),
		AfterSHA:  gitver.ResolveRef(opts.Repo, opts.AfterRef),
		Before:    before,
		After:     after,
		Compare:   resp,
	}, nil
}

// Sink 抽象结果上报目标：HTTP Uploader（bench-runner）或本地 store 直连（benchd）。
type Sink interface {
	CreateRun(project, commitSHA, ref, label string, env model.Environment) (int64, error)
	UploadResults(runID int64, results []runner.BenchOutput) error
	UploadCounters(runID int64, counters map[string]map[string]float64) error
	UploadProfiles(runID int64, results []runner.BenchOutput) error
	Finish(runID int64) error
}

// Upload 把 before/after 两侧各作为一个 run 上报，返回两个 run_id。
func Upload(s Sink, project, ref string, res *Result, env model.Environment) (beforeID, afterID int64, err error) {
	sides := []struct {
		label   string
		commit  string
		outputs []runner.BenchOutput
	}{
		{model.LabelBefore, res.BeforeSHA, res.Before},
		{model.LabelAfter, res.AfterSHA, res.After},
	}
	ids := make([]int64, 2)
	for i, side := range sides {
		runID, err := s.CreateRun(project, side.commit, ref, side.label, env)
		if err != nil {
			return 0, 0, err
		}
		if err := s.UploadResults(runID, side.outputs); err != nil {
			return 0, 0, err
		}
		countersByBench := map[string]map[string]float64{}
		for _, o := range side.outputs {
			if len(o.Counters) > 0 {
				countersByBench[o.Name] = o.Counters
			}
		}
		if err := s.UploadCounters(runID, countersByBench); err != nil {
			return 0, 0, err
		}
		if err := s.UploadProfiles(runID, side.outputs); err != nil {
			return 0, 0, err
		}
		if err := s.Finish(runID); err != nil {
			return 0, 0, err
		}
		ids[i] = runID
	}
	return ids[0], ids[1], nil
}

// 确保 runner.Uploader 始终满足 Sink（编译期检查）。
var _ Sink = (*runner.Uploader)(nil)
