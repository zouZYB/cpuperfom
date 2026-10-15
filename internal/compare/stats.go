// Package compare 实现优化前后样本的统计对比：
// 中位数、加速比 bootstrap 置信区间、Mann-Whitney U 显著性检验。
package compare

import (
	"math"
	"math/rand"
	"sort"

	"github.com/cpuperfom/cpuperfom/internal/model"
)

// MinSamples 是出统计结论所需的最小样本量（每侧）。
const MinSamples = 10

// SignificanceAlpha 与 MinEffectPct：p < alpha 且 |Δ| >= MinEffectPct 才算可信差异。
const (
	SignificanceAlpha = 0.05
	MinEffectPct      = 5.0
	bootstrapRounds   = 2000
)

// Median 返回中位数。输入会被复制排序，原切片不受影响。
func Median(xs []float64) float64 {
	n := len(xs)
	if n == 0 {
		return math.NaN()
	}
	s := make([]float64, n)
	copy(s, xs)
	sort.Float64s(s)
	if n%2 == 1 {
		return s[n/2]
	}
	return (s[n/2-1] + s[n/2]) / 2
}

// BootstrapSpeedupCI 对加速比 before/after 的中位数之比做 bootstrap 重采样，
// 返回 95% 百分位置信区间。
func BootstrapSpeedupCI(before, after []float64, rng *rand.Rand) (low, high float64) {
	if len(before) == 0 || len(after) == 0 {
		return math.NaN(), math.NaN()
	}
	ratios := make([]float64, 0, bootstrapRounds)
	nb, na := len(before), len(after)
	for i := 0; i < bootstrapRounds; i++ {
		sb := make([]float64, nb)
		sa := make([]float64, na)
		for j := range sb {
			sb[j] = before[rng.Intn(nb)]
		}
		for j := range sa {
			sa[j] = after[rng.Intn(na)]
		}
		mb, ma := Median(sb), Median(sa)
		if ma > 0 {
			ratios = append(ratios, mb/ma)
		}
	}
	if len(ratios) < 100 {
		return math.NaN(), math.NaN()
	}
	sort.Float64s(ratios)
	low = ratios[int(0.025*float64(len(ratios)))]
	high = ratios[int(0.975*float64(len(ratios)))-1]
	return low, high
}

// MannWhitneyU 执行双侧 Mann-Whitney U 检验（正态近似 + 连续性校正 + 并列秩校正）。
// 返回 U 统计量与 p 值；样本量过小（<5）时 p 值不可靠，调用方应以 MinSamples 兜底。
func MannWhitneyU(x, y []float64) (u, p float64) {
	nx, ny := len(x), len(y)
	if nx == 0 || ny == 0 {
		return math.NaN(), math.NaN()
	}
	type obs struct {
		v   float64
		isX bool
	}
	all := make([]obs, 0, nx+ny)
	for _, v := range x {
		all = append(all, obs{v, true})
	}
	for _, v := range y {
		all = append(all, obs{v, false})
	}
	sort.Slice(all, func(i, j int) bool { return all[i].v < all[j].v })

	// 赋秩（并列取平均秩），同时统计并列组大小用于方差校正
	rankX := 0.0
	tieSum := 0.0
	for i := 0; i < len(all); {
		j := i
		for j < len(all) && all[j].v == all[i].v {
			j++
		}
		avgRank := float64(i+j+1) / 2 // 秩从 1 开始：i+1..j 的平均
		t := float64(j - i)
		if t > 1 {
			tieSum += t*t*t - t
		}
		for k := i; k < j; k++ {
			if all[k].isX {
				rankX += avgRank
			}
		}
		i = j
	}

	fnx, fny := float64(nx), float64(ny)
	u1 := rankX - fnx*(fnx+1)/2
	u2 := fnx*fny - u1
	u = math.Min(u1, u2)

	n := fnx + fny
	meanU := fnx * fny / 2
	varU := fnx * fny / 12 * ((n + 1) - tieSum/(n*(n-1)))
	if varU <= 0 {
		return u, 1.0
	}
	z := (u - meanU + 0.5) / math.Sqrt(varU) // 连续性校正
	p = 2 * (1 - normalCDF(math.Abs(z)))
	return u, p
}

// normalCDF 标准正态分布累积函数（erf 实现）。
func normalCDF(x float64) float64 {
	return 0.5 * (1 + math.Erf(x/math.Sqrt2))
}

// Samples 对比两组样本，输出完整统计结论。
func Samples(name, kind, unit string, before, after []float64, rng *rand.Rand) model.CompareResult {
	r := model.CompareResult{
		Benchmark:    name,
		Kind:         kind,
		Unit:         unit,
		NBefore:      len(before),
		NAfter:       len(after),
		MedianBefore: Median(before),
		MedianAfter:  Median(after),
	}
	if len(before) == 0 || len(after) == 0 || r.MedianAfter <= 0 {
		r.Note = "数据缺失"
		return r
	}
	r.Speedup = r.MedianBefore / r.MedianAfter
	r.DeltaPct = (r.MedianAfter - r.MedianBefore) / r.MedianBefore * 100
	r.CILow, r.CIHigh = BootstrapSpeedupCI(before, after, rng)

	if len(before) < MinSamples || len(after) < MinSamples {
		r.Note = "样本不足（<10），结论仅供参考"
		return r
	}
	_, r.PValue = MannWhitneyU(before, after)
	r.Significant = r.PValue < SignificanceAlpha && math.Abs(r.DeltaPct) >= MinEffectPct
	if r.PValue >= SignificanceAlpha {
		r.Note = "差异不显著（p≥0.05）"
	} else if math.Abs(r.DeltaPct) < MinEffectPct {
		r.Note = "差异<5%，视为不可信"
	}
	return r
}
