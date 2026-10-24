package ci

import (
	"github.com/cpuperfom/cpuperfom/internal/model"
	"github.com/cpuperfom/cpuperfom/internal/store"
	"github.com/cpuperfom/cpuperfom/runner"
	"github.com/cpuperfom/cpuperfom/runner/refcmp"
)

// ciProvider 标记由 benchd 内置监控产生的 run。
const ciProvider = "benchd-watch"

// localSink 实现 refcmp.Sink：不经过 HTTP，直接写 SQLite。
type localSink struct {
	q *store.Queries
}

var _ refcmp.Sink = (*localSink)(nil)

func newLocalSink(q *store.Queries) *localSink { return &localSink{q: q} }

// CreateRun 登记一次运行；项目不存在时自动创建（与 API 的 createRun 行为一致）。
func (s *localSink) CreateRun(project, commitSHA, ref, label string, env model.Environment) (int64, error) {
	proj, err := s.q.GetProjectByName(project)
	if err != nil {
		pid, cerr := s.q.CreateProject(project, "")
		if cerr != nil {
			return 0, cerr
		}
		proj = &model.Project{ID: pid, Name: project}
	}
	envID, err := s.q.UpsertEnvironment(&env)
	if err != nil {
		return 0, err
	}
	return s.q.CreateRun(&model.Run{
		ProjectID:  proj.ID,
		CommitSHA:  commitSHA,
		Ref:        ref,
		Label:      label,
		CIProvider: ciProvider,
		EnvID:      envID,
	})
}

func (s *localSink) UploadResults(runID int64, results []runner.BenchOutput) error {
	run, err := s.q.GetRun(runID)
	if err != nil {
		return err
	}
	for _, b := range results {
		unit := b.Unit
		if unit == "" {
			unit = "ns"
		}
		benchID, err := s.q.UpsertBenchmark(run.ProjectID, b.Name, b.Kind, unit)
		if err != nil {
			return err
		}
		samples := make([]model.Sample, len(b.Values))
		for i, v := range b.Values {
			samples[i] = model.Sample{RunID: runID, BenchmarkID: benchID, Iteration: i, ValueNs: v}
		}
		if err := s.q.InsertSamples(samples); err != nil {
			return err
		}
	}
	return nil
}

func (s *localSink) UploadCounters(runID int64, counters map[string]map[string]float64) error {
	if len(counters) == 0 {
		return nil
	}
	run, err := s.q.GetRun(runID)
	if err != nil {
		return err
	}
	for name, cs := range counters {
		benchID, err := s.q.UpsertBenchmark(run.ProjectID, name, model.KindProg, "ns")
		if err != nil {
			return err
		}
		rows := make([]model.Counter, 0, len(cs))
		for cname, v := range cs {
			rows = append(rows, model.Counter{RunID: runID, BenchmarkID: benchID, Name: cname, Value: v})
		}
		if err := s.q.InsertCounters(rows); err != nil {
			return err
		}
	}
	return nil
}

func (s *localSink) UploadProfiles(runID int64, results []runner.BenchOutput) error {
	run, err := s.q.GetRun(runID)
	if err != nil {
		return err
	}
	for _, b := range results {
		if b.Profile == "" {
			continue
		}
		benchID, err := s.q.UpsertBenchmark(run.ProjectID, b.Name, model.KindProg, "ns")
		if err != nil {
			return err
		}
		if err := s.q.UpsertProfile(runID, benchID, "folded", []byte(b.Profile)); err != nil {
			return err
		}
	}
	return nil
}

func (s *localSink) Finish(runID int64) error {
	return s.q.FinishRun(runID)
}
