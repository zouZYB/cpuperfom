package flame

import "testing"

func TestParseFolded(t *testing.T) {
	text := `main;matmul_naive 100
main;matmul_naive;inner 50
main;init 20

# comment
main;matmul_naive 30
`
	stacks, err := ParseFolded(text)
	if err != nil {
		t.Fatal(err)
	}
	if stacks["main;matmul_naive"] != 130 {
		t.Fatalf("dup stack should merge: %v", stacks["main;matmul_naive"])
	}
	if stacks["main;matmul_naive;inner"] != 50 || stacks["main;init"] != 20 {
		t.Fatalf("stacks: %v", stacks)
	}
}

func TestParseFoldedErrors(t *testing.T) {
	if _, err := ParseFolded("no-count-line"); err == nil {
		t.Fatal("missing count should error")
	}
	if _, err := ParseFolded("stack abc"); err == nil {
		t.Fatal("bad count should error")
	}
}

func TestBuildTree(t *testing.T) {
	before, _ := ParseFolded("main;a;x 100\nmain;b 50")
	after, _ := ParseFolded("main;a;x 30\nmain;a;y 20\nmain;b 50")
	root := BuildTree(before, after)

	if root.Before != 150 || root.After != 100 {
		t.Fatalf("root totals: before=%d after=%d", root.Before, root.After)
	}
	main := root.Children[0]
	if main.Name != "main" || main.Before != 150 || main.After != 100 {
		t.Fatalf("main: %+v", main)
	}
	// main 的子节点按名字排序：a, b
	a := main.Children[0]
	if a.Name != "a" || a.Before != 100 || a.After != 50 {
		t.Fatalf("a: %+v", a)
	}
	// a 的子节点：x（before 100/after 30）、y（0/20）
	x, y := a.Children[0], a.Children[1]
	if x.Name != "x" || x.Before != 100 || x.After != 30 {
		t.Fatalf("x: %+v", x)
	}
	if y.Name != "y" || y.Before != 0 || y.After != 20 {
		t.Fatalf("y: %+v", y)
	}
}

func TestPrune(t *testing.T) {
	before, _ := ParseFolded("main;big 100\nmain;small 2")
	root := BuildTree(before, nil)
	root.Prune(10)
	main := root.Children[0]
	if len(main.Children) != 1 || main.Children[0].Name != "big" {
		t.Fatalf("prune should keep only big: %+v", main.Children)
	}
}

func TestDiffRatio(t *testing.T) {
	n := &Node{Before: 100, After: 150}
	if r := n.DiffRatio(); r != 0.5 {
		t.Fatalf("DiffRatio = %v, want 0.5", r)
	}
	// before 为 0 时不除零
	n = &Node{Before: 0, After: 10}
	if r := n.DiffRatio(); r != 10 {
		t.Fatalf("DiffRatio with before=0: %v", r)
	}
}
