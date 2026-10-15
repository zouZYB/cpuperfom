package runner

import (
	"fmt"
	"io"
	"math"
	"sort"
	"text/tabwriter"

	"github.com/cpuperfom/cpuperfom/internal/model"
	"github.com/cpuperfom/cpuperfom/runner/counters"
	"github.com/cpuperfom/cpuperfom/runner/envctl"
	"github.com/cpuperfom/cpuperfom/runner/exec"
	"github.com/cpuperfom/cpuperfom/runner/profile"
)

// BenchOutput 是一个基准项的执行结果（样本 + 计数器 + 可选 profile）。
type BenchOutput struct {
	Name     string             `json:"name"`
	Kind     string             `json:"kind"`
	Unit     string             `json:"unit"`
	Values   []float64          `json:"values"`
	Counters map[string]float64 `json:"counters,omitempty"`
	Profile  string             `json:"profile,omitempty"` // folded stack 文本
}

// CompareResponse 是 /api/v1/compare 的响应。
type CompareResponse struct {
	BeforeRun  int64                `json:"before_run"`
	AfterRun   int64                `json:"after_run"`
	EnvWarning string               `json:"env_warning"`
	Results    []model.CompareResult `json:"results"`
	Counters   map[string]struct {
		Before map[string]float64 `json:"before"`
		After  map[string]float64 `json:"after"`
	} `json:"counters"`
}

// RunOptions 控制一次完整执行。
type RunOptions struct {
	Repeat     int
	Warmup     int
	TimeoutSec int
	Compiler   string // 用于环境快照
	NoCounters bool
}

// ExecuteAll 按配置执行全部基准，返回结果、环境快照与警告。
func ExecuteAll(cfg *Config, opts RunOptions) ([]BenchOutput, model.Environment, []string, error) {
	warns := envctl.Check()
	if !counters.Available() && !opts.NoCounters {
		warns = append(warns, "当前平台无可用计数器后端（支持：Linux perf / AMD uProf），仅上报计时数据")
	}

	outputs := make([]BenchOutput, 0, len(cfg.Benchmarks))
	for _, b := range cfg.Benchmarks {
		res, err := exec.Run(b.Name, b.Kind, b.Command, exec.Options{
			Repeat:     max(opts.Repeat, b.Repeat),
			Warmup:     opts.Warmup,
			TimeoutSec: opts.TimeoutSec,
		})
		if err != nil {
			return nil, model.Environment{}, warns, err
		}
		out := BenchOutput{Name: res.Name, Kind: res.Kind, Unit: res.Unit, Values: res.Values}

		// prog 级基准附带计数器采集（func 级由微基准框架驱动，计数器在 M2 接入）
		if b.Kind == "prog" && counters.Available() && !opts.NoCounters {
			if cs, err := counters.Collect(b.Command, cfg.Counters); err == nil {
				out.Counters = cs
			} else {
				warns = append(warns, fmt.Sprintf("bench %s 计数器采集失败: %v", b.Name, err))
			}
		}
		// profile.enabled 且平台支持（Linux perf）时采集调用栈 profile
		if b.Kind == "prog" && cfg.Profile.Enabled && profile.Available() {
			if folded, err := profile.Collect(b.Command, "", cfg.Profile.Freq); err == nil {
				out.Profile = folded
			} else {
				warns = append(warns, fmt.Sprintf("bench %s profile 采集失败: %v", b.Name, err))
			}
		}
		outputs = append(outputs, out)
	}
	return outputs, envctl.Snapshot(opts.Compiler), warns, nil
}

