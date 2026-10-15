package store

import (
	"testing"

	"github.com/cpuperfom/cpuperfom/internal/model"
)

// openTest 打开内存数据库（每个测试独立）。
func openTest(t *testing.T) *Queries {
	t.Helper()
	db, err := Open(":memory:")
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	t.Cleanup(func() { db.Close() })
	return New(db)
}

func TestProjectCRUD(t *testing.T) {
	q := openTest(t)

	id, err := q.CreateProject("proj-a", "https://example.com/a.git")
	if err != nil || id == 0 {
		t.Fatalf("create: id=%d err=%v", id, err)
	}
	// 重名应失败（UNIQUE 约束）
	if _, err := q.CreateProject("proj-a", ""); err == nil {
		t.Fatal("duplicate name should fail")
	}
	p, err := q.GetProjectByName("proj-a")
	if err != nil || p.RepoURL != "https://example.com/a.git" {
		t.Fatalf("get: %+v err=%v", p, err)
	}
	list, err := q.ListProjects()
	if err != nil || len(list) != 1 {
		t.Fatalf("list: %v err=%v", list, err)
	}
}

func TestEnvironmentDedup(t *testing.T) {
	q := openTest(t)

	env := model.Environment{CPUModel: "AMD 7800X3D", OS: "windows/amd64", Cores: 16}
	id1, err := q.UpsertEnvironment(&env)
	if err != nil {
		t.Fatal(err)
	}
	if env.Hash == "" {
		t.Fatal("hash should be computed")
	}
	// 相同环境应复用同一条记录
	id2, _ := q.UpsertEnvironment(&model.Environment{CPUModel: "AMD 7800X3D", OS: "windows/amd64", Cores: 16})
	if id1 != id2 {
		t.Fatalf("dedup failed: id1=%d id2=%d", id1, id2)
	}
	// 不同环境应新建
	id3, _ := q.UpsertEnvironment(&model.Environment{CPUModel: "Intel", OS: "linux/amd64"})
	if id3 == id1 {
		t.Fatal("different env should get new id")
	}
}

func TestRunLifecycleAndSamples(t *testing.T) {
	q := openTest(t)
	pid, _ := q.CreateProject("p", "")
	envID, _ := q.UpsertEnvironment(&model.Environment{CPUModel: "x"})

	runID, err := q.CreateRun(&model.Run{ProjectID: pid, Label: model.LabelBefore, EnvID: envID, CommitSHA: "abc"})
	if err != nil {
		t.Fatal(err)
	}
	run, err := q.GetRun(runID)
	if err != nil || run.CommitSHA != "abc" || run.Label != "before" {
		t.Fatalf("get run: %+v err=%v", run, err)
	}
	if run.FinishedAt != "" {
		t.Fatal("should not be finished yet")
	}

	benchID, err := q.UpsertBenchmark(pid, "matmul", model.KindFunc, "ns")
	if err != nil {
		t.Fatal(err)
	}
	// 同名 upsert 幂等
	benchID2, _ := q.UpsertBenchmark(pid, "matmul", model.KindFunc, "ns")
	if benchID != benchID2 {
		t.Fatal("benchmark upsert not idempotent")
	}

	// 插入乱序样本，读取应按 iteration 排序
	samples := []model.Sample{
		{RunID: runID, BenchmarkID: benchID, Iteration: 2, ValueNs: 30},
		{RunID: runID, BenchmarkID: benchID, Iteration: 0, ValueNs: 10},
		{RunID: runID, BenchmarkID: benchID, Iteration: 1, ValueNs: 20},
	}
	if err := q.InsertSamples(samples); err != nil {
		t.Fatal(err)
	}
	vals, err := q.SamplesFor(runID, benchID)
	if err != nil || len(vals) != 3 || vals[0] != 10 || vals[1] != 20 || vals[2] != 30 {
		t.Fatalf("samples: %v err=%v", vals, err)
	}

	benches, err := q.BenchmarksOfRun(runID)
	if err != nil || len(benches) != 1 || benches[0].Name != "matmul" {
		t.Fatalf("benchmarks of run: %v err=%v", benches, err)
	}

	if err := q.FinishRun(runID); err != nil {
		t.Fatal(err)
	}
	run, _ = q.GetRun(runID)
	if run.FinishedAt == "" {
		t.Fatal("finished_at should be set")
	}
}

func TestCountersReplace(t *testing.T) {
	q := openTest(t)
	pid, _ := q.CreateProject("p", "")
	runID, _ := q.CreateRun(&model.Run{ProjectID: pid, Label: model.LabelCI})
	benchID, _ := q.UpsertBenchmark(pid, "app", model.KindProg, "ns")

	cs := []model.Counter{
		{RunID: runID, BenchmarkID: benchID, Name: "ipc", Value: 0.7},
		{RunID: runID, BenchmarkID: benchID, Name: "cycles", Value: 1e9},
	}
	if err := q.InsertCounters(cs); err != nil {
		t.Fatal(err)
	}
	// 重复写入同名计数器应覆盖
	if err := q.InsertCounters([]model.Counter{{RunID: runID, BenchmarkID: benchID, Name: "ipc", Value: 1.8}}); err != nil {
		t.Fatal(err)
	}
	m, err := q.CountersFor(runID, benchID)
	if err != nil {
		t.Fatal(err)
	}
	if m["ipc"] != 1.8 || m["cycles"] != 1e9 || len(m) != 2 {
		t.Fatalf("counters: %v", m)
	}
}
