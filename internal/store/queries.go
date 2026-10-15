package store

import (
	"database/sql"
	"fmt"

	"github.com/cpuperfom/cpuperfom/internal/model"
)

// Queries 封装所有数据库操作。
type Queries struct{ db *sql.DB }

func New(db *sql.DB) *Queries { return &Queries{db: db} }

// ---------- 项目 ----------

func (q *Queries) CreateProject(name, repoURL string) (int64, error) {
	res, err := q.db.Exec(`INSERT INTO projects (name, repo_url) VALUES (?, ?)`, name, repoURL)
	if err != nil {
		return 0, fmt.Errorf("create project: %w", err)
	}
	return res.LastInsertId()
}

func (q *Queries) GetProjectByName(name string) (*model.Project, error) {
	var p model.Project
	err := q.db.QueryRow(`SELECT id, name, repo_url, created_at FROM projects WHERE name = ?`, name).
		Scan(&p.ID, &p.Name, &p.RepoURL, &p.CreatedAt)
	if err != nil {
		return nil, err
	}
	return &p, nil
}

func (q *Queries) ListProjects() ([]model.Project, error) {
	rows, err := q.db.Query(`SELECT id, name, repo_url, created_at FROM projects ORDER BY name`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []model.Project
	for rows.Next() {
		var p model.Project
		if err := rows.Scan(&p.ID, &p.Name, &p.RepoURL, &p.CreatedAt); err != nil {
			return nil, err
		}
		out = append(out, p)
	}
	return out, rows.Err()
}

// ---------- 环境快照 ----------

// UpsertEnvironment 按 hash 去重插入，返回环境 ID。
func (q *Queries) UpsertEnvironment(e *model.Environment) (int64, error) {
	if e.Hash == "" {
		e.Hash = e.ComputeHash()
	}
	_, err := q.db.Exec(`INSERT OR IGNORE INTO environments
		(hash, cpu_model, microarch, cores, freq_governor, os, kernel, compiler, flags)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		e.Hash, e.CPUModel, e.Microarch, e.Cores, e.FreqGovernor, e.OS, e.Kernel, e.Compiler, e.Flags)
	if err != nil {
		return 0, fmt.Errorf("upsert environment: %w", err)
	}
	var id int64
	err = q.db.QueryRow(`SELECT id FROM environments WHERE hash = ?`, e.Hash).Scan(&id)
	return id, err
}

// ---------- 运行 ----------

func (q *Queries) CreateRun(r *model.Run) (int64, error) {
	// EnvID 为 0 表示无环境快照，写 NULL 避免外键约束失败
	var envID any
	if r.EnvID != 0 {
		envID = r.EnvID
	}
	res, err := q.db.Exec(`INSERT INTO runs
		(project_id, commit_sha, ref, label, ci_provider, ci_run_url, env_id)
		VALUES (?, ?, ?, ?, ?, ?, ?)`,
		r.ProjectID, r.CommitSHA, r.Ref, r.Label, r.CIProvider, r.CIRunURL, envID)
	if err != nil {
		return 0, fmt.Errorf("create run: %w", err)
	}
	return res.LastInsertId()
}

func (q *Queries) FinishRun(id int64) error {
	_, err := q.db.Exec(`UPDATE runs SET finished_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?`, id)
	return err
}

func (q *Queries) GetRun(id int64) (*model.Run, error) {
	var r model.Run
	err := q.db.QueryRow(`SELECT id, project_id, commit_sha, ref, label, ci_provider, ci_run_url,
		COALESCE(env_id, 0), started_at, COALESCE(finished_at, '') FROM runs WHERE id = ?`, id).
		Scan(&r.ID, &r.ProjectID, &r.CommitSHA, &r.Ref, &r.Label, &r.CIProvider, &r.CIRunURL,
			&r.EnvID, &r.StartedAt, &r.FinishedAt)
	if err != nil {
		return nil, err
	}
	return &r, nil
}

// ListRuns 按项目名列出运行，按开始时间倒序。project 为空时列出全部。
func (q *Queries) ListRuns(project string, limit int) ([]model.Run, error) {
	if limit <= 0 {
		limit = 50
	}
	rows, err := q.db.Query(`SELECT r.id, r.project_id, r.commit_sha, r.ref, r.label,
		r.ci_provider, r.ci_run_url, COALESCE(r.env_id, 0), r.started_at, COALESCE(r.finished_at, '')
		FROM runs r JOIN projects p ON p.id = r.project_id
		WHERE (? = '' OR p.name = ?)
		ORDER BY r.started_at DESC LIMIT ?`, project, project, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []model.Run
	for rows.Next() {
		var r model.Run
		if err := rows.Scan(&r.ID, &r.ProjectID, &r.CommitSHA, &r.Ref, &r.Label,
			&r.CIProvider, &r.CIRunURL, &r.EnvID, &r.StartedAt, &r.FinishedAt); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, rows.Err()
}

// ---------- 基准与样本 ----------

// UpsertBenchmark 返回 (project_id, name) 对应的基准 ID。
func (q *Queries) UpsertBenchmark(projectID int64, name, kind, unit string) (int64, error) {
	_, err := q.db.Exec(`INSERT OR IGNORE INTO benchmarks (project_id, name, kind, unit) VALUES (?, ?, ?, ?)`,
		projectID, name, kind, unit)
	if err != nil {
		return 0, err
	}
	var id int64
	err = q.db.QueryRow(`SELECT id FROM benchmarks WHERE project_id = ? AND name = ?`, projectID, name).Scan(&id)
	return id, err
}

// InsertSamples 在单事务中批量写入样本。
func (q *Queries) InsertSamples(samples []model.Sample) error {
	tx, err := q.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	stmt, err := tx.Prepare(`INSERT INTO samples (run_id, benchmark_id, iteration, value_ns) VALUES (?, ?, ?, ?)`)
	if err != nil {
		return err
	}
	defer stmt.Close()
	for _, s := range samples {
		if _, err := stmt.Exec(s.RunID, s.BenchmarkID, s.Iteration, s.ValueNs); err != nil {
			return err
		}
	}
	return tx.Commit()
}

// SamplesFor 取某次运行中某基准的全部样本值（按迭代序）。
func (q *Queries) SamplesFor(runID, benchmarkID int64) ([]float64, error) {
	rows, err := q.db.Query(`SELECT value_ns FROM samples WHERE run_id = ? AND benchmark_id = ? ORDER BY iteration`,
		runID, benchmarkID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []float64
	for rows.Next() {
		var v float64
		if err := rows.Scan(&v); err != nil {
			return nil, err
		}
		out = append(out, v)
	}
	return out, rows.Err()
}

// BenchmarksOfRun 列出某次运行涉及的所有基准。
func (q *Queries) BenchmarksOfRun(runID int64) ([]model.Benchmark, error) {
	rows, err := q.db.Query(`SELECT DISTINCT b.id, b.project_id, b.name, b.kind, b.unit
		FROM benchmarks b JOIN samples s ON s.benchmark_id = b.id WHERE s.run_id = ? ORDER BY b.name`, runID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []model.Benchmark
	for rows.Next() {
		var b model.Benchmark
		if err := rows.Scan(&b.ID, &b.ProjectID, &b.Name, &b.Kind, &b.Unit); err != nil {
			return nil, err
		}
		out = append(out, b)
	}
	return out, rows.Err()
}

// InsertCounters 批量写入计数器（重复则覆盖）。
func (q *Queries) InsertCounters(counters []model.Counter) error {
	tx, err := q.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	stmt, err := tx.Prepare(`INSERT OR REPLACE INTO counters (run_id, benchmark_id, name, value) VALUES (?, ?, ?, ?)`)
	if err != nil {
		return err
	}
	defer stmt.Close()
	for _, c := range counters {
		if _, err := stmt.Exec(c.RunID, c.BenchmarkID, c.Name, c.Value); err != nil {
			return err
		}
	}
	return tx.Commit()
}

// BenchSample 是一个带基准名和单位的分组样本集。
type BenchSample struct {
	Benchmark string    `json:"benchmark"`
	Kind      string    `json:"kind"`
	Unit      string    `json:"unit"`
	Values    []float64 `json:"values"`
}

// SamplesOfRun 取某次运行的全部样本，按基准分组。
func (q *Queries) SamplesOfRun(runID int64) ([]BenchSample, error) {
	benches, err := q.BenchmarksOfRun(runID)
	if err != nil {
		return nil, err
	}
	out := make([]BenchSample, 0, len(benches))
	for _, b := range benches {
		vals, err := q.SamplesFor(runID, b.ID)
		if err != nil {
			return nil, err
		}
		out = append(out, BenchSample{Benchmark: b.Name, Kind: b.Kind, Unit: b.Unit, Values: vals})
	}
	return out, nil
}

// UpsertProfile 写入（或覆盖）某次运行某基准的火焰图数据。
func (q *Queries) UpsertProfile(runID, benchmarkID int64, format string, data []byte) error {
	_, err := q.db.Exec(`INSERT OR REPLACE INTO profiles (run_id, benchmark_id, format, data)
		VALUES (?, ?, ?, ?)`, runID, benchmarkID, format, data)
	return err
}

// GetProfile 读取某次运行某基准的火焰图数据。
func (q *Queries) GetProfile(runID, benchmarkID int64, format string) ([]byte, error) {
	var data []byte
	err := q.db.QueryRow(`SELECT data FROM profiles WHERE run_id = ? AND benchmark_id = ? AND format = ?`,
		runID, benchmarkID, format).Scan(&data)
	return data, err
}

// BenchCounter 是一条带基准名的计数器记录。
type BenchCounter struct {
	Benchmark string  `json:"benchmark"`
	Name      string  `json:"name"`
	Value     float64 `json:"value"`
}

// CountersOfRun 取某次运行的全部计数器（带基准名）。
func (q *Queries) CountersOfRun(runID int64) ([]BenchCounter, error) {
	rows, err := q.db.Query(`SELECT b.name, c.name, c.value FROM counters c
		JOIN benchmarks b ON b.id = c.benchmark_id
		WHERE c.run_id = ? ORDER BY b.name, c.name`, runID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []BenchCounter
	for rows.Next() {
		var c BenchCounter
		if err := rows.Scan(&c.Benchmark, &c.Name, &c.Value); err != nil {
			return nil, err
		}
		out = append(out, c)
	}
	return out, rows.Err()
}

// CountersFor 取某次运行中某基准的全部计数器。
func (q *Queries) CountersFor(runID, benchmarkID int64) (map[string]float64, error) {
	rows, err := q.db.Query(`SELECT name, value FROM counters WHERE run_id = ? AND benchmark_id = ?`, runID, benchmarkID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := map[string]float64{}
	for rows.Next() {
		var name string
		var v float64
		if err := rows.Scan(&name, &v); err != nil {
			return nil, err
		}
		out[name] = v
	}
	return out, rows.Err()
}
