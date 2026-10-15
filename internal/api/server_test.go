package api

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/cpuperfom/cpuperfom/internal/model"
	"github.com/cpuperfom/cpuperfom/internal/store"
)

// newTestServer 创建内存数据库的测试服务器。
func newTestServer(t *testing.T) *httptest.Server {
	t.Helper()
	db, err := store.Open(":memory:")
	if err != nil {
		t.Fatalf("open db: %v", err)
	}
	t.Cleanup(func() { db.Close() })
	return httptest.NewServer(NewServer(store.New(db)).Handler())
}

func postJSON(t *testing.T, url string, body any, headers ...string) (int, map[string]any) {
	t.Helper()
	data, _ := json.Marshal(body)
	req, _ := http.NewRequest(http.MethodPost, url, bytes.NewReader(data))
	req.Header.Set("Content-Type", "application/json")
	if len(headers) > 0 {
		req.Header.Set("Authorization", headers[0])
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("post %s: %v", url, err)
	}
	defer resp.Body.Close()
	var out map[string]any
	json.NewDecoder(resp.Body).Decode(&out)
	return resp.StatusCode, out
}

// createRunWithSamples 登记一次运行并上传一组基准样本，返回 run_id。
func createRunWithSamples(t *testing.T, base, label string, values map[string][]float64) int64 {
	t.Helper()
	status, out := postJSON(t, base+"/api/v1/runs", map[string]any{
		"project": "demo", "label": label, "commit_sha": "sha-" + label,
		"env": model.Environment{CPUModel: "TestCPU", OS: "test/amd64"},
	})
	if status != http.StatusCreated {
		t.Fatalf("create run: status=%d %v", status, out)
	}
	runID := int64(out["run_id"].(float64))

	benches := make([]map[string]any, 0, len(values))
	for name, vals := range values {
		benches = append(benches, map[string]any{
			"name": name, "kind": "prog", "unit": "ns", "values": vals,
		})
	}
	status, out = postJSON(t, base+fmt.Sprintf("/api/v1/runs/%d/samples", runID),
		map[string]any{"benchmarks": benches})
	if status != http.StatusOK {
		t.Fatalf("upload samples: status=%d %v", status, out)
	}
	return runID
}

