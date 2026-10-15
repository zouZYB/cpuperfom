// Package runner 实现跨平台基准执行器（bench-runner）。
package runner

import (
	"fmt"
	"os"

	"gopkg.in/yaml.v3"
)

// Config 对应被测项目仓库中的 bench.yaml。
type Config struct {
	Project    string   `yaml:"project"`
	Benchmarks []Bench  `yaml:"benchmarks"`
	Counters   []string `yaml:"counters"`
	Profile    struct {
		Enabled bool   `yaml:"enabled"`
		Tool    string `yaml:"tool"`
		Freq    int    `yaml:"freq"`
	} `yaml:"profile"`
}

// Bench 描述一个基准项。
type Bench struct {
	Name    string `yaml:"name"`
	Kind    string `yaml:"kind"` // func | prog
	Command string `yaml:"command"`
	Repeat  int    `yaml:"repeat"` // prog 级：重复执行次数
}

func LoadConfig(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read config: %w", err)
	}
	var c Config
	if err := yaml.Unmarshal(data, &c); err != nil {
		return nil, fmt.Errorf("parse config: %w", err)
	}
	if c.Project == "" {
		return nil, fmt.Errorf("config: project name required")
	}
	for i, b := range c.Benchmarks {
		if b.Name == "" || b.Command == "" {
			return nil, fmt.Errorf("config: benchmark #%d missing name or command", i)
		}
		if b.Kind == "" {
			c.Benchmarks[i].Kind = "prog"
		}
		if c.Benchmarks[i].Kind == "prog" && b.Repeat <= 0 {
			c.Benchmarks[i].Repeat = 20
		}
	}
	return &c, nil
}
