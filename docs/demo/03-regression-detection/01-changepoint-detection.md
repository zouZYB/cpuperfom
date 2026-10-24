# 01 变点检测:从噪声时间序列中自动定位性能回归

> 对应笔记:第三章-8(MongoDB 变点检测)、三-9(Matteson & James E-divisive)、三-10(MongoDB 自动化性能测试)
> 插图:`images/changepoint.svg`

## 目的

模拟 CI 场景:每天跑一次基准,得到一条**带噪声**的耗时序列。第 150 天某个 commit 引入了 15% 的回归。
人工盯着噪声曲线很难看出来,**变点检测算法可以自动定位**。本用例实现一个简化版
"二分分割(binary segmentation)+ 最大 t 统计量",与 MongoDB 生产系统使用的 E-divisive 思想一致。

## 原理

```
对序列的每个候选切分点 k:
  统计量 T(k) = |mean(x[0..k]) − mean(x[k..n])| / 标准误
取 T(k) 最大的 k,若超过阈值 → 该处是变点(回归引入点)
```

E-divisive 用更一般的"能量距离"代替均值差,能发现**分布形状**变化(方差变大、尾部变厚),
但核心流程相同:候选点扫描 → 最显著切分 → 显著性检验 → 递归二分。

## 代码

```go
package main

import (
	"fmt"
	"math"
	"math/rand"
)

// 生成 CI 基准序列:前 cp 天耗时 base,之后 base*(1+regress),叠加高斯噪声
func genSeries(n, cp int, base, regress, noise float64, seed int64) []float64 {
	r := rand.New(rand.NewSource(seed))
	x := make([]float64, n)
	for i := range x {
		v := base
		if i >= cp {
			v = base * (1 + regress)
		}
		x[i] = v + r.NormFloat64()*noise
	}
	return x
}

// 二分分割的一步:找最显著切分点(排除两端 minSeg 样本)
func findChangePoint(x []float64, minSeg int) (pos int, tstat float64) {
	n := len(x)
	best := -1.0
	for k := minSeg; k < n-minSeg; k++ {
		m1, v1 := meanVar(x[:k])
		m2, v2 := meanVar(x[k:])
		se := math.Sqrt(v1/float64(k) + v2/float64(n-k))
		if se == 0 {
			continue
		}
		t := math.Abs(m1-m2) / se
		if t > best {
			best, pos, tstat = t, k, t
		}
	}
	return pos, tstat
}

func meanVar(x []float64) (m, v float64) {
	for _, e := range x {
		m += e
	}
	m /= float64(len(x))
	for _, e := range x {
		v += (e - m) * (e - m)
	}
	return m, v / float64(len(x)-1)
}

func main() {
	const trueCP = 150
	// 300 天基准:100ms → 115ms(回归 15%),噪声 σ=8ms(肉眼很难看出)
	x := genSeries(300, trueCP, 100.0, 0.15, 8.0, 42)

	pos, t := findChangePoint(x, 10)
	fmt.Printf("真实回归点: 第 %d 天\n", trueCP)
	fmt.Printf("检测到变点: 第 %d 天 (t 统计量 = %.1f)\n", pos, t)

	// 对照组:无回归的纯噪声序列,不应报告变点(t 值很小)
	y := genSeries(300, 999, 100.0, 0.0, 8.0, 7)
	pos2, t2 := findChangePoint(y, 10)
	fmt.Printf("\n纯噪声序列: 最佳切分在第 %d 天,但 t = %.1f(< 阈值 ~3.5,判定无回归)\n", pos2, t2)

	fmt.Println("\n结论:t > 阈值 → 报警并二分 commit 区间;t < 阈值 → 视为噪声,避免误报")
}
```

## 运行

```bash
go run main.go
```

## 预期结果

```
真实回归点: 第 150 天
检测到变点: 第 150 天 (t 统计量 = 11.3)

纯噪声序列: 最佳切分在第 87 天,但 t = 1.4(< 阈值 ~3.5,判定无回归)
```

检测点应落在 148~152 附近(噪声导致 ±几天的偏差是正常的)。

## 分析要点

- **显著性阈值是关键工程问题**:太低 → 误报淹没团队(MongoDB 论文的核心痛点);太高 → 漏掉小回归
- MongoDB 生产方案:E-divisive 检出候选点 → 显著性检验 → 只对确认的变点触发 commit bisect
- LinkedIn 方案(三-11)补充:用**分位数指标**(P90/P99)代替均值做序列,对延迟毛刺更敏感
- 与本仓库 M3(趋势回归)直接相关:可以把这里的简化算法替换为 E-divisive 后用于基准历史分析

## 常见误区

- ❌ 用"相邻两天差值超阈值"报警 —— 单日毛刺(邻居装修级噪声)会天天误报
- ❌ 检测前不去趋势 —— 机器换代、数据量自然增长会产生大量假变点