// TestFullCompareFlow 走通核心闭环：两次运行 → 对比 → 显著性结论。
func TestFullCompareFlow(t *testing.T) {
	srv := newTestServer(t)
	defer srv.Close()

	// before: ~100ns；after: ~50ns（2x 加速，20 个样本保证显著）
	before, after := make([]float64, 20), make([]float64, 20)
	for i := range before {
		before[i] = 100 + float64(i%5)
		after[i] = 50 + float64(i%5)
	}
	beforeID := createRunWithSamples(t, srv.URL, "before", map[string][]float64{"bench-x": before})
	afterID := createRunWithSamples(t, srv.URL, "after", map[string][]float64{"bench-x": after})

	resp, err := http.Get(fmt.Sprintf("%s/api/v1/compare?before_run=%d&after_run=%d", srv.URL, beforeID, afterID))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	var out struct {
		Results []model.CompareResult `json:"results"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		t.Fatal(err)
	}
	if len(out.Results) != 1 {
		t.Fatalf("results: %v", out.Results)
	}
	r := out.Results[0]
	if r.Speedup < 1.8 || r.Speedup > 2.2 {
		t.Fatalf("speedup = %v, want ~2.0", r.Speedup)
	}
	if !r.Significant {
		t.Fatalf("should be significant, note=%q", r.Note)
	}
	if r.MedianBefore != 102 || r.MedianAfter != 52 {
		t.Fatalf("medians = %v/%v, want 102/52", r.MedianBefore, r.MedianAfter)
	}
}

func TestRunNotFound(t *testing.T) {
	srv := newTestServer(t)
	defer srv.Close()

	resp, err := http.Get(srv.URL + "/api/v1/runs/999")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusNotFound {
		t.Fatalf("status = %d, want 404", resp.StatusCode)
	}
	status, _ := postJSON(t, srv.URL+"/api/v1/runs/999/samples", map[string]any{"benchmarks": []any{}})
	if status != http.StatusNotFound {
		t.Fatalf("samples to missing run: status = %d, want 404", status)
	}
}

func TestAuthToken(t *testing.T) {
	t.Setenv("BENCHD_TOKEN", "secret")
	srv := newTestServer(t)
	defer srv.Close()

	// 无 token 的写请求应 401
	status, _ := postJSON(t, srv.URL+"/api/v1/runs", map[string]any{"project": "x"})
	if status != http.StatusUnauthorized {
		t.Fatalf("no token: status = %d, want 401", status)
	}
	// 带正确 token 应通过
	status, _ = postJSON(t, srv.URL+"/api/v1/runs", map[string]any{"project": "x"}, "Bearer secret")
	if status != http.StatusCreated {
		t.Fatalf("with token: status = %d, want 201", status)
	}
	// GET 不需要 token
	resp, err := http.Get(srv.URL + "/api/v1/projects")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("GET without token: status = %d, want 200", resp.StatusCode)
	}
}

func TestCompareEnvWarning(t *testing.T) {
	srv := newTestServer(t)
	defer srv.Close()

	vals := []float64{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
	beforeID := createRunWithSamples(t, srv.URL, "before", map[string][]float64{"b": vals})

	// 手工构造环境不同的 after 运行
	status, out := postJSON(t, srv.URL+"/api/v1/runs", map[string]any{
		"project": "demo", "label": "after",
		"env": model.Environment{CPUModel: "DifferentCPU", OS: "test/amd64"},
	})
	if status != http.StatusCreated {
		t.Fatalf("create after run: %v", out)
	}
	afterID := int64(out["run_id"].(float64))
	postJSON(t, srv.URL+fmt.Sprintf("/api/v1/runs/%d/samples", afterID), map[string]any{
		"benchmarks": []map[string]any{{"name": "b", "kind": "prog", "unit": "ns", "values": vals}},
	})

	resp, err := http.Get(fmt.Sprintf("%s/api/v1/compare?before_run=%d&after_run=%d", srv.URL, beforeID, afterID))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	var cmp struct {
		EnvWarning string `json:"env_warning"`
	}
	json.NewDecoder(resp.Body).Decode(&cmp)
	if cmp.EnvWarning == "" {
		t.Fatal("不同环境的对比应有警告")
	}
}

// TestProfileUploadAndDiff 走通火焰图闭环：上传两份 folded profile → 差分树。
func TestProfileUploadAndDiff(t *testing.T) {
	srv := newTestServer(t)
	defer srv.Close()

	beforeID := createRunWithSamples(t, srv.URL, "before", map[string][]float64{"app": {1, 2, 3}})
	afterID := createRunWithSamples(t, srv.URL, "after", map[string][]float64{"app": {1, 2, 3}})

	beforeFolded := "main;matmul_naive 100\nmain;io_wait 50\n"
	afterFolded := "main;matmul_tiled 30\nmain;io_wait 50\n"

	status, out := postJSON(t, srv.URL+fmt.Sprintf("/api/v1/runs/%d/profiles", beforeID),
		map[string]any{"benchmark": "app", "data": beforeFolded})
	if status != http.StatusOK {
		t.Fatalf("upload before profile: status=%d %v", status, out)
	}
	status, out = postJSON(t, srv.URL+fmt.Sprintf("/api/v1/runs/%d/profiles", afterID),
		map[string]any{"benchmark": "app", "data": afterFolded})
	if status != http.StatusOK {
		t.Fatalf("upload after profile: status=%d %v", status, out)
	}

	// 坏格式应 400
	status, _ = postJSON(t, srv.URL+fmt.Sprintf("/api/v1/runs/%d/profiles", beforeID),
		map[string]any{"benchmark": "app", "data": "no-count-line"})
	if status != http.StatusBadRequest {
		t.Fatalf("bad folded should 400, got %d", status)
	}

	resp, err := http.Get(fmt.Sprintf("%s/api/v1/flamegraph/diff?before_run=%d&after_run=%d&benchmark=app",
		srv.URL, beforeID, afterID))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("diff status = %d", resp.StatusCode)
	}
	var tree struct {
		Before   int64 `json:"before"`
		After    int64 `json:"after"`
		Children []struct {
			Name  string `json:"name"`
			After int64  `json:"after"`
		} `json:"children"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&tree); err != nil {
		t.Fatal(err)
	}
	if tree.Before != 150 || tree.After != 80 {
		t.Fatalf("root totals = %d/%d, want 150/80", tree.Before, tree.After)
	}
	if len(tree.Children) != 1 || tree.Children[0].Name != "main" {
		t.Fatalf("children: %+v", tree.Children)
	}

	// 缺 profile 的基准应 404
	resp2, err := http.Get(fmt.Sprintf("%s/api/v1/flamegraph/diff?before_run=%d&after_run=%d&benchmark=missing",
		srv.URL, beforeID, afterID))
	if err != nil {
		t.Fatal(err)
	}
	defer resp2.Body.Close()
	if resp2.StatusCode != http.StatusNotFound {
		t.Fatalf("missing benchmark diff: status = %d, want 404", resp2.StatusCode)
	}
}
