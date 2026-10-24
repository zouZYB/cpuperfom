package ci

import (
	"context"
	"fmt"
	"log"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/cpuperfom/cpuperfom/internal/model"
	"github.com/cpuperfom/cpuperfom/internal/store"
	"github.com/cpuperfom/cpuperfom/runner"
	"github.com/cpuperfom/cpuperfom/runner/envctl"
	"github.com/cpuperfom/cpuperfom/runner/refcmp"
)

// watchedRepo 是运行时的受监控仓库（配置 + 数据库 ID）。
type watchedRepo struct {
	cfg     *RepoConfig
	watchID int64
}

// jobReq 是一次待执行的 before/after 对比任务。
type jobReq struct {
	repo               *watchedRepo
	beforeSHA, afterSHA string
}

// Service 是 CI 监控服务：每仓库一个轮询 goroutine + 全局单 worker 串行执行任务。
// 单 worker 是有意为之：基准计时对系统噪声敏感，任务不能并行互扰。
type Service struct {
	cfg   *Config
	q     *store.Queries
	sink  refcmp.Sink
	repos []*watchedRepo

	queue   chan struct{}      // 唤醒 worker 的信号（容量 1，配合 pending 去重）
	pending map[string]jobReq  // 仓库名 → 待执行任务（新 SHA 覆盖旧，latest-wins）
	enqueued map[string]string // 仓库名 → 已入队/执行中的 afterSHA（游标推进前防重复入队）
	mu      sync.Mutex
}

// NewService 登记受监控仓库（游标在配置变更后保留）。
func NewService(cfg *Config, q *store.Queries) (*Service, error) {
	s := &Service{
		cfg:     cfg,
		q:       q,
		sink:    newLocalSink(q),
		queue:    make(chan struct{}, 1),
		pending:  map[string]jobReq{},
		enqueued: map[string]string{},
	}
	for i := range cfg.Repos {
		rc := &cfg.Repos[i]
		id, err := q.UpsertWatchRepo(rc.Name, rc.Path, rc.Branch)
		if err != nil {
			return nil, err
		}
		s.repos = append(s.repos, &watchedRepo{cfg: rc, watchID: id})
	}
	return s, nil
}

// Start 阻塞运行直到 ctx 取消：清扫中断任务 → 启动 worker 与各仓库轮询 goroutine。
// ctx 取消后等待进行中的任务结束再返回。
func (s *Service) Start(ctx context.Context) {
	if err := s.q.FailStaleCIJobs(); err != nil {
		log.Printf("ci: 清扫中断任务失败: %v", err)
	}
	var wg sync.WaitGroup
	wg.Add(1)
	go func() { defer wg.Done(); s.worker(ctx) }()
	for _, wr := range s.repos {
		wg.Add(1)
		go func(wr *watchedRepo) { defer wg.Done(); s.pollLoop(ctx, wr) }(wr)
	}
	wg.Wait()
	log.Printf("ci: 监控服务已停止")
}

// pollLoop 每 interval 轮询一次（启动时立即首轮，用于建游标）。
func (s *Service) pollLoop(ctx context.Context, wr *watchedRepo) {
	log.Printf("ci: 开始监控 %s（%s 分支，每 %s 轮询）", wr.cfg.Name, wr.cfg.Branch, wr.cfg.IntervalD())
	s.pollOnce(wr)
	t := time.NewTicker(wr.cfg.IntervalD())
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			s.pollOnce(wr)
		}
	}
}

