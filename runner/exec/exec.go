// Package exec 执行基准命令并采集样本。
// func 级：解析 Google Benchmark JSON 输出；
// prog 级：重复执行整条命令并做墙钟计时。
package exec

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// Result 是一个基准项的采样结果。
type Result struct {
	Name   string    `json:"name"`
	Kind   string    `json:"kind"`
	Unit   string    `json:"unit"`
	Values []float64 `json:"values"` // 单位统一为 ns
}

// Options 控制执行行为。
type Options struct {
	Repeat     int    // prog 级重复次数（含预热）
	Warmup     int    // 预热轮数（丢弃）
	TimeoutSec int    // 单次命令超时
	WorkDir    string // 执行工作目录（worktree 对比时使用），空为当前目录
}

// shellCmd 按平台选择 shell，支持指定工作目录。
func shellCmd(ctx context.Context, command, workDir string) *exec.Cmd {
	var cmd *exec.Cmd
	if runtime.GOOS == "windows" {
		cmd = exec.CommandContext(ctx, "cmd", "/C", normalizeWindowsCommand(command, workDir))
	} else {
		cmd = exec.CommandContext(ctx, "sh", "-c", command)
	}
	if workDir != "" {
		cmd.Dir = workDir
	}
	return cmd
}

// normalizeWindowsCommand 修正 Windows 下可执行文件的查找方式：
//  1. 类 Unix 的相对路径（含 '/'）转反斜杠——cmd 会把首 token 的 '/' 误认为选项
//  2. 纯文件名（如 "app.exe"）若存在于工作目录，补 ".\" 前缀——
//     cmd 在某些环境下不搜索当前目录
// 相对路径基于 workDir 探测。
func normalizeWindowsCommand(command, workDir string) string {
	fields := strings.Fields(command)
	if len(fields) == 0 {
		return command
	}
	first := fields[0]
	probe := func(candidate string) bool {
		p := candidate
		if workDir != "" && !filepath.IsAbs(p) {
			p = filepath.Join(workDir, p)
		}
		_, err := os.Stat(p)
		return err == nil
	}
	switch {
	case strings.Contains(first, "/") && !strings.Contains(first, `\`):
		candidate := strings.ReplaceAll(first, "/", `\`)
		if probe(candidate) {
			fields[0] = candidate
			return strings.Join(fields, " ")
		}
	case !strings.ContainsAny(first, `/\`):
		if probe(first) {
			fields[0] = `.\` + first
			return strings.Join(fields, " ")
		}
	}
	return command
}

// Run 执行一个基准项并返回结果。
func Run(name, kind, command string, opts Options) (*Result, error) {
	switch kind {
	case "func":
		return runFunc(name, command, opts)
	default:
		return runProg(name, command, opts)
	}
}

// ---------- func 级：Google Benchmark JSON ----------

// gbOutput 是 Google Benchmark JSON 的子集。
type gbOutput struct {
	Benchmarks []struct {
		RunType   string  `json:"run_type"`
		Name      string  `json:"name"`
		RealTime  float64 `json:"real_time"`
		TimeUnit  string  `json:"time_unit"`
	} `json:"benchmarks"`
}

var unitToNs = map[string]float64{
	"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9,
}

// runFunc 执行一次基准程序，要求输出 Google Benchmark JSON
// （命令中应带 --benchmark_format=json，缺省时自动补上），
// 取所有 run_type=iteration 条目的 real_time 作为样本。
func runFunc(name, command string, opts Options) (*Result, error) {
	if !strings.Contains(command, "--benchmark_format") {
		command += " --benchmark_format=json"
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout(opts)*30) // 微基准本身耗时较长
	defer cancel()

	var stdout, stderr bytes.Buffer
	cmd := shellCmd(ctx, command, opts.WorkDir)
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("bench %q failed: %w\nstderr: %s", name, err, stderr.String())
	}

	return parseGBJSON(name, stdout.Bytes())
}

// parseGBJSON 从 Google Benchmark JSON 输出中提取样本：
// 取所有 run_type=iteration 条目的 real_time，统一换算为 ns。
func parseGBJSON(name string, data []byte) (*Result, error) {
	var out gbOutput
	if err := json.Unmarshal(data, &out); err != nil {
		return nil, fmt.Errorf("bench %q: parse google benchmark json: %w", name, err)
	}
	res := &Result{Name: name, Kind: "func", Unit: "ns"}
	for _, b := range out.Benchmarks {
		if b.RunType != "iteration" {
			continue // 跳过 mean/median 等聚合条目，保留原始迭代样本
		}
		factor, ok := unitToNs[b.TimeUnit]
		if !ok {
			factor = 1
		}
		res.Values = append(res.Values, b.RealTime*factor)
	}
	if len(res.Values) == 0 {
		return nil, fmt.Errorf("bench %q: no iteration samples found; 请确认命令带 --benchmark_repetitions=N（N>=10）", name)
	}
	return res, nil
}

// ---------- prog 级：整命令墙钟计时 ----------

func runProg(name, command string, opts Options) (*Result, error) {
	total := opts.Repeat + opts.Warmup
	res := &Result{Name: name, Kind: "prog", Unit: "ns"}
	for i := 0; i < total; i++ {
		ctx, cancel := context.WithTimeout(context.Background(), timeout(opts))
		start := time.Now()
		cmd := shellCmd(ctx, command, opts.WorkDir)
		var outBuf bytes.Buffer
		cmd.Stdout, cmd.Stderr = &outBuf, &outBuf
		err := cmd.Run()
		elapsed := time.Since(start)
		cancel()
		if err != nil {
			out := outBuf.String()
			if len(out) > 300 {
				out = "…" + out[len(out)-300:]
			}
			return nil, fmt.Errorf("prog %q run %d failed: %w\noutput: %s", name, i, err, out)
		}
		if i < opts.Warmup {
			continue // 预热轮丢弃（缓存冷启动、page fault 等）
		}
		res.Values = append(res.Values, float64(elapsed.Nanoseconds()))
	}
	return res, nil
}

func timeout(opts Options) time.Duration {
	if opts.TimeoutSec > 0 {
		return time.Duration(opts.TimeoutSec) * time.Second
	}
	return 60 * time.Second
}
