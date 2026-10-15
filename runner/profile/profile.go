// Package profile 采集采样 profile 并转为 folded stack 格式（差分火焰图源数据）。
// Linux 下通过 perf record + perf script 实现；其他平台不可用（Windows 可探索 AMDuProfCLI）。
package profile

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

// Available 报告当前平台是否支持 profile 采集。
func Available() bool {
	if runtime.GOOS != "linux" {
		return false
	}
	_, err := exec.LookPath("perf")
	return err == nil
}

// Collect 用 perf record 采样 command 的调用栈，返回 folded 格式文本。
// freq 为采样频率（Hz），默认 999（避开 1000 的锁步效应）。
func Collect(command, workDir string, freq int) (string, error) {
	if !Available() {
		return "", fmt.Errorf("profile unavailable on %s", runtime.GOOS)
	}
	if freq <= 0 {
		freq = 999
	}
	dataFile := filepath.Join(os.TempDir(), "cpuperfom-perf.data")
	defer os.Remove(dataFile)

	record := exec.Command("perf", "record", "-F", fmt.Sprint(freq), "-g",
		"-o", dataFile, "--", "sh", "-c", command)
	if workDir != "" {
		record.Dir = workDir
	}
	if out, err := record.CombinedOutput(); err != nil {
		return "", fmt.Errorf("perf record: %w\n%s", err, tail(string(out)))
	}

	script := exec.Command("perf", "script", "-i", dataFile)
	out, err := script.Output()
	if err != nil {
		return "", fmt.Errorf("perf script: %w", err)
	}
	return ScriptToFolded(string(out)), nil
}

// ScriptToFolded 把 perf script 输出转为 folded 格式（等价 stackcollapse-perf.pl 的核心逻辑）。
// perf script 每个样本块：首行 "comm pid/tid ts: event:"，随后每行 "addr symbol (dso)"，
// 从叶帧到根帧排列，空行分隔。folded 需要根在前。
func ScriptToFolded(script string) string {
	counts := map[string]int{}
	var order []string
	var frames []string

	flush := func() {
		if len(frames) == 0 {
			return
		}
		// 反转为 根→叶
		for i, j := 0, len(frames)-1; i < j; i, j = i+1, j-1 {
			frames[i], frames[j] = frames[j], frames[i]
		}
		stack := strings.Join(frames, ";")
		if _, seen := counts[stack]; !seen {
			order = append(order, stack)
		}
		counts[stack]++
		frames = frames[:0]
	}

	for _, line := range strings.Split(script, "\n") {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			flush()
			continue
		}
		if line[0] != ' ' && line[0] != '\t' {
			flush() // 样本块首行
			continue
		}
		// 帧行：地址 符号 (dso)
		fields := strings.Fields(trimmed)
		if len(fields) < 2 {
			continue
		}
		symbol := strings.Join(fields[1:], " ")
		if idx := strings.LastIndex(symbol, " ("); idx >= 0 {
			symbol = symbol[:idx] // 去掉 (dso)
		}
		if symbol == "" || symbol == "[unknown]" {
			continue
		}
		frames = append(frames, symbol)
	}
	flush()

	var sb strings.Builder
	for _, stack := range order {
		fmt.Fprintf(&sb, "%s %d\n", stack, counts[stack])
	}
	return sb.String()
}

func tail(s string) string {
	if len(s) > 300 {
		return "…" + s[len(s)-300:]
	}
	return s
}
