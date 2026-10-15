// Package api 实现 cpuperfom 平台的 REST API。
package api

import (
	"encoding/json"
	"log"
	"math/rand"
	"net/http"
	"os"
	"strconv"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"

	"github.com/cpuperfom/cpuperfom/internal/compare"
	"github.com/cpuperfom/cpuperfom/internal/flame"
	"github.com/cpuperfom/cpuperfom/internal/model"
	"github.com/cpuperfom/cpuperfom/internal/store"
)

// Server 持有依赖并注册路由。
type Server struct {
	q   *store.Queries
	mux *chi.Mux
	rng *rand.Rand
}

func NewServer(q *store.Queries) *Server {
	s := &Server{q: q, mux: chi.NewMux(), rng: rand.New(rand.NewSource(time.Now().UnixNano()))}
	s.routes()
	return s
}

func (s *Server) Handler() http.Handler { return s.mux }

// SetStatic 注册 SPA 静态资源处理（未匹配到 API 的路径全部回退到 index.html）。
func (s *Server) SetStatic(fsys http.FileSystem) {
	fileSrv := http.FileServer(fsys)
	s.mux.NotFound(func(w http.ResponseWriter, r *http.Request) {
		// API 路径未匹配仍返回 404 JSON
		if len(r.URL.Path) >= 5 && r.URL.Path[:5] == "/api/" {
			writeError(w, http.StatusNotFound, "not found")
			return
		}
		p := r.URL.Path
		if p == "/" {
			fileSrv.ServeHTTP(w, r)
			return
		}
		if f, err := fsys.Open(p[1:]); err == nil {
			f.Close()
			fileSrv.ServeHTTP(w, r)
			return
		}
		// SPA 前端路由回退
		r2 := new(http.Request)
		*r2 = *r
		r2.URL.Path = "/"
		fileSrv.ServeHTTP(w, r2)
	})
}

func (s *Server) routes() {
	r := s.mux
	r.Use(middleware.RequestID, middleware.Recoverer, middleware.Logger)
	r.Get("/healthz", func(w http.ResponseWriter, _ *http.Request) { writeJSON(w, http.StatusOK, map[string]string{"status": "ok"}) })

	r.Route("/api/v1", func(r chi.Router) {
		r.Use(s.authMiddleware)
		r.Post("/projects", s.createProject)
		r.Get("/projects", s.listProjects)
		r.Post("/runs", s.createRun)
		r.Get("/runs", s.listRuns)
		r.Get("/runs/{id}", s.getRun)
		r.Get("/runs/{id}/counters", s.getRunCounters)
		r.Get("/runs/{id}/samples", s.getRunSamples)
		r.Post("/runs/{id}/samples", s.uploadSamples)
		r.Post("/runs/{id}/counters", s.uploadCounters)
		r.Post("/runs/{id}/finish", s.finishRun)
		r.Post("/runs/{id}/profiles", s.uploadProfile)
		r.Get("/compare", s.compareRuns)
		r.Get("/flamegraph/diff", s.flamegraphDiff)
	})
}

// authMiddleware：若设置了 BENCHD_TOKEN 环境变量，则写接口要求 Bearer 认证。
// M4 里程碑会替换为完整的用户/token 体系。
func (s *Server) authMiddleware(next http.Handler) http.Handler {
	token := os.Getenv("BENCHD_TOKEN")
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if token != "" && r.Method != http.MethodGet {
			if r.Header.Get("Authorization") != "Bearer "+token {
				writeError(w, http.StatusUnauthorized, "invalid or missing bearer token")
				return
			}
		}
		next.ServeHTTP(w, r)
	})
}

// ---------- 请求/响应类型 ----------

type createProjectReq struct {
	Name    string `json:"name"`
	RepoURL string `json:"repo_url"`
}

type createRunReq struct {
	Project    string            `json:"project"`
	CommitSHA  string            `json:"commit_sha"`
	Ref        string            `json:"ref"`
	Label      string            `json:"label"`
	CIProvider string            `json:"ci_provider"`
	CIRunURL   string            `json:"ci_run_url"`
	Env        model.Environment `json:"env"`
}

type uploadSamplesReq struct {
	Benchmarks []struct {
		Name   string    `json:"name"`
		Kind   string    `json:"kind"`
		Unit   string    `json:"unit"`
		Values []float64 `json:"values"`
	} `json:"benchmarks"`
}

type uploadCountersReq struct {
	Benchmarks []struct {
		Name     string             `json:"name"`
		Counters map[string]float64 `json:"counters"`
	} `json:"benchmarks"`
}

// ---------- handlers ----------

func (s *Server) createProject(w http.ResponseWriter, r *http.Request) {
	var req createProjectReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Name == "" {
		writeError(w, http.StatusBadRequest, "invalid request: name required")
		return
	}
	id, err := s.q.CreateProject(req.Name, req.RepoURL)
	if err != nil {
		writeError(w, http.StatusConflict, err.Error())
		return
	}
	writeJSON(w, http.StatusCreated, map[string]int64{"project_id": id})
}

