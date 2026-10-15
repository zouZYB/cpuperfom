package profile

import (
	"strings"
	"testing"
)

// 模仿 perf script 的真实输出结构（叶帧在前，空行分隔样本）。
const sampleScript = `app 1234 5678.901: cycles:
        7f1234 inner_func (/lib/libc.so)
        564abc matmul_naive (/app/app)
        564def main (/app/app)
        7f5678 __libc_start_main (/lib/libc.so)

app 1234 5679.001: cycles:
        7f1234 inner_func (/lib/libc.so)
        564abc matmul_naive (/app/app)
        564def main (/app/app)
        7f5678 __libc_start_main (/lib/libc.so)

app 1234 5679.101: cycles:
        564f00 init (/app/app)
        564def main (/app/app)
        7f5678 __libc_start_main (/lib/libc.so)
`

func TestScriptToFolded(t *testing.T) {
	folded := ScriptToFolded(sampleScript)
	lines := strings.Split(strings.TrimSpace(folded), "\n")
	if len(lines) != 2 {
		t.Fatalf("folded lines = %d, want 2: %q", len(lines), folded)
	}
	// 根在前、计数合并
	wantMain := "__libc_start_main;main;matmul_naive;inner_func 2"
	wantInit := "__libc_start_main;main;init 1"
	if !strings.Contains(folded, wantMain) {
		t.Fatalf("missing %q in:\n%s", wantMain, folded)
	}
	if !strings.Contains(folded, wantInit) {
		t.Fatalf("missing %q in:\n%s", wantInit, folded)
	}
}

func TestScriptToFoldedSkipsUnknown(t *testing.T) {
	script := "app 1 2.0: cycles:\n        7f1 [unknown] (/lib/x)\n        564 main (/app)\n"
	folded := ScriptToFolded(script)
	if strings.Contains(folded, "[unknown]") {
		t.Fatalf("[unknown] frame should be skipped: %q", folded)
	}
	if !strings.Contains(folded, "main 1") {
		t.Fatalf("folded: %q", folded)
	}
}
