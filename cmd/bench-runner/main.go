// bench-runner 是跨平台基准执行器：执行基准、采集环境/计数器、上报 benchd 或本地对比。
package main

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"os"
	"os/exec"
	"strings"
	"text/tabwriter"
	"time"

	"github.com/spf13/cobra"

	"github.com/cpuperfom/cpuperfom/internal/compare"
	"github.com/cpuperfom/cpuperfom/runner"
	"github.com/cpuperfom/cpuperfom/runner/abtest"
	"github.com/cpuperfom/cpuperfom/runner/envctl"
	"github.com/cpuperfom/cpuperfom/runner/gitver"
)

func main() {
	root := &cobra.Command{
		Use:   "bench-runner",
		Short: "cpuperfom 基准执行器",
	}
	root.AddCommand(runCmd(), compareCmd(), compareRefCmd(), runsCmd())
	if err := root.Execute(); err != nil {
		os.Exit(1)
	}
}

func runCmd() *cobra.Command {
	var (
		configPath, server, token, label, commit, ref, out, compiler string
		repeat, warmup, timeout                                      int
		noCounters, noUpload                                         bool
	)
	cmd := &cobra.Command{
		Use:   "run",
		Short: "执行 bench.yaml 中的全部基准并上报",
		RunE: func(cmd *cobra.Command, _ []string) error {
			cfg, err := runner.LoadConfig(configPath)
			if err != nil {
				return err
			}
			if commit == "" {
				commit, ref = gitInfo()
			}

			outputs, env, warns, err := runner.ExecuteAll(cfg, runner.RunOptions{
				Repeat: repeat, Warmup: warmup, TimeoutSec: timeout,
				Compiler: compiler, NoCounters: noCounters,
			})
			for _, w := range warns {
				fmt.Fprintf(os.Stderr, "⚠  %s\n", w)
			}
			if err != nil {
				return err
			}
			for _, o := range outputs {
				fmt.Printf("✓ %-30s %d 个样本（%s）\n", o.Name, len(o.Values), o.Kind)
			}

			// 本地保存结果（用于离线 compare）
			if out != "" {
				data, _ := json.MarshalIndent(outputs, "", "  ")
				if err := os.WriteFile(out, data, 0o644); err != nil {
					return fmt.Errorf("write %s: %w", out, err)
				}
				fmt.Printf("结果已保存到 %s\n", out)
			}

			if noUpload || server == "" {
				return nil
			}
			up := runner.NewUploader(server, token)
			runID, err := up.CreateRun(cfg.Project, commit, ref, label, env)
			if err != nil {
				return err
			}
			if err := up.UploadResults(runID, outputs); err != nil {
				return err
			}
			countersByBench := map[string]map[string]float64{}
			for _, o := range outputs {
				if len(o.Counters) > 0 {
					countersByBench[o.Name] = o.Counters
				}
			}
			if err := up.UploadCounters(runID, countersByBench); err != nil {
				return err
			}
			if err := up.UploadProfiles(runID, outputs); err != nil {
				return err
			}
			if err := up.Finish(runID); err != nil {
				return err
			}
			fmt.Printf("已上报 run_id=%d 到 %s\n", runID, server)
			return nil
		},
	}
	f := cmd.Flags()
	f.StringVarP(&configPath, "config", "c", "bench.yaml", "基准配置文件")
	f.StringVar(&server, "server", "", "benchd 地址，如 http://localhost:8080")
	f.StringVar(&token, "token", "", "上报认证 token")
	f.StringVar(&label, "label", "ci", "运行标签 before|after|baseline|ci")
	f.StringVar(&commit, "commit", "", "commit SHA（默认取当前 git HEAD）")
	f.StringVar(&ref, "ref", "", "分支名（默认取当前 git 分支）")
	f.StringVarP(&out, "out", "o", "", "本地保存结果 JSON 路径")
	f.IntVar(&repeat, "repeat", 0, "prog 级重复次数（覆盖 bench.yaml）")
	f.IntVar(&warmup, "warmup", 2, "预热轮数（丢弃）")
	f.IntVar(&timeout, "timeout", 60, "单次命令超时秒数")
	f.BoolVar(&noCounters, "no-counters", false, "禁用硬件计数器采集")
	f.BoolVar(&noUpload, "no-upload", false, "只执行不上报")
	f.StringVar(&compiler, "compiler", "", "编译器（用于环境快照，如 gcc/clang/cl）")
	return cmd
}

