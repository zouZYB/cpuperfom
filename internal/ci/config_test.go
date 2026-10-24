package ci

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

// writeConfig 在临时目录写 ci.yaml 并返回路径；repoDir 作为受监控仓库（需含 .git）。
func writeConfig(t *testing.T, repoDir, content string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "ci.yaml")
	if err := os.WriteFile(p, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func fakeRepo(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	if err := os.Mkdir(filepath.Join(dir, ".git"), 0o755); err != nil {
		t.Fatal(err)
	}
	return dir
}

func TestLoadConfigMergeDefaults(t *testing.T) {
	repo := fakeRepo(t)
	p := writeConfig(t, repo, `
defaults:
  interval: 30s
  rounds: 2
  branch: master
repos:
  - path: `+filepath.ToSlash(repo)+`
`)
	cfg, err := LoadConfig(p)
	if err != nil {
		t.Fatal(err)
	}
	if len(cfg.Repos) != 1 {
		t.Fatalf("repos: %d", len(cfg.Repos))
	}
	r := cfg.Repos[0]
	if r.Name != filepath.Base(repo) {
		t.Fatalf("name should default to dir base, got %q", r.Name)
	}
	if r.IntervalD() != 30*time.Second {
		t.Fatalf("interval: %v", r.IntervalD())
	}
	if r.Rounds != 2 || r.Branch != "master" {
		t.Fatalf("defaults not merged: %+v", r)
	}
	if r.Config != "bench.yaml" || r.Warmup != 1 || r.Timeout != 120 || r.BuildTimeout != 600 {
		t.Fatalf("fallback defaults wrong: %+v", r)
	}
}

func TestLoadConfigRepoOverridesDefaults(t *testing.T) {
	repo := fakeRepo(t)
	p := writeConfig(t, repo, `
defaults: { interval: 60s, rounds: 3 }
repos:
  - name: demo
    path: `+filepath.ToSlash(repo)+`
    interval: 5
    rounds: 5
    build: "make all"
`)
	cfg, err := LoadConfig(p)
	if err != nil {
		t.Fatal(err)
	}
	r := cfg.Repos[0]
	if r.IntervalD() != 5*time.Second {
		t.Fatalf("numeric interval should be seconds, got %v", r.IntervalD())
	}
	if r.Rounds != 5 || r.Build != "make all" {
		t.Fatalf("override failed: %+v", r)
	}
}

func TestLoadConfigErrors(t *testing.T) {
	repo := fakeRepo(t)

	// 缺 path
	p := writeConfig(t, repo, "repos:\n  - name: x\n")
	if _, err := LoadConfig(p); err == nil {
		t.Fatal("missing path should fail")
	}
	// path 不是 git 仓库
	notGit := t.TempDir()
	p = writeConfig(t, repo, "repos:\n  - path: "+filepath.ToSlash(notGit)+"\n")
	if _, err := LoadConfig(p); err == nil {
		t.Fatal("non-git path should fail")
	}
	// repos 为空
	p = writeConfig(t, repo, "repos: []\n")
	if _, err := LoadConfig(p); err == nil {
		t.Fatal("empty repos should fail")
	}
	// 非法 interval
	p = writeConfig(t, repo, "repos:\n  - path: "+filepath.ToSlash(repo)+"\n    interval: abc\n")
	if _, err := LoadConfig(p); err == nil {
		t.Fatal("bad interval should fail")
	}
}

func TestHeadRef(t *testing.T) {
	r := &RepoConfig{Branch: "main"}
	if r.HeadRef() != "main" {
		t.Fatalf("HeadRef: %s", r.HeadRef())
	}
	r = &RepoConfig{Branch: "main", Fetch: true, Remote: "origin"}
	if r.HeadRef() != "origin/main" {
		t.Fatalf("HeadRef with fetch: %s", r.HeadRef())
	}
}
