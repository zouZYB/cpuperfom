// Package flame 实现 folded stack 格式火焰图数据的解析、树构建与差分。
//
// folded 格式（Brendan Gregg FlameGraph）：
//   frame_a;frame_b;frame_c <count>
// 每行一个调用栈（根在前），末尾空格分隔采样计数。
package flame

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
)

// Node 是火焰图树节点。Before/After 为该帧（含子树）在两次 profile 中的样本数。
type Node struct {
	Name     string  `json:"name"`
	Before   int64   `json:"before"`
	After    int64   `json:"after"`
	Children []*Node `json:"children,omitempty"`
}

// ParseFolded 解析 folded 文本为 调用栈→计数。
func ParseFolded(text string) (map[string]int64, error) {
	stacks := map[string]int64{}
	for i, line := range strings.Split(text, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		idx := strings.LastIndex(line, " ")
		if idx < 0 {
			return nil, fmt.Errorf("line %d: missing count", i+1)
		}
		count, err := strconv.ParseInt(strings.TrimSpace(line[idx+1:]), 10, 64)
		if err != nil {
			return nil, fmt.Errorf("line %d: bad count: %w", i+1, err)
		}
		stack := strings.TrimSpace(line[:idx])
		if stack == "" {
			return nil, fmt.Errorf("line %d: empty stack", i+1)
		}
		stacks[stack] += count
	}
	return stacks, nil
}

// BuildTree 把两次 profile 的调用栈集合合并为一棵差分树。
// 根节点名为 "root"，Before/After 为总样本数。
func BuildTree(before, after map[string]int64) *Node {
	root := &Node{Name: "root"}
	insert := func(stacks map[string]int64, isBefore bool) {
		for stack, count := range stacks {
			node := root
			if isBefore {
				node.Before += count
			} else {
				node.After += count
			}
			for _, frame := range strings.Split(stack, ";") {
				child := findChild(node, frame)
				if child == nil {
					child = &Node{Name: frame}
					node.Children = append(node.Children, child)
				}
				if isBefore {
					child.Before += count
				} else {
					child.After += count
				}
				node = child
			}
		}
	}
	insert(before, true)
	insert(after, false)
	sortChildren(root)
	return root
}

func findChild(n *Node, name string) *Node {
	for _, c := range n.Children {
		if c.Name == name {
			return c
		}
	}
	return nil
}

func sortChildren(n *Node) {
	sort.Slice(n.Children, func(i, j int) bool {
		return n.Children[i].Name < n.Children[j].Name
	})
	for _, c := range n.Children {
		sortChildren(c)
	}
}

// Prune 剪掉 Before+After 小于 minSamples 的子树，控制输出体积。
// 返回剪枝后的树（原地修改）。
func (n *Node) Prune(minSamples int64) *Node {
	kept := n.Children[:0]
	for _, c := range n.Children {
		if c.Before+c.After >= minSamples {
			kept = append(kept, c.Prune(minSamples))
		}
	}
	if len(kept) == 0 {
		n.Children = nil
	} else {
		n.Children = kept
	}
	return n
}

// DiffRatio 计算节点的差分比例：(after-before)/max(before,1)，
// 正值=变慢（红），负值=变快（蓝），供前端着色。
func (n *Node) DiffRatio() float64 {
	base := n.Before
	if base < 1 {
		base = 1
	}
	return float64(n.After-n.Before) / float64(base)
}