func compareCmd() *cobra.Command {
	var (
		server, token, beforeFile, afterFile string
		beforeID, afterID                    int64
	)
	cmd := &cobra.Command{
		Use:   "compare",
		Short: "对比两次运行（在线 run_id 或本地结果文件）",
		RunE: func(_ *cobra.Command, _ []string) error {
			// 在线模式：查询 benchd
			if server != "" {
				if beforeID == 0 || afterID == 0 {
					return fmt.Errorf("在线对比需要 --before 和 --after run_id")
				}
				up := runner.NewUploader(server, token)
				resp, err := up.Compare(beforeID, afterID)
				if err != nil {
					return err
				}
				runner.PrintCompareTable(os.Stdout, resp)
				return nil
			}
			// 本地模式：两份结果文件直接对比
			if beforeFile == "" || afterFile == "" {
				return fmt.Errorf("需要 --server 在线对比，或 --before-file/--after-file 本地对比")
			}
			resp, err := compareLocal(beforeFile, afterFile)
			if err != nil {
				return err
			}
			runner.PrintCompareTable(os.Stdout, resp)
			return nil
		},
	}
	f := cmd.Flags()
	f.StringVar(&server, "server", "", "benchd 地址（在线对比）")
	f.StringVar(&token, "token", "", "认证 token")
	f.Int64Var(&beforeID, "before", 0, "优化前 run_id")
	f.Int64Var(&afterID, "after", 0, "优化后 run_id")
	f.StringVar(&beforeFile, "before-file", "", "优化前本地结果 JSON")
	f.StringVar(&afterFile, "after-file", "", "优化后本地结果 JSON")
	return cmd
}

// compareRefCmd：git worktree 双版本 + ABAB 交替运行对比。
func compareRefCmd() *cobra.Command {
	var (
		repo, beforeRef, afterRef, buildCmd, configPath string
		server, token, outPrefix                        string
		rounds, repeat, warmup, timeout                 int
		noUpload                                        bool
	)
	cmd := &cobra.Command{
		Use:   "compare-ref",
		Short: "worktree 检出 before/after 两版本，ABAB 交替运行并对比",
		RunE: func(_ *cobra.Command, _ []string) error {
			cfg, err := runner.LoadConfig(configPath)
			if err != nil {
				return err
			}

			fmt.Printf("检出 %s（before）与 %s（after）…\n", beforeRef, afterRef)
			pair, err := gitver.Setup(repo, beforeRef, afterRef)
			if err != nil {
				return err
			}
			defer pair.Cleanup()

			if buildCmd != "" {
				fmt.Println("构建两个版本…")
				if err := pair.Build(buildCmd); err != nil {
					return err
				}
			}

			fmt.Printf("ABAB 交替运行 %d 轮…\n", rounds)
			before, after, err := abtest.Run(cfg, pair.BeforeDir, pair.AfterDir, rounds, runner.RunOptions{
				Repeat: repeat, Warmup: warmup, TimeoutSec: timeout,
			})
			if err != nil {
				return err
			}

			// 本地统计对比并打印
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
			fmt.Println()
			runner.PrintCompareTable(os.Stdout, resp)

			// 本地保存
			if outPrefix != "" {
				if data, err := json.MarshalIndent(before, "", "  "); err == nil {
					os.WriteFile(outPrefix+"-before.json", data, 0o644)
				}
				if data, err := json.MarshalIndent(after, "", "  "); err == nil {
					os.WriteFile(outPrefix+"-after.json", data, 0o644)
				}
				fmt.Printf("结果已保存到 %s-{before,after}.json\n", outPrefix)
			}

			if noUpload || server == "" {
				return nil
			}
			// 上报两次运行
			up := runner.NewUploader(server, token)
			env := envctl.Snapshot("")
			for _, side := range []struct {
				label   string
				commit  string
				outputs []runner.BenchOutput
			}{
				{"before", gitver.ResolveRef(repo, beforeRef), before},
				{"after", gitver.ResolveRef(repo, afterRef), after},
			} {
				runID, err := up.CreateRun(cfg.Project, side.commit, "", side.label, env)
				if err != nil {
					return err
				}
				if err := up.UploadResults(runID, side.outputs); err != nil {
					return err
				}
				if err := up.UploadProfiles(runID, side.outputs); err != nil {
					return err
				}
				if err := up.Finish(runID); err != nil {
					return err
				}
				fmt.Printf("已上报 %s run_id=%d\n", side.label, runID)
			}
			return nil
		},
	}
	f := cmd.Flags()
	f.StringVar(&repo, "repo", ".", "被测项目 git 仓库路径")
	f.StringVar(&beforeRef, "before-ref", "", "优化前 ref（如 main、HEAD~1）")
	f.StringVar(&afterRef, "after-ref", "", "优化后 ref（如 HEAD）")
	f.StringVar(&buildCmd, "build", "", "构建命令（在每个 worktree 内执行）")
	f.StringVarP(&configPath, "config", "c", "bench.yaml", "基准配置文件")
	f.IntVar(&rounds, "rounds", 3, "ABAB 交替轮数")
	f.IntVar(&repeat, "repeat", 0, "prog 级重复次数（覆盖配置）")
	f.IntVar(&warmup, "warmup", 1, "预热轮数")
	f.IntVar(&timeout, "timeout", 120, "单次命令超时秒数")
	f.StringVar(&server, "server", "", "benchd 地址")
	f.StringVar(&token, "token", "", "认证 token")
	f.BoolVar(&noUpload, "no-upload", false, "只对比不上报")
	f.StringVarP(&outPrefix, "out", "o", "", "本地保存结果前缀")
	cmd.MarkFlagRequired("before-ref")
	cmd.MarkFlagRequired("after-ref")
	return cmd
}