// pollOnce 检查一次 HEAD；发现新提交则入队对比任务。单次失败只记录，不影响后续轮询。
func (s *Service) pollOnce(wr *watchedRepo) {
	rc := wr.cfg
	if rc.Fetch {
		if out, err := exec.Command("git", "-C", rc.Path, "fetch", rc.Remote, rc.Branch).CombinedOutput(); err != nil {
			msg := fmt.Sprintf("git fetch: %v: %s", err, tail(string(out), 300))
			log.Printf("ci: [%s] %s", rc.Name, msg)
			s.q.RecordPoll(wr.watchID, "", msg)
			return
		}
	}
	head, err := resolveSHA(rc.Path, rc.HeadRef())
	if err != nil {
		msg := fmt.Sprintf("rev-parse %s: %v", rc.HeadRef(), err)
		log.Printf("ci: [%s] %s", rc.Name, msg)
		s.q.RecordPoll(wr.watchID, "", msg)
		return
	}
	s.q.RecordPoll(wr.watchID, head, "")

	wrepo, err := s.q.GetWatchRepo(wr.watchID)
	if err != nil {
		log.Printf("ci: [%s] 读取游标失败: %v", rc.Name, err)
		return
	}
	// 首次轮询只建游标，不把历史 HEAD 当新提交跑一轮
	if wrepo.LastCommit == "" {
		if err := s.q.AdvanceCursor(wr.watchID, head); err != nil {
			log.Printf("ci: [%s] 写入游标失败: %v", rc.Name, err)
			return
		}
		log.Printf("ci: [%s] 初始化游标 %s（不触发对比）", rc.Name, short(head))
		return
	}
	if head == wrepo.LastCommit {
		return
	}

	before := wrepo.LastCommit
	if !isAncestor(rc.Path, before, head) {
		log.Printf("ci: [%s] ⚠ %s 不是 %s 的祖先（force push / rebase？），仍照常对比",
			rc.Name, short(before), short(head))
	}
	// 游标要等任务结束才推进，期间同一 HEAD 可能反复被轮询看到——
	// 若该 HEAD 已在队列或执行中则跳过，避免同一对比被重复执行
	s.mu.Lock()
	dup := s.enqueued[rc.Name] == head
	s.mu.Unlock()
	if dup {
		return
	}

	if n := countCommits(rc.Path, before, head); n > 1 {
		log.Printf("ci: [%s] 发现 %d 个新提交，只对比最新（跳过 %d 个中间提交）", rc.Name, n, n-1)
	} else {
		log.Printf("ci: [%s] 发现新提交 %s → %s", rc.Name, short(before), short(head))
	}
	s.enqueue(jobReq{repo: wr, beforeSHA: before, afterSHA: head})
}

// enqueue 登记任务：同仓库排队期间的新提交会覆盖旧的（latest-wins），并唤醒 worker。
func (s *Service) enqueue(j jobReq) {
	s.mu.Lock()
	s.pending[j.repo.cfg.Name] = j
	s.enqueued[j.repo.cfg.Name] = j.afterSHA
	s.mu.Unlock()
	select {
	case s.queue <- struct{}{}:
	default: // worker 已知有活干，pending 里已是最新
	}
}

// worker 全局唯一执行器：逐个取出 pending 任务串行执行。
func (s *Service) worker(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case <-s.queue:
			for {
				s.mu.Lock()
				var j jobReq
				ok := false
				for name, v := range s.pending {
					j, ok = v, true
					delete(s.pending, name)
					break
				}
				s.mu.Unlock()
				if !ok {
					break
				}
				s.runJob(ctx, j)
			}
		}
	}
}

