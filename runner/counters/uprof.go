// AMDuProfPcm 后端：Windows/Linux 上 AMD 处理器的计数器采集。
//
// 实测注意事项（uProf 5.3 / Ryzen 7800X3D / Win11）：
//  1. 目标程序必须用绝对路径启动，相对路径 launch 失败
//  2. 异常中断的会话会占用 PMC MSR，需 -r 重置后重试
//  3. PCM 的 stdout 不能接管道（会挂起），Go 中置 nil 接 null 设备
//  4. 默认只采 core 0，需 -a 采全核
package counters

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

const uprofDefaultPath = `C:\Program Files\AMD\AMDuProf\bin\AMDuProfPcm.exe`

// uprofMetrics 固定采集的指标集（覆盖 IPC、分支、各级缓存）。
const uprofMetrics = "ipc,l1,l2,l3,dc"

// findUProfPcm 按 环境变量 → PATH → 默认安装路径 查找 AMDuProfPcm。
func findUProfPcm() (string, bool) {
	if p := os.Getenv("CPUPERFOM_UPROF_PCM"); p != "" {
		if _, err := os.Stat(p); err == nil {
			return p, true
		}
	}
	if p, err := exec.LookPath("AMDuProfPcm"); err == nil {
		return p, true
	}
	if _, err := os.Stat(uprofDefaultPath); err == nil {
		return uprofDefaultPath, true
	}
	return "", false
}

type uprofProvider struct{ path string }

func (uprofProvider) Name() string { return "AMDuProfPcm" }
func (uprofProvider) Available() bool { return true }

func (p uprofProvider) Collect(command string, _ []string) (map[string]float64, error) {
	res, log, err := p.collectOnce(command)
	if err == nil {
		return res, nil
	}
	// MSR 被上次异常中断的会话占用：重置一次再重试
	if strings.Contains(log, "MSRs are in use") {
		reset := exec.Command(p.path, "-r")
		reset.Stdout, reset.Stderr = nil, nil
		if rerr := reset.Run(); rerr == nil {
			res, _, rerr2 := p.collectOnce(command)
			return res, rerr2
		}
	}
	return nil, err
}

func (p uprofProvider) collectOnce(command string) (map[string]float64, string, error) {
	exe, args, err := resolveCommand(command)
	if err != nil {
		return nil, "", err
	}
	csvFile := filepath.Join(os.TempDir(), "cpuperfom-pcm.csv")
	defer os.Remove(csvFile)
	// PCM 输出必须进真实文件而非管道（接管道会挂起）
	logFile, err := os.CreateTemp("", "cpuperfom-pcm-log-*.txt")
	if err != nil {
		return nil, "", err
	}
	defer os.Remove(logFile.Name())
	defer logFile.Close()

	pcmArgs := []string{
		"-m", uprofMetrics,
		"-C", // 累计模式（整个运行窗口聚合）
		"-A", "system", // 系统级聚合
		"-q", // 不打印拓扑
		"-a", // 采全核（进程可能被调度到任意核）
		"-d", "120", // 上限保护：程序退出即停止
		"-o", csvFile,
		"--", exe,
	}
	pcmArgs = append(pcmArgs, args...)

	cmd := exec.Command(p.path, pcmArgs...)
	cmd.Stdout, cmd.Stderr = logFile, logFile
	runErr := cmd.Run()

	logFile.Sync()
	logData, _ := os.ReadFile(logFile.Name())
	log := string(logData)

	if strings.Contains(log, "MSRs are in use") {
		return nil, log, fmt.Errorf("pcm msr busy")
	}
	if runErr != nil {
		return nil, log, fmt.Errorf("AMDuProfPcm: %w", runErr)
	}
	data, err := os.ReadFile(csvFile)
	if err != nil {
		return nil, log, fmt.Errorf("read pcm csv: %w (log: %s)", err, lastLines(log, 3))
	}
	return parseUProfCSV(string(data)), log, nil
}

func lastLines(s string, n int) string {
	lines := strings.Split(strings.TrimSpace(s), "\n")
	if len(lines) > n {
		lines = lines[len(lines)-n:]
	}
	return strings.Join(lines, " | ")
}

// resolveCommand 把命令行拆成 可执行文件绝对路径 + 参数。
// PCM 要求绝对路径启动目标程序。
func resolveCommand(command string) (string, []string, error) {
	fields := strings.Fields(command)
	if len(fields) == 0 {
		return "", nil, fmt.Errorf("empty command")
	}
	exe := fields[0]
	if !filepath.IsAbs(exe) {
		abs, err := filepath.Abs(exe)
		if err != nil {
			return "", nil, fmt.Errorf("resolve %s: %w", exe, err)
		}
		exe = abs
	}
	if _, err := os.Stat(exe); err != nil {
		return "", nil, fmt.Errorf("benchmark executable not found: %s", exe)
	}
	return exe, fields[1:], nil
}

// uprofMetricMap 把 PCM CSV 指标名映射为统一计数器名。
var uprofMetricMap = map[string]string{
	"Cycles Not in Halt":                 "cycles",
	"Retired Instructions":               "instructions",
	"IPC (Sys + User)":                   "ipc",
	"Retired Branches Mispredicted (pti)": "branch_miss_pti",
	"DC Access (pti)":                    "dc_access_pti",
	"L2 Miss (pti)":                      "l2_miss_pti",
	"L3 Miss %":                          "l3_miss_pct",
	"Ave L3 Miss Latency (ns)":           "l3_miss_latency_ns",
	"Eff Freq (MHz)":                     "eff_freq_mhz",
}

// parseUProfCSV 解析 AMDuProfPcm -C 累计报告的 "指标名,数值" 行。
func parseUProfCSV(csv string) map[string]float64 {
	res := map[string]float64{}
	for _, line := range strings.Split(csv, "\n") {
		name, valStr, ok := strings.Cut(line, ",")
		if !ok {
			continue
		}
		mapped, ok := uprofMetricMap[strings.TrimSpace(name)]
		if !ok {
			continue
		}
		v, err := strconv.ParseFloat(strings.TrimSpace(valStr), 64)
		if err != nil {
			continue
		}
		res[mapped] = v
	}
	return res
}