func (s *Server) listProjects(w http.ResponseWriter, _ *http.Request) {
	projects, err := s.q.ListProjects()
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"projects": projects})
}

func (s *Server) createRun(w http.ResponseWriter, r *http.Request) {
	var req createRunReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Project == "" {
		writeError(w, http.StatusBadRequest, "invalid request: project required")
		return
	}
	label := req.Label
	if label == "" {
		label = model.LabelCI
	}
	proj, err := s.q.GetProjectByName(req.Project)
	if err != nil {
		// 项目不存在时自动创建，降低 runner 接入成本
		pid, cerr := s.q.CreateProject(req.Project, "")
		if cerr != nil {
			writeError(w, http.StatusInternalServerError, cerr.Error())
			return
		}
		proj = &model.Project{ID: pid, Name: req.Project}
	}

	envID, err := s.q.UpsertEnvironment(&req.Env)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	runID, err := s.q.CreateRun(&model.Run{
		ProjectID:  proj.ID,
		CommitSHA:  req.CommitSHA,
		Ref:        req.Ref,
		Label:      label,
		CIProvider: req.CIProvider,
		CIRunURL:   req.CIRunURL,
		EnvID:      envID,
	})
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusCreated, map[string]any{"run_id": runID, "project_id": proj.ID})
}

func (s *Server) getRun(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid run id")
		return
	}
	run, err := s.q.GetRun(id)
	if err != nil {
		writeError(w, http.StatusNotFound, "run not found")
		return
	}
	writeJSON(w, http.StatusOK, run)
}

// listRuns 按项目列出运行（?project=xxx&limit=N），用于查看历史监控数据。
func (s *Server) listRuns(w http.ResponseWriter, r *http.Request) {
	project := r.URL.Query().Get("project")
	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
	runs, err := s.q.ListRuns(project, limit)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"runs": runs})
}

// getRunCounters 返回某次运行的全部硬件计数器（按基准分组）。
func (s *Server) getRunCounters(w http.ResponseWriter, r *http.Request) {
	runID, err := strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid run id")
		return
	}
	cs, err := s.q.CountersOfRun(runID)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	byBench := map[string]map[string]float64{}
	for _, c := range cs {
		if byBench[c.Benchmark] == nil {
			byBench[c.Benchmark] = map[string]float64{}
		}
		byBench[c.Benchmark][c.Name] = c.Value
	}
	writeJSON(w, http.StatusOK, map[string]any{"run_id": runID, "counters": byBench})
}

// getRunSamples 返回某次运行的全部样本（按基准分组），用于前端箱线图。
func (s *Server) getRunSamples(w http.ResponseWriter, r *http.Request) {
	runID, err := strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid run id")
		return
	}
	samples, err := s.q.SamplesOfRun(runID)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"run_id": runID, "samples": samples})
}

func (s *Server) uploadSamples(w http.ResponseWriter, r *http.Request) {
	runID, err := strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid run id")
		return
	}
	run, err := s.q.GetRun(runID)
	if err != nil {
		writeError(w, http.StatusNotFound, "run not found")
		return
	}
	var req uploadSamplesReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "invalid json: "+err.Error())
		return
	}
	total := 0
	for _, b := range req.Benchmarks {
		unit := b.Unit
		if unit == "" {
			unit = "ns"
		}
		benchID, err := s.q.UpsertBenchmark(run.ProjectID, b.Name, b.Kind, unit)
		if err != nil {
			writeError(w, http.StatusInternalServerError, err.Error())
			return
		}
		samples := make([]model.Sample, len(b.Values))
		for i, v := range b.Values {
			samples[i] = model.Sample{RunID: runID, BenchmarkID: benchID, Iteration: i, ValueNs: v}
		}
		if err := s.q.InsertSamples(samples); err != nil {
			writeError(w, http.StatusInternalServerError, err.Error())
			return
		}
		total += len(samples)
	}
	writeJSON(w, http.StatusOK, map[string]int{"inserted": total})
}

func (s *Server) uploadCounters(w http.ResponseWriter, r *http.Request) {
	runID, err := strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid run id")
		return
	}
	run, err := s.q.GetRun(runID)
	if err != nil {
		writeError(w, http.StatusNotFound, "run not found")
		return
	}
	var req uploadCountersReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "invalid json: "+err.Error())
		return
	}
	total := 0
	for _, b := range req.Benchmarks {
		benchID, err := s.q.UpsertBenchmark(run.ProjectID, b.Name, model.KindProg, "ns")
		if err != nil {
			writeError(w, http.StatusInternalServerError, err.Error())
			return
		}
		cs := make([]model.Counter, 0, len(b.Counters))
		for name, v := range b.Counters {
			cs = append(cs, model.Counter{RunID: runID, BenchmarkID: benchID, Name: name, Value: v})
		}
		if err := s.q.InsertCounters(cs); err != nil {
			writeError(w, http.StatusInternalServerError, err.Error())
			return
		}
		total += len(cs)
	}
	writeJSON(w, http.StatusOK, map[string]int{"inserted": total})
}