// PrintCompareTable 以表格形式打印对比结果。
func PrintCompareTable(w io.Writer, resp *CompareResponse) {
	if resp.EnvWarning != "" {
		fmt.Fprintf(w, "⚠  %s\n\n", resp.EnvWarning)
	}
	tw := tabwriter.NewWriter(w, 0, 4, 2, ' ', 0)
	fmt.Fprintln(tw, "BENCHMARK\tBEFORE\tAFTER\tΔ%\tSPEEDUP\t95% CI\tp\t结论")
	for _, r := range resp.Results {
		verdict := "≈ 无显著差异"
		switch {
		case r.Note == "数据缺失":
			verdict = "? 数据缺失"
		case r.Significant && r.DeltaPct < 0:
			verdict = fmt.Sprintf("✓ 变快 %.1f%%", -r.DeltaPct)
		case r.Significant && r.DeltaPct > 0:
			verdict = fmt.Sprintf("✗ 变慢 %.1f%%", r.DeltaPct)
		case r.Note != "":
			verdict = "≈ " + r.Note
		}
		fmt.Fprintf(tw, "%s\t%s\t%s\t%+.2f%%\t%.3fx\t[%.3f, %.3f]\t%s\t%s\n",
			r.Benchmark,
			formatNs(r.MedianBefore), formatNs(r.MedianAfter),
			r.DeltaPct, r.Speedup, r.CILow, r.CIHigh,
			formatP(r.PValue), verdict)
	}
	tw.Flush()

	// 计数器对比表（有数据才显示）
	if len(resp.Counters) > 0 {
		fmt.Fprintln(w, "\n计数器对比：")
		ctw := tabwriter.NewWriter(w, 0, 4, 2, ' ', 0)
		fmt.Fprintln(ctw, "BENCHMARK\tMETRIC\tBEFORE\tAFTER\tΔ%")
		for bench, c := range resp.Counters {
			for _, metric := range counterMetricOrder(c.Before, c.After) {
				bv, av := c.Before[metric], c.After[metric]
				delta := ""
				if bv != 0 {
					delta = fmt.Sprintf("%+.1f%%", (av-bv)/bv*100)
				}
				fmt.Fprintf(ctw, "%s\t%s\t%s\t%s\t%s\n",
					bench, metric, formatCounter(bv), formatCounter(av), delta)
			}
		}
		ctw.Flush()
	}
}

// counterMetricOrder 按固定顺序输出关键指标，其余按字典序排后。
func counterMetricOrder(before, after map[string]float64) []string {
	priority := []string{"ipc", "cycles", "instructions", "l2_miss_pti", "l3_miss_pct",
		"l3_miss_latency_ns", "branch_miss_pti", "branch-misses", "cache-misses", "eff_freq_mhz"}
	seen := map[string]bool{}
	var out []string
	for _, m := range priority {
		if _, ok := before[m]; ok {
			out = append(out, m)
			seen[m] = true
		} else if _, ok := after[m]; ok {
			out = append(out, m)
			seen[m] = true
		}
	}
	var rest []string
	for m := range before {
		if !seen[m] {
			rest = append(rest, m)
		}
	}
	for m := range after {
		if !seen[m] {
			if _, ok := before[m]; !ok {
				rest = append(rest, m)
			}
			seen[m] = true
		}
	}
	sort.Strings(rest)
	return append(out, rest...)
}

func formatCounter(v float64) string {
	switch {
	case v >= 1e9:
		return fmt.Sprintf("%.2fG", v/1e9)
	case v >= 1e6:
		return fmt.Sprintf("%.2fM", v/1e6)
	default:
		return fmt.Sprintf("%.2f", v)
	}
}

func formatNs(ns float64) string {
	switch {
	case math.IsNaN(ns):
		return "-"
	case ns >= 1e9:
		return fmt.Sprintf("%.3fs", ns/1e9)
	case ns >= 1e6:
		return fmt.Sprintf("%.3fms", ns/1e6)
	case ns >= 1e3:
		return fmt.Sprintf("%.3fµs", ns/1e3)
	default:
		return fmt.Sprintf("%.1fns", ns)
	}
}

func formatP(p float64) string {
	if math.IsNaN(p) {
		return "-"
	}
	if p < 0.001 {
		return "<0.001"
	}
	return fmt.Sprintf("%.3f", p)
}
