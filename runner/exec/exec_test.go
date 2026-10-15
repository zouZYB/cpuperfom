package exec

import (
	"testing"
)

// 样本模仿 Google Benchmark --benchmark_repetitions=3 的真实 JSON 结构。
const sampleGBJSON = `{
  "context": {"date": "2026-10-09", "num_cpus": 16},
  "benchmarks": [
    {"run_type": "iteration", "name": "BM_X", "repetition_index": 0, "real_time": 100.0, "cpu_time": 99.0, "time_unit": "ns"},
    {"run_type": "iteration", "name": "BM_X", "repetition_index": 1, "real_time": 200.0, "cpu_time": 199.0, "time_unit": "ns"},
    {"run_type": "iteration", "name": "BM_X", "repetition_index": 2, "real_time": 300.0, "cpu_time": 299.0, "time_unit": "ns"},
    {"run_type": "aggregate", "name": "BM_X_mean", "aggregate_name": "mean", "real_time": 200.0, "time_unit": "ns"},
    {"run_type": "aggregate", "name": "BM_X_median", "aggregate_name": "median", "real_time": 200.0, "time_unit": "ns"}
  ]
}`

func TestParseGBJSON(t *testing.T) {
	res, err := parseGBJSON("x", []byte(sampleGBJSON))
	if err != nil {
		t.Fatal(err)
	}
	// 只保留 iteration 条目，聚合条目必须跳过
	if len(res.Values) != 3 {
		t.Fatalf("values = %v, want 3 iteration samples", res.Values)
	}
	if res.Values[0] != 100 || res.Values[2] != 300 {
		t.Fatalf("values = %v", res.Values)
	}
	if res.Unit != "ns" {
		t.Fatalf("unit = %s", res.Unit)
	}
}

func TestParseGBJSONUnitConversion(t *testing.T) {
	data := `{"benchmarks": [
		{"run_type": "iteration", "real_time": 1.5, "time_unit": "us"},
		{"run_type": "iteration", "real_time": 2.0, "time_unit": "ms"}
	]}`
	res, err := parseGBJSON("x", []byte(data))
	if err != nil {
		t.Fatal(err)
	}
	if res.Values[0] != 1500 {
		t.Fatalf("us→ns: %v, want 1500", res.Values[0])
	}
	if res.Values[1] != 2e6 {
		t.Fatalf("ms→ns: %v, want 2e6", res.Values[1])
	}
}

func TestParseGBJSONNoIterations(t *testing.T) {
	data := `{"benchmarks": [{"run_type": "aggregate", "real_time": 1.0, "time_unit": "ns"}]}`
	if _, err := parseGBJSON("x", []byte(data)); err == nil {
		t.Fatal("aggregate-only output should error (missing --benchmark_repetitions)")
	}
}

func TestParseGBJSONInvalid(t *testing.T) {
	if _, err := parseGBJSON("x", []byte("not json")); err == nil {
		t.Fatal("invalid json should error")
	}
}
