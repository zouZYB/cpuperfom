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

func TestWatchRepoAndCIJob(t *testing.T) {
	q := openTest(t)

	id, err := q.UpsertWatchRepo("demo", "/tmp/demo", "main")
	if err != nil || id == 0 {
		t.Fatalf("upsert: id=%d err=%v", id, err)
	}
	// 同 (path, branch) upsert 幂等且更新名称、保留游标
	if err := q.AdvanceCursor(id, "abc123"); err != nil {
		t.Fatal(err)
	}
	id2, err := q.UpsertWatchRepo("demo-renamed", "/tmp/demo", "main")
	if err != nil || id2 != id {
		t.Fatalf("upsert idempotent: id2=%d err=%v", id2, err)
	}
	w, err := q.GetWatchRepo(id)
	if err != nil || w.Name != "demo-renamed" || w.LastCommit != "abc123" {
		t.Fatalf("get: %+v err=%v", w, err)
	}

	if err := q.RecordPoll(id, "def456", ""); err != nil {
		t.Fatal(err)
	}
	w, _ = q.GetWatchRepo(id)
	if w.LastSeen != "def456" || w.LastPollAt == "" {
		t.Fatalf("record poll: %+v", w)
	}

	repos, err := q.ListWatchRepos()
	if err != nil || len(repos) != 1 {
		t.Fatalf("list: %v err=%v", repos, err)
	}

	// CI 任务生命周期（before/after run_id 需指向真实 run，有外键约束）
	pid, _ := q.CreateProject("p", "")
	run1, _ := q.CreateRun(&model.Run{ProjectID: pid, Label: model.LabelBefore, CommitSHA: "abc123"})
	run2, _ := q.CreateRun(&model.Run{ProjectID: pid, Label: model.LabelAfter, CommitSHA: "def456"})
	jobID, err := q.CreateCIJob(&model.CIJob{WatchRepoID: id, BeforeSHA: "abc123", AfterSHA: "def456"})
	if err != nil {
		t.Fatal(err)
	}
	if err := q.FinishCIJob(jobID, "success", "matmul: 变慢 8.2%", "", run1, run2); err != nil {
		t.Fatal(err)
	}
	jobs, err := q.ListCIJobs("", 0)
	if err != nil || len(jobs) != 1 {
		t.Fatalf("list jobs: %v err=%v", jobs, err)
	}
	j := jobs[0]
	if j.Status != "success" || j.Verdict != "matmul: 变慢 8.2%" || j.BeforeRunID != run1 || j.AfterRunID != run2 || j.FinishedAt == "" {
		t.Fatalf("job: %+v", j)
	}
	if j.RepoName != "demo-renamed" {
		t.Fatalf("repo name join: %q", j.RepoName)
	}
	// 按仓库名过滤
	jobs, _ = q.ListCIJobs("no-such-repo", 0)
	if len(jobs) != 0 {
		t.Fatal("filter by repo name failed")
	}

	// running 任务被启动清扫标记为失败
	job2, _ := q.CreateCIJob(&model.CIJob{WatchRepoID: id, BeforeSHA: "a", AfterSHA: "b"})
	if err := q.FailStaleCIJobs(); err != nil {
		t.Fatal(err)
	}
	jobs, _ = q.ListCIJobs("", 10)
	if jobs[0].ID != job2 || jobs[0].Status != "failed" || jobs[0].Error == "" {
		t.Fatalf("stale sweep: %+v", jobs[0])
	}
	// 已完成的任务不受影响
	if jobs[1].ID != jobID || jobs[1].Status != "success" {
		t.Fatalf("finished job should not be swept: %+v", jobs[1])
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
