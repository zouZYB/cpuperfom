package compare

import (
	"math"
	"math/rand"
	"testing"
)

func TestMedian(t *testing.T) {
	if got := Median([]float64{3, 1, 2}); got != 2 {
		t.Fatalf("odd median = %v, want 2", got)
	}
	if got := Median([]float64{4, 1, 3, 2}); got != 2.5 {
		t.Fatalf("even median = %v, want 2.5", got)
	}
	if got := Median(nil); !math.IsNaN(got) {
		t.Fatalf("empty median should be NaN, got %v", got)
	}
}

func TestMannWhitneyUKnownValue(t *testing.T) {
	// 经典教材例子：两组明显分离的小样本，U=0，p 应很小
	x := []float64{1, 2, 3, 4, 5}
	y := []float64{6, 7, 8, 9, 10}
	u, p := MannWhitneyU(x, y)
	if u != 0 {
		t.Fatalf("U = %v, want 0", u)
	}
	if p > 0.05 {
		t.Fatalf("p = %v, want < 0.05", p)
	}
}

func TestMannWhitneyUSameDistribution(t *testing.T) {
	// 同一分布抽样，p 通常不应显著
	x := []float64{10.1, 9.8, 10.2, 10.0, 9.9, 10.1, 10.0, 9.8, 10.2, 10.1}
	y := []float64{10.0, 10.1, 9.9, 10.1, 10.0, 10.2, 9.9, 10.0, 10.1, 10.0}
	_, p := MannWhitneyU(x, y)
	if p < 0.01 {
		t.Fatalf("p = %v, unexpectedly significant for same distribution", p)
	}
}

func TestBootstrapSpeedupCIContainsTrueRatio(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	before, after := make([]float64, 30), make([]float64, 30)
	for i := range before {
		before[i] = 100 + rng.NormFloat64()*3
		after[i] = 50 + rng.NormFloat64()*2
	}
	low, high := BootstrapSpeedupCI(before, after, rng)
	if low > 2.0 || high < 2.0 {
		t.Fatalf("CI [%v, %v] should contain true speedup ~2.0", low, high)
	}
	if low >= high {
		t.Fatalf("invalid CI: low=%v high=%v", low, high)
	}
}

func TestSamplesVerdicts(t *testing.T) {
	rng := rand.New(rand.NewSource(1))

	// 明显变快 50%：应显著
	before, after := make([]float64, 20), make([]float64, 20)
	for i := range before {
		before[i] = 100 + rng.NormFloat64()
		after[i] = 50 + rng.NormFloat64()
	}
	r := Samples("bench", "func", "ns", before, after, rng)
	if !r.Significant {
		t.Fatalf("expected significant, got note=%q p=%v", r.Note, r.PValue)
	}
	if math.Abs(r.Speedup-2.0) > 0.1 {
		t.Fatalf("speedup = %v, want ~2.0", r.Speedup)
	}

	// 微小差异 1%：应标"差异<5%，视为不可信"
	for i := range before {
		before[i] = 100 + rng.NormFloat64()
		after[i] = 99 + rng.NormFloat64()
	}
	r = Samples("bench", "func", "ns", before, after, rng)
	if r.Significant {
		t.Fatalf("1%% diff should not be significant")
	}

	// 样本不足：应有提示
	r = Samples("bench", "func", "ns", before[:5], after[:5], rng)
	if r.Note == "" || r.Significant {
		t.Fatalf("small sample should be flagged, got note=%q", r.Note)
	}
}
