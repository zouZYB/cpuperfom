package counters

import "testing"

// 样本来自 AMDuProfPcm 5.3 在 Ryzen 7800X3D 上的真实输出（节选）。
const sampleCSV = `
AMDuProfPcm Report

Processor Name:,AMD Ryzen 7 7800X3D 8-Core Processor

CORE METRICS
Metric,System (Aggregated)
Retired Instructions,225564416.00
Cycles Not in Halt,306993592.00
IPC (Sys + User),0.66
Retired Branches Mispredicted (pti),6.93
DC Access (pti),590.34
L2 Miss (pti),39.36
Eff Freq (MHz),4194.16

L3 METRICS
Metric,System (Aggregated)
L3 Miss %,21.88
Ave L3 Miss Latency (ns),89.18
`

func TestParseUProfCSV(t *testing.T) {
	res := parseUProfCSV(sampleCSV)
	want := map[string]float64{
		"instructions":       225564416.00,
		"cycles":             306993592.00,
		"ipc":                0.66,
		"branch_miss_pti":    6.93,
		"dc_access_pti":      590.34,
		"l2_miss_pti":        39.36,
		"l3_miss_pct":        21.88,
		"l3_miss_latency_ns": 89.18,
		"eff_freq_mhz":       4194.16,
	}
	for k, v := range want {
		if got, ok := res[k]; !ok || got != v {
			t.Errorf("%s = %v (ok=%v), want %v", k, got, ok, v)
		}
	}
}

func TestResolveCommand(t *testing.T) {
	// 不存在的可执行文件应报错
	if _, _, err := resolveCommand("no-such-dir/no-such.exe arg1"); err == nil {
		t.Fatal("expected error for missing executable")
	}
}

func TestParsePerfCSV(t *testing.T) {
	out := "1000,,cycles,100.00,,,\n2000,,instructions,100.00,,,\n<not supported>,,cache-misses,,,,\n"
	res := parsePerfCSV(out)
	if res["cycles"] != 1000 || res["instructions"] != 2000 {
		t.Fatalf("parse failed: %v", res)
	}
	if res["ipc"] != 2.0 {
		t.Fatalf("ipc = %v, want 2.0", res["ipc"])
	}
	if _, ok := res["cache-misses"]; ok {
		t.Fatal("<not supported> should be skipped")
	}
}
