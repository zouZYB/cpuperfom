// Package model 定义 cpuperfom 平台的核心数据模型。
package model

// Run 标签：一次基准运行的语义角色。
const (
	LabelBefore   = "before"
	LabelAfter    = "after"
	LabelBaseline = "baseline"
	LabelCI       = "ci"
)

// 基准粒度。
const (
	KindFunc = "func" // 函数级微基准（Google Benchmark）
	KindProg = "prog" // 程序级基准（整命令计时）
)

// 时间字段统一为 RFC3339 文本（SQLite 无原生时间类型）。
type Project struct {
	ID        int64  `json:"id"`
	Name      string `json:"name"`
	RepoURL   string `json:"repo_url"`
	CreatedAt string `json:"created_at"`
}

// Environment 是一次运行的环境快照，按 Hash 去重存储。
type Environment struct {
	ID           int64  `json:"id"`
	Hash         string `json:"hash"`
	CPUModel     string `json:"cpu_model"`
	Microarch    string `json:"microarch"`
	Cores        int    `json:"cores"`
	FreqGovernor string `json:"freq_governor"`
	OS           string `json:"os"`
	Kernel       string `json:"kernel"`
	Compiler     string `json:"compiler"`
	Flags        string `json:"flags"`
}

// Hash 由除 ID/Hash 外所有字段决定，用于环境去重。
func (e *Environment) ComputeHash() string {
	h := sha256Of(e.CPUModel, e.Microarch, e.FreqGovernor, e.OS, e.Kernel, e.Compiler, e.Flags)
	return h
}

type Run struct {
	ID         int64  `json:"id"`
	ProjectID  int64  `json:"project_id"`
	CommitSHA  string `json:"commit_sha"`
	Ref        string `json:"ref"`
	Label      string `json:"label"`
	CIProvider string `json:"ci_provider"`
	CIRunURL   string `json:"ci_run_url"`
	EnvID      int64  `json:"env_id"`
	StartedAt  string `json:"started_at"`
	FinishedAt string `json:"finished_at,omitempty"`
}

type Benchmark struct {
	ID        int64  `json:"id"`
	ProjectID int64  `json:"project_id"`
	Name      string `json:"name"`
	Kind      string `json:"kind"`
	Unit      string `json:"unit"`
}

type Sample struct {
	RunID       int64   `json:"run_id"`
	BenchmarkID int64   `json:"benchmark_id"`
	Iteration   int     `json:"iteration"`
	ValueNs     float64 `json:"value_ns"`
}

type Counter struct {
	RunID       int64   `json:"run_id"`
	BenchmarkID int64   `json:"benchmark_id"`
	Name        string  `json:"name"`
	Value       float64 `json:"value"`
}

type Profile struct {
	RunID       int64  `json:"run_id"`
	BenchmarkID int64  `json:"benchmark_id"`
	Format      string `json:"format"`
	Data        []byte `json:"-"`
}

// WatchRepo 是一个受 CI 监控的 git 仓库及其轮询游标。
type WatchRepo struct {
	ID         int64  `json:"id"`
	Name       string `json:"name"`
	Path       string `json:"path"`
	Branch     string `json:"branch"`
	LastCommit string `json:"last_commit"` // 已处理到的 commit SHA，'' = 未初始化
	LastSeen   string `json:"last_seen"`
	LastPollAt string `json:"last_poll_at"`
	LastError  string `json:"last_error"`
	CreatedAt  string `json:"created_at"`
}

// CIJob 是一次「新提交 → before/after 对比」任务的执行记录。
type CIJob struct {
	ID          int64  `json:"id"`
	WatchRepoID int64  `json:"watch_repo_id"`
	RepoName    string `json:"repo_name,omitempty"` // 仅 ListCIJobs 联表填充
	BeforeSHA   string `json:"before_sha"`
	AfterSHA    string `json:"after_sha"`
	Status      string `json:"status"` // running | success | failed
	BeforeRunID int64  `json:"before_run_id"`
	AfterRunID  int64  `json:"after_run_id"`
	Verdict     string `json:"verdict"`
	Error       string `json:"error"`
	StartedAt   string `json:"started_at"`
	FinishedAt  string `json:"finished_at,omitempty"`
}

// CompareResult 是单个基准 before/after 对比的统计结论。
type CompareResult struct {
	Benchmark    string  `json:"benchmark"`
	Kind         string  `json:"kind"`
	Unit         string  `json:"unit"`
	NBefore      int     `json:"n_before"`
	NAfter       int     `json:"n_after"`
	MedianBefore float64 `json:"median_before"`
	MedianAfter  float64 `json:"median_after"`
	Speedup      float64 `json:"speedup"`   // before/after，>1 表示变快
	DeltaPct     float64 `json:"delta_pct"` // (after-before)/before*100，负值表示变快
	CILow        float64 `json:"ci_low"`    // speedup 的 bootstrap 95% CI
	CIHigh       float64 `json:"ci_high"`
	PValue       float64 `json:"p_value"`   // Mann-Whitney U
	Significant  bool    `json:"significant"`
	Note         string  `json:"note"` // "样本不足" / "差异<5%不可信" 等
}
