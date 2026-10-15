// Package envctl 采集运行环境快照并执行环境检查（绑核、频率 governor 等）。
package envctl

import (
	"fmt"
	"os"
	"os/exec"
	"runtime"
	"strings"

	"github.com/cpuperfom/cpuperfom/internal/model"
)

// Snapshot 采集当前机器环境快照。各字段尽力而为，失败留空。
func Snapshot(compiler string) model.Environment {
	env := model.Environment{
		CPUModel:  cpuModel(),
		Cores:     runtime.NumCPU(),
		OS:        runtime.GOOS + "/" + runtime.GOARCH,
		Kernel:    kernel(),
		Compiler:  compilerVersion(compiler),
		FreqGovernor: governor(),
	}
	env.Hash = env.ComputeHash()
	return env
}

// Check 执行环境检查，返回警告列表（不阻断运行）。
func Check() []string {
	var warns []string
	if runtime.GOOS == "linux" {
		g := governor()
		if g != "" && g != "performance" {
			warns = append(warns, fmt.Sprintf("CPU 频率 governor 为 %q（建议 performance），频率波动会污染基准结果", g))
		}
		if v := readFileTrim("/sys/devices/system/cpu/intel_pstate/no_turbo"); v == "0" {
			warns = append(warns, "Intel Turbo Boost 处于开启状态，短时基准可能受频率爬升影响")
		}
	}
	return warns
}

func cpuModel() string {
	if runtime.GOOS == "linux" {
		for _, line := range strings.Split(readFileTrim("/proc/cpuinfo"), "\n") {
			if strings.HasPrefix(line, "model name") {
				if i := strings.Index(line, ":"); i >= 0 {
					return strings.TrimSpace(line[i+1:])
				}
			}
		}
		return ""
	}
	if runtime.GOOS == "windows" {
		out, _ := exec.Command("powershell", "-NoProfile", "-Command",
			"(Get-CimInstance Win32_Processor).Name").Output()
		return strings.TrimSpace(string(out))
	}
	return ""
}

func kernel() string {
	if runtime.GOOS == "windows" {
		out, _ := exec.Command("cmd", "/C", "ver").Output()
		return strings.TrimSpace(string(out))
	}
	out, _ := exec.Command("uname", "-r").Output()
	return strings.TrimSpace(string(out))
}

func governor() string {
	return readFileTrim("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
}

func compilerVersion(compiler string) string {
	if compiler == "" {
		return ""
	}
	out, err := exec.Command(compiler, "--version").Output()
	if err != nil {
		return ""
	}
	line, _, _ := strings.Cut(string(out), "\n")
	return strings.TrimSpace(line)
}

func readFileTrim(path string) string {
	data, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(data))
}