// runsCmd 列出服务器上的历史运行。
func runsCmd() *cobra.Command {
	var server, token, project string
	var limit int
	cmd := &cobra.Command{
		Use:   "runs",
		Short: "列出服务器上的历史运行",
		RunE: func(_ *cobra.Command, _ []string) error {
			if server == "" {
				return fmt.Errorf("需要 --server")
			}
			runs, err := runner.NewUploader(server, token).ListRuns(project, limit)
			if err != nil {
				return err
			}
			tw := tabwriter.NewWriter(os.Stdout, 0, 4, 2, ' ', 0)
			fmt.Fprintln(tw, "ID\tLABEL\tCOMMIT\tREF\tSTARTED\tFINISHED")
			for _, r := range runs {
				commit := r.CommitSHA
				if len(commit) > 8 {
					commit = commit[:8]
				}
				fmt.Fprintf(tw, "%d\t%s\t%s\t%s\t%s\t%s\n",
					r.ID, r.Label, commit, r.Ref, r.StartedAt, r.FinishedAt)
			}
			return tw.Flush()
		},
	}
	f := cmd.Flags()
	f.StringVar(&server, "server", "", "benchd 地址")
	f.StringVar(&token, "token", "", "认证 token")
	f.StringVar(&project, "project", "", "按项目名过滤")
	f.IntVar(&limit, "limit", 50, "最多返回条数")
	return cmd
}

// compareLocal 读取两份本地结果文件，按基准名匹配后做统计对比。
func compareLocal(beforePath, afterPath string) (*runner.CompareResponse, error) {
	load := func(p string) ([]runner.BenchOutput, error) {
		data, err := os.ReadFile(p)
		if err != nil {
			return nil, err
		}
		var out []runner.BenchOutput
		return out, json.Unmarshal(data, &out)
	}
	before, err := load(beforePath)
	if err != nil {
		return nil, fmt.Errorf("read before: %w", err)
	}
	after, err := load(afterPath)
	if err != nil {
		return nil, fmt.Errorf("read after: %w", err)
	}
	afterByName := map[string]runner.BenchOutput{}
	for _, a := range after {
		afterByName[a.Name] = a
	}
	rng := rand.New(rand.NewSource(time.Now().UnixNano()))
	resp := &runner.CompareResponse{}
	for _, b := range before {
		a, ok := afterByName[b.Name]
		if !ok {
			continue
		}
		resp.Results = append(resp.Results, compare.Samples(b.Name, b.Kind, b.Unit, b.Values, a.Values, rng))
	}
	return resp, nil
}

// gitInfo 尽力获取当前 git HEAD 与分支名。
func gitInfo() (commit, ref string) {
	if out, err := exec.Command("git", "rev-parse", "HEAD").Output(); err == nil {
		commit = strings.TrimSpace(string(out))
	}
	if out, err := exec.Command("git", "rev-parse", "--abbrev-ref", "HEAD").Output(); err == nil {
		ref = strings.TrimSpace(string(out))
	}
	return commit, ref
}
