// Package counters 采集硬件计数器，按平台自动选择后端：
//   Linux        → perf stat
//   Windows(AMD) → AMDuProfPcm（uProf 5.x）
//   其他         → unavailable（不阻断计时数据）
package counters

import (
	"fmt"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
)

// Provider 是计数器采集后端。
type Provider interface {
	Name() string
	Available() bool
	// Collect 包裹执行 command，返回统一的计数器名→数值。
	// 统一名称：cycles / instructions / ipc / branch_miss_pti /
	//           l2_miss_pti / l3_miss_pct / l3_miss_latency_ns / cache_misses
	Collect(command string, events []string) (map[string]float64, error)
}

var provider Provider = detectProvider()

func detectProvider() Provider {
	if runtime.GOOS == "linux" {
		if _, err := exec.LookPath("perf"); err == nil {
			return perfProvider{}
		}
	}
	if p, ok := findUProfPcm(); ok {
		return uprofProvider{path: p}
	}
	return noopProvider{}
}

// Available 报告当前平台是否支持计数器采集。
func Available() bool { return provider.Available() }

// ProviderName 返回当前后端名（perf / AMDuProfPcm / none）。
func ProviderName() string { return provider.Name() }

// Collect 用当前后端采集计数器。
func Collect(command string, events []string) (map[string]float64, error) {
	return provider.Collect(command, events)
}

// ---------- noop ----------

type noopProvider struct{}

func (noopProvider) Name() string { return "none" }
func (noopProvider) Available() bool { return false }
func (noopProvider) Collect(string, []string) (map[string]float64, error) {
	return nil, fmt.Errorf("counters unavailable on %s", runtime.GOOS)
}

// ---------- perf（Linux） ----------

type perfProvider struct{}

func (perfProvider) Name() string { return "perf" }
func (perfProvider) Available() bool { return true }

func (perfProvider) Collect(command string, events []string) (map[string]float64, error) {
	if len(events) == 0 {
		events = []string{"cycles", "instructions", "cache-misses", "branch-misses"}
	}
	args := []string{"stat", "-x", ",", "-e", strings.Join(events, ","), "--", "sh", "-c", command}
	cmd := exec.Command("perf", args...)
	// perf stat 的计数器输出在 stderr
	var stderr strings.Builder
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("perf stat: %w", err)
	}
	return parsePerfCSV(stderr.String()), nil
}

// parsePerfCSV 解析 perf stat -x, 输出：
// 形如 "12345,,cycles,100.00,,," 或 "<not supported>,,cache-misses,..."
func parsePerfCSV(out string) map[string]float64 {
	res := map[string]float64{}
	for _, line := range strings.Split(out, "\n") {
		fields := strings.Split(line, ",")
		if len(fields) < 3 || strings.HasPrefix(line, "#") {
			continue
		}
		valStr := strings.TrimSpace(fields[0])
		if strings.HasPrefix(valStr, "<") {
			continue // <not supported> / <not counted>
		}
		v, err := strconv.ParseFloat(valStr, 64)
		if err != nil {
			continue
		}
		res[strings.TrimSpace(fields[2])] = v
	}
	if cyc, ok1 := res["cycles"]; ok1 {
		if ins, ok2 := res["instructions"]; ok2 && cyc > 0 {
			res["ipc"] = ins / cyc // 派生指标
		}
	}
	return res
}
