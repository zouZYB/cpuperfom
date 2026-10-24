// Package ci 实现 benchd 内置的持续性能监控：轮询 git 仓库，
// 发现新提交后自动执行 before/after 性能对比并入库。
package ci

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"time"

	"gopkg.in/yaml.v3"
)

// Config 对应 ci.yaml：benchd 持续性能监控配置。
type Config struct {
	Defaults RepoConfig   `yaml:"defaults"`
	Repos    []RepoConfig `yaml:"repos"`
}

// RepoConfig 描述一个受监控的 git 仓库。
type RepoConfig struct {
	Name         string `yaml:"name"`          // 缺省取 path 目录名
	Path         string `yaml:"path"`          // 本地 git 仓库路径（必填）
	Branch       string `yaml:"branch"`        // 监控分支，默认 main
	Config       string `yaml:"config"`        // bench.yaml 路径，相对仓库根（在 after worktree 内读取）
	Build        string `yaml:"build"`         // 构建命令，在两个 worktree 内分别执行；可空
	Interval     string `yaml:"interval"`      // 轮询间隔（"60s" 或秒数）
	Rounds       int    `yaml:"rounds"`        // ABAB 交替轮数，默认 3
	Warmup       int    `yaml:"warmup"`        // 预热轮数，默认 1
	Timeout      int    `yaml:"timeout"`       // 单次基准命令超时秒数，默认 120
	BuildTimeout int    `yaml:"build_timeout"` // 构建超时秒数，默认 600
	Fetch        bool   `yaml:"fetch"`         // 轮询前是否 git fetch（监控远端时）
	Remote       string `yaml:"remote"`        // fetch 的远端名，默认 origin

	interval time.Duration // Interval 解析结果
}

// IntervalD 返回解析后的轮询间隔。
func (r *RepoConfig) IntervalD() time.Duration { return r.interval }

// HeadRef 返回轮询要比对的 ref（fetch 模式为 remote/branch）。
func (r *RepoConfig) HeadRef() string {
	if r.Fetch {
		return r.Remote + "/" + r.Branch
	}
	return r.Branch
}

// LoadConfig 读取 ci.yaml，合并 defaults 并校验。
func LoadConfig(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read ci config: %w", err)
	}
	var c Config
	if err := yaml.Unmarshal(data, &c); err != nil {
		return nil, fmt.Errorf("parse ci config: %w", err)
	}
	if len(c.Repos) == 0 {
		return nil, fmt.Errorf("ci config: repos 为空")
	}
	for i := range c.Repos {
		if err := c.Repos[i].mergeDefaults(&c.Defaults); err != nil {
			return nil, fmt.Errorf("ci config repo #%d: %w", i, err)
		}
	}
	return &c, nil
}

// mergeDefaults 用 defaults 填充零值字段并校验。
func (r *RepoConfig) mergeDefaults(d *RepoConfig) error {
	if r.Path == "" {
		return fmt.Errorf("path 必填")
	}
	abs, err := filepath.Abs(r.Path)
	if err != nil {
		return err
	}
	r.Path = abs
	if _, err := os.Stat(filepath.Join(r.Path, ".git")); err != nil {
		return fmt.Errorf("%s 不是 git 仓库", r.Path)
	}
	if r.Name == "" {
		r.Name = filepath.Base(r.Path)
	}
	if r.Branch == "" {
		r.Branch = firstNonEmpty(d.Branch, "main")
	}
	if r.Config == "" {
		r.Config = firstNonEmpty(d.Config, "bench.yaml")
	}
	if r.Interval == "" {
		r.Interval = firstNonEmpty(d.Interval, "60s")
	}
	if r.Rounds == 0 {
		r.Rounds = firstNonZero(d.Rounds, 3)
	}
	if r.Warmup == 0 {
		r.Warmup = firstNonZero(d.Warmup, 1)
	}
	if r.Timeout == 0 {
		r.Timeout = firstNonZero(d.Timeout, 120)
	}
	if r.BuildTimeout == 0 {
		r.BuildTimeout = firstNonZero(d.BuildTimeout, 600)
	}
	if r.Fetch && r.Remote == "" {
		r.Remote = firstNonEmpty(d.Remote, "origin")
	}
	iv, err := parseInterval(r.Interval)
	if err != nil {
		return fmt.Errorf("interval %q: %w", r.Interval, err)
	}
	r.interval = iv
	return nil
}

// parseInterval 支持 "60s"/"5m" 时长字符串或纯数字（秒）。
func parseInterval(s string) (time.Duration, error) {
	if d, err := time.ParseDuration(s); err == nil {
		if d <= 0 {
			return 0, fmt.Errorf("必须为正数")
		}
		return d, nil
	}
	if n, err := strconv.Atoi(s); err == nil && n > 0 {
		return time.Duration(n) * time.Second, nil
	}
	return 0, fmt.Errorf("无法解析（支持 60s、5m 或秒数）")
}

func firstNonEmpty(v, def string) string {
	if v != "" {
		return v
	}
	return def
}

func firstNonZero(v, def int) int {
	if v != 0 {
		return v
	}
	return def
}