func (s *Server) finishRun(w http.ResponseWriter, r *http.Request) {
	runID, err := strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid run id")
		return
	}
	if err := s.q.FinishRun(runID); err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	// M3：此处触发回归检测
	writeJSON(w, http.StatusOK, map[string]string{"status": "finished"})
}

// uploadProfile 上传某次运行某基准的火焰图数据（folded 格式文本）。
func (s *Server) uploadProfile(w http.ResponseWriter, r *http.Request) {
	runID, err := strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid run id")
		return
	}
	run, err := s.q.GetRun(runID)
	if err != nil {
		writeError(w, http.StatusNotFound, "run not found")
		return
	}
	var req struct {
		Benchmark string `json:"benchmark"`
		Format    string `json:"format"`
		Data      string `json:"data"` // folded stack 文本
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Benchmark == "" || req.Data == "" {
		writeError(w, http.StatusBadRequest, "benchmark and data required")
		return
	}
	if req.Format == "" {
		req.Format = "folded"
	}
	// 上传即校验格式，坏数据不入库
	if _, err := flame.ParseFolded(req.Data); err != nil {
		writeError(w, http.StatusBadRequest, "invalid folded format: "+err.Error())
		return
	}
	benchID, err := s.q.UpsertBenchmark(run.ProjectID, req.Benchmark, model.KindProg, "ns")
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	if err := s.q.UpsertProfile(runID, benchID, req.Format, []byte(req.Data)); err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

// flamegraphDiff 返回两次运行同一基准的差分火焰图树。
func (s *Server) flamegraphDiff(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	beforeID, err1 := strconv.ParseInt(q.Get("before_run"), 10, 64)
	afterID, err2 := strconv.ParseInt(q.Get("after_run"), 10, 64)
	benchName := q.Get("benchmark")
	if err1 != nil || err2 != nil || benchName == "" {
		writeError(w, http.StatusBadRequest, "before_run, after_run, benchmark required")
		return
	}
	load := func(runID int64) (map[string]int64, error) {
		run, err := s.q.GetRun(runID)
		if err != nil {
			return nil, err
		}
		benchID, err := s.q.UpsertBenchmark(run.ProjectID, benchName, model.KindProg, "ns")
		if err != nil {
			return nil, err
		}
		data, err := s.q.GetProfile(runID, benchID, "folded")
		if err != nil {
			return nil, err
		}
		return flame.ParseFolded(string(data))
	}
	before, err := load(beforeID)
	if err != nil {
		writeError(w, http.StatusNotFound, "before profile not found")
		return
	}
	after, err := load(afterID)
	if err != nil {
		writeError(w, http.StatusNotFound, "after profile not found")
		return
	}
	tree := flame.BuildTree(before, after)
	// 剪掉过小分支控制体积：阈值 = 总样本的 0.1%
	threshold := (tree.Before + tree.After) / 1000
	if threshold < 1 {
		threshold = 1
	}
	tree.Prune(threshold)
	writeJSON(w, http.StatusOK, tree)
}

func (s *Server) compareRuns(w http.ResponseWriter, r *http.Request) {
	beforeID, err1 := strconv.ParseInt(r.URL.Query().Get("before_run"), 10, 64)
	afterID, err2 := strconv.ParseInt(r.URL.Query().Get("after_run"), 10, 64)
	if err1 != nil || err2 != nil {
		writeError(w, http.StatusBadRequest, "before_run and after_run required")
		return
	}
	beforeRun, err := s.q.GetRun(beforeID)
	if err != nil {
		writeError(w, http.StatusNotFound, "before run not found")
		return
	}
	afterRun, err := s.q.GetRun(afterID)
	if err != nil {
		writeError(w, http.StatusNotFound, "after run not found")
		return
	}

	warning := ""
	if beforeRun.EnvID != afterRun.EnvID {
		warning = "两次运行环境不一致，结果可能不可比"
	}

	// 以 before 运行的基准集合为基准，取并集
	benches, err := s.q.BenchmarksOfRun(beforeID)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	results := make([]model.CompareResult, 0, len(benches))
	counters := map[string]any{}
	for _, b := range benches {
		xs, err := s.q.SamplesFor(beforeID, b.ID)
		if err != nil {
			continue
		}
		ys, err := s.q.SamplesFor(afterID, b.ID)
		if err != nil {
			continue
		}
		results = append(results, compare.Samples(b.Name, b.Kind, b.Unit, xs, ys, s.rng))

		// 附带计数器前后对比（有则输出）
		beforeC, _ := s.q.CountersFor(beforeID, b.ID)
		afterC, _ := s.q.CountersFor(afterID, b.ID)
		if len(beforeC) > 0 || len(afterC) > 0 {
			counters[b.Name] = map[string]any{"before": beforeC, "after": afterC}
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"before_run":  beforeID,
		"after_run":   afterID,
		"env_warning": warning,
		"results":     results,
		"counters":    counters,
	})
}

// ---------- 工具 ----------

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(v); err != nil {
		log.Printf("write json: %v", err)
	}
}

func writeError(w http.ResponseWriter, status int, msg string) {
	writeJSON(w, status, map[string]string{"error": msg})
}
