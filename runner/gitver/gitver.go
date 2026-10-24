// Package gitver 用 git worktree 检出两个版本，互不污染地编译与运行。
package gitver

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

// Pair 是 before/after 两个 worktree 的路径。
type Pair struct {
	BeforeDir string
	AfterDir  string
	repo      string
}

// Setup 为 repo 下的两个 ref 各创建一个临时 worktree。
// 调用方负责 defer pair.Cleanup()。
func Setup(repo, beforeRef, afterRef string) (*Pair, error) {
	absRepo, err := filepath.Abs(repo)
	if err != nil {
		return nil, err
	}
	if _, err := os.Stat(filepath.Join(absRepo, ".git")); err != nil {
		return nil, fmt.Errorf("%s 不是 git 仓库", absRepo)
	}

	tmp, err := os.MkdirTemp("", "cpuperfom-wt-*")
	if err != nil {
		return nil, err
	}
	p := &Pair{
		repo:      absRepo,
		BeforeDir: filepath.Join(tmp, "before"),
		AfterDir:  filepath.Join(tmp, "after"),
	}
	for _, wt := range []struct{ dir, ref string }{
		{p.BeforeDir, beforeRef}, {p.AfterDir, afterRef},
	} {
		out, err := exec.Command("git", "-C", absRepo, "worktree", "add", "--detach", wt.dir, wt.ref).CombinedOutput()
		if err != nil {
			p.Cleanup()
			return nil, fmt.Errorf("git worktree add %s: %w\n%s", wt.ref, err, out)
		}
	}
	return p, nil
}

// Build 在两个 worktree 中分别执行构建命令（如 "cmake --build build -j"）。
// buildCmd 为空则跳过（例如已提交构建产物或用脚本自行构建）。
func (p *Pair) Build(buildCmd string) error {
	return p.BuildContext(context.Background(), buildCmd)
}

// BuildContext 同 Build，但可通过 ctx 设置超时或取消（防止构建挂死）。
func (p *Pair) BuildContext(ctx context.Context, buildCmd string) error {
	if buildCmd == "" {
		return nil
	}
	for _, dir := range []string{p.BeforeDir, p.AfterDir} {
		var cmd *exec.Cmd
		if runtime.GOOS == "windows" {
			cmd = exec.CommandContext(ctx, "cmd", "/C", buildCmd)
		} else {
			cmd = exec.CommandContext(ctx, "sh", "-c", buildCmd)
		}
		cmd.Dir = dir
		out, err := cmd.CombinedOutput()
		if err != nil {
			return fmt.Errorf("build in %s: %w\n%s", dir, err, tail(string(out), 500))
		}
	}
	return nil
}

// Cleanup 移除两个 worktree（--force 容忍构建产物）。
func (p *Pair) Cleanup() {
	for _, dir := range []string{p.BeforeDir, p.AfterDir} {
		exec.Command("git", "-C", p.repo, "worktree", "remove", "--force", dir).Run()
	}
	os.RemoveAll(filepath.Dir(p.BeforeDir))
}

// ResolveRef 返回 ref 的完整 SHA（用于运行记录的 commit 字段）。
func ResolveRef(repo, ref string) string {
	out, err := exec.Command("git", "-C", repo, "rev-parse", ref).Output()
	if err != nil {
		return ref
	}
	return strings.TrimSpace(string(out))
}

func tail(s string, n int) string {
	if len(s) > n {
		return "…" + s[len(s)-n:]
	}
	return s
}
