// Package abtest 实现 ABAB 交替运行：before/after 两个版本逐轮交替执行，
// 消除温度漂移、后台任务等时间相关性干扰。
package abtest

import (
	"fmt"

	"github.com/cpuperfom/cpuperfom/runner"
	"github.com/cpuperfom/cpuperfom/runner/exec"
)

// Run 在 beforeDir/afterDir 两个 worktree 中按 ABAB 顺序执行 cfg 中的基准，
// 返回 (before 结果, after 结果)。
// rounds 为交替轮数：每轮对每个基准各执行一次 before 和 after。
func Run(cfg *runner.Config, beforeDir, afterDir string, rounds int, opts runner.RunOptions) (before, after []runner.BenchOutput, err error) {
	if rounds <= 0 {
		rounds = 3
	}
	beforeAcc := accumulate(cfg)
	afterAcc := accumulate(cfg)

	for round := 1; round <= rounds; round++ {
		// 每轮内 A、B 交替执行（ABABAB…）
		for _, side := range []struct {
			dir string
			acc map[string]*runner.BenchOutput
			tag string
		}{
			{beforeDir, beforeAcc, "before"},
			{afterDir, afterAcc, "after"},
		} {
			for _, b := range cfg.Benchmarks {
				res, err := exec.Run(b.Name, b.Kind, b.Command, exec.Options{
					Repeat:     max(opts.Repeat, b.Repeat),
					Warmup:     opts.Warmup,
					TimeoutSec: opts.TimeoutSec,
					WorkDir:    side.dir,
				})
				if err != nil {
					return nil, nil, fmt.Errorf("round %d %s bench %s: %w", round, side.tag, b.Name, err)
				}
				out := side.acc[b.Name]
				out.Values = append(out.Values, res.Values...)
			}
		}
		fmt.Printf("  round %d/%d 完成（累计样本 before=%d after=%d）\n",
			round, rounds, len(beforeAcc[cfg.Benchmarks[0].Name].Values), len(afterAcc[cfg.Benchmarks[0].Name].Values))
	}
	return valuesOf(cfg, beforeAcc), valuesOf(cfg, afterAcc), nil
}

func accumulate(cfg *runner.Config) map[string]*runner.BenchOutput {
	m := map[string]*runner.BenchOutput{}
	for _, b := range cfg.Benchmarks {
		m[b.Name] = &runner.BenchOutput{Name: b.Name, Kind: b.Kind, Unit: "ns"}
	}
	return m
}

func valuesOf(cfg *runner.Config, acc map[string]*runner.BenchOutput) []runner.BenchOutput {
	out := make([]runner.BenchOutput, 0, len(cfg.Benchmarks))
	for _, b := range cfg.Benchmarks {
		out = append(out, *acc[b.Name])
	}
	return out
}