// runJob 执行一次 before/after 对比：建任务记录 → refcmp 全流程 → 入库 → 结论。
// 无论成败游标都推进——坏提交（如编译不过）不能卡死后续所有监控，
// 失败信息留在 ci_jobs 中，可用 bench-runner compare-ref 手动补跑。
func (s *Service) runJob(ctx context.Context, j jobReq) {
	rc := j.repo.cfg
	jobID, err := s.q.CreateCIJob(&model.CIJob{
		WatchRepoID: j.repo.watchID,
		BeforeSHA:   j.beforeSHA,
		AfterSHA:    j.afterSHA,
	})
	if err != nil {
		log.Printf("ci: [%s] 创建任务记录失败: %v", rc.Name, err)
		return
	}
	log.Printf("ci: [%s] 开始任务 #%d：%s → %s", rc.Name, jobID, short(j.beforeSHA), short(j.afterSHA))

	// 失败也推进游标；同时解除该 HEAD 的入队标记（defer 统一处理）。
	// 注意顺序：先推进游标再删标记，否则两操作间的轮询会重复入队。
	defer func() {
		if err := s.q.AdvanceCursor(j.repo.watchID, j.afterSHA); err != nil {
			log.Printf("ci: [%s] 推进游标失败: %v", rc.Name, err)
		}
		s.mu.Lock()
		delete(s.enqueued, rc.Name)
		s.mu.Unlock()
	}()

	res, err := refcmp.Run(refcmp.Options{
		Repo:             rc.Path,
		BeforeRef:        j.beforeSHA,
		AfterRef:         j.afterSHA,
		BuildCmd:         rc.Build,
		ConfigPath:       rc.Config,
		ConfigInWorktree: true, // bench.yaml 随被测仓库提交演进
		Rounds:           rc.Rounds,
		Run: runner.RunOptions{
			Warmup:     rc.Warmup,
			TimeoutSec: rc.Timeout,
		},
		BuildTimeoutSec: rc.BuildTimeout,
		Logf: func(format string, args ...any) {
			log.Printf("ci: [%s] %s", rc.Name, fmt.Sprintf(strings.TrimRight(format, "\n"), args...))
		},
	})
	if err != nil {
		errMsg := tail(err.Error(), 1000)
		log.Printf("ci: [%s] 任务 #%d 失败: %s", rc.Name, jobID, errMsg)
		s.q.FinishCIJob(jobID, "failed", "", errMsg, 0, 0)
		return
	}

	if ctx.Err() != nil {
		s.q.FinishCIJob(jobID, "failed", "", "interrupted by benchd shutdown", 0, 0)
		return
	}

	beforeID, afterID, err := refcmp.Upload(s.sink, res.Project, rc.Branch, res, envctl.Snapshot(""))
	if err != nil {
		errMsg := tail(err.Error(), 1000)
		log.Printf("ci: [%s] 任务 #%d 入库失败: %s", rc.Name, jobID, errMsg)
		s.q.FinishCIJob(jobID, "failed", "", errMsg, 0, 0)
		return
	}

	verdict := summarize(res.Compare)
	s.q.FinishCIJob(jobID, "success", verdict, "", beforeID, afterID)
	log.Printf("ci: [%s] 任务 #%d 完成：%s（before run_id=%d，after run_id=%d）",
		rc.Name, jobID, verdict, beforeID, afterID)
}

// summarize 把统计对比结果压缩成一行结论摘要。
func summarize(resp *runner.CompareResponse) string {
	parts := make([]string, 0, len(resp.Results))
	for _, r := range resp.Results {
		switch {
		case r.Significant && r.DeltaPct > 0:
			parts = append(parts, fmt.Sprintf("%s: 变慢 %.1f%%", r.Benchmark, r.DeltaPct))
		case r.Significant && r.DeltaPct < 0:
			parts = append(parts, fmt.Sprintf("%s: 变快 %.1f%%", r.Benchmark, -r.DeltaPct))
		case r.Note != "":
			parts = append(parts, fmt.Sprintf("%s: %s", r.Benchmark, r.Note))
		default:
			parts = append(parts, fmt.Sprintf("%s: 无显著差异", r.Benchmark))
		}
	}
	return strings.Join(parts, "；")
}

// ---------- git 辅助 ----------

// resolveSHA 解析 ref 到完整 SHA（与 gitver.ResolveRef 不同：失败时返回错误）。
func resolveSHA(repo, ref string) (string, error) {
	out, err := exec.Command("git", "-C", repo, "rev-parse", "--verify", ref).Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(out)), nil
}

// isAncestor 报告 before 是否为 after 的祖先（非祖先通常是 force push / rebase）。
func isAncestor(repo, before, after string) bool {
	return exec.Command("git", "-C", repo, "merge-base", "--is-ancestor", before, after).Run() == nil
}

// countCommits 统计 before..after 之间的提交数（失败按 1 计）。
func countCommits(repo, before, after string) int {
	out, err := exec.Command("git", "-C", repo, "rev-list", "--count", before+".."+after).Output()
	if err != nil {
		return 1
	}
	n, err := strconv.Atoi(strings.TrimSpace(string(out)))
	if err != nil || n < 1 {
		return 1
	}
	return n
}

func short(sha string) string {
	if len(sha) > 8 {
		return sha[:8]
	}
	return sha
}

func tail(s string, n int) string {
	if len(s) > n {
		return "…" + s[len(s)-n:]
	}
	return s
}
