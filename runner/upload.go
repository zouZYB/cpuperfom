package runner

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/cpuperfom/cpuperfom/internal/model"
)

// Uploader 负责将运行结果上报到 benchd 服务器。
type Uploader struct {
	Server string
	Token  string
	Client *http.Client
}

func NewUploader(server, token string) *Uploader {
	return &Uploader{Server: server, Token: token, Client: &http.Client{Timeout: 30 * time.Second}}
}

// CreateRun 在服务器上登记一次运行，返回 run_id。
func (u *Uploader) CreateRun(project, commitSHA, ref, label string, env model.Environment) (int64, error) {
	body := map[string]any{
		"project":    project,
		"commit_sha": commitSHA,
		"ref":        ref,
		"label":      label,
		"env":        env,
	}
	var resp struct {
		RunID int64 `json:"run_id"`
	}
	if err := u.post("/api/v1/runs", body, &resp); err != nil {
		return 0, err
	}
	return resp.RunID, nil
}

// UploadResults 上报一组基准的样本，超过 chunkSize 自动分片。
func (u *Uploader) UploadResults(runID int64, results []BenchOutput) error {
	const chunkSize = 10000
	for _, r := range results {
		for start := 0; start < len(r.Values); start += chunkSize {
			end := min(start+chunkSize, len(r.Values))
			body := map[string]any{"benchmarks": []map[string]any{{
				"name":   r.Name,
				"kind":   r.Kind,
				"unit":   r.Unit,
				"values": r.Values[start:end],
			}}}
			if err := u.post(fmt.Sprintf("/api/v1/runs/%d/samples", runID), body, nil); err != nil {
				return err
			}
		}
	}
	return nil
}

// UploadCounters 上报计数器。
func (u *Uploader) UploadCounters(runID int64, counters map[string]map[string]float64) error {
	benches := make([]map[string]any, 0, len(counters))
	for name, cs := range counters {
		benches = append(benches, map[string]any{"name": name, "counters": cs})
	}
	if len(benches) == 0 {
		return nil
	}
	return u.post(fmt.Sprintf("/api/v1/runs/%d/counters", runID), map[string]any{"benchmarks": benches}, nil)
}

// UploadProfiles 上报火焰图数据（folded 文本），无 profile 的结果自动跳过。
func (u *Uploader) UploadProfiles(runID int64, results []BenchOutput) error {
	for _, r := range results {
		if r.Profile == "" {
			continue
		}
		body := map[string]any{"benchmark": r.Name, "format": "folded", "data": r.Profile}
		if err := u.post(fmt.Sprintf("/api/v1/runs/%d/profiles", runID), body, nil); err != nil {
			return err
		}
	}
	return nil
}

// Finish 标记运行完成。
func (u *Uploader) Finish(runID int64) error {
	return u.post(fmt.Sprintf("/api/v1/runs/%d/finish", runID), map[string]any{}, nil)
}

// ListRuns 查询历史运行列表。
func (u *Uploader) ListRuns(project string, limit int) ([]model.Run, error) {
	var resp struct {
		Runs []model.Run `json:"runs"`
	}
	err := u.get(fmt.Sprintf("/api/v1/runs?project=%s&limit=%d", project, limit), &resp)
	return resp.Runs, err
}

// Compare 请求服务器对比两次运行。
func (u *Uploader) Compare(beforeID, afterID int64) (*CompareResponse, error) {
	var resp CompareResponse
	err := u.get(fmt.Sprintf("/api/v1/compare?before_run=%d&after_run=%d", beforeID, afterID), &resp)
	return &resp, err
}

// ---------- 内部 ----------

func (u *Uploader) post(path string, body, out any) error {
	data, err := json.Marshal(body)
	if err != nil {
		return err
	}
	req, err := http.NewRequest(http.MethodPost, u.Server+path, bytes.NewReader(data))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	if u.Token != "" {
		req.Header.Set("Authorization", "Bearer "+u.Token)
	}
	return u.do(req, out)
}

func (u *Uploader) get(path string, out any) error {
	req, err := http.NewRequest(http.MethodGet, u.Server+path, nil)
	if err != nil {
		return err
	}
	if u.Token != "" {
		req.Header.Set("Authorization", "Bearer "+u.Token)
	}
	return u.do(req, out)
}

func (u *Uploader) do(req *http.Request, out any) error {
	resp, err := u.Client.Do(req)
	if err != nil {
		return fmt.Errorf("%s %s: %w", req.Method, req.URL, err)
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 300 {
		return fmt.Errorf("%s %s: HTTP %d: %s", req.Method, req.URL, resp.StatusCode, data)
	}
	if out != nil {
		if err := json.Unmarshal(data, out); err != nil {
			return fmt.Errorf("decode response: %w", err)
		}
	}
	return nil
}
