package runner

import (
	"os"
	"path/filepath"
	"testing"
)

func writeConfig(t *testing.T, content string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "bench.yaml")
	if err := os.WriteFile(p, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestLoadConfigDefaults(t *testing.T) {
	p := writeConfig(t, `
project: demo
benchmarks:
  - name: b1
    command: ./app
  - name: b2
    kind: func
    command: ./bench
    repeat: 5
`)
	cfg, err := LoadConfig(p)
	if err != nil {
		t.Fatal(err)
	}
	// kind 缺省应为 prog，prog 级 repeat 缺省应为 20
	if cfg.Benchmarks[0].Kind != "prog" {
		t.Fatalf("default kind = %q, want prog", cfg.Benchmarks[0].Kind)
	}
	if cfg.Benchmarks[0].Repeat != 20 {
		t.Fatalf("default repeat = %d, want 20", cfg.Benchmarks[0].Repeat)
	}
	// 显式指定的值保留
	if cfg.Benchmarks[1].Kind != "func" || cfg.Benchmarks[1].Repeat != 5 {
		t.Fatalf("explicit values not preserved: %+v", cfg.Benchmarks[1])
	}
}

func TestLoadConfigErrors(t *testing.T) {
	cases := map[string]string{
		"缺 project":  "benchmarks: [{name: b, command: ./x}]",
		"缺 command":  "project: p\nbenchmarks: [{name: b}]",
		"缺 name":     "project: p\nbenchmarks: [{command: ./x}]",
		"非法 yaml":   "project: [unclosed",
	}
	for name, content := range cases {
		if _, err := LoadConfig(writeConfig(t, content)); err == nil {
			t.Errorf("%s: 应报错", name)
		}
	}
}

func TestLoadConfigNotFound(t *testing.T) {
	if _, err := LoadConfig(filepath.Join(t.TempDir(), "missing.yaml")); err == nil {
		t.Fatal("missing file should error")
	}
}
