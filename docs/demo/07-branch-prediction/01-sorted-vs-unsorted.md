# 01 有序 vs 随机数据:分支预测的经典实验

> 对应笔记:第七章-4《Making Your Code Faster by Taming Branches》(Lemire, 2020)、七-1/七-2(预测器原理)
> 插图:`images/tage.svg`

## 目的

复现 Stack Overflow 上最著名的性能问题("为什么排序后的数组处理更快"):
**完全相同的代码,数据有序时快约 10 倍**。差异全部来自分支预测命中率。

## 原理

```
if (data[i] >= 128) sum += lut[data[i]];

数据随机 → 分支方向 50/50 不可预测 → TAGE 预测器也猜不中
         → 每次误预测清空流水线,罚 15~25 周期
数据有序 → 前一半全"否",后一半全"是" → 几乎 100% 猜中 → 零惩罚
```

> 实现细节:分支体内放一次**查表(内存读)**。Go 编译器对"纯算术的短 if"会生成
> 条件移动(cmov)消除分支,那样就测不到误预测了;含内存读的分支体无法被 cmov 化,
> 分支真实存在——这正是本实验要的效果。

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"sort"
	"time"
)

var sink int
var lut [256]int

const N = 1 << 22 // 4M 个元素

func sumIfGe128(data []uint8) int {
	sum := 0
	for _, v := range data {
		if v >= 128 { // 被测分支:分支体内含一次查表(内存读),编译器无法转成条件移动
			sum += lut[v]
		}
	}
	return sum
}

func bench(data []uint8) (time.Duration, int) {
	best := time.Hour
	result := 0
	for r := 0; r < 10; r++ {
		start := time.Now()
		result = sumIfGe128(data)
		if d := time.Since(start); d < best {
			best = d
		}
	}
	return best, result
}

func main() {
	for i := range lut {
		lut[i] = i
	}
	data := make([]uint8, N)
	rng := rand.New(rand.NewSource(42))
	for i := range data {
		data[i] = uint8(rng.Intn(256))
	}

	dRand, r1 := bench(data)
	fmt.Printf("随机数据: %v  (sum=%d)\n", dRand, r1)

	sort.Slice(data, func(i, j int) bool { return data[i] < data[j] })

	dSorted, r2 := bench(data)
	fmt.Printf("有序数据: %v  (sum=%d)\n", dSorted, r2)

	fmt.Printf("\n有序快 %.1fx —— 代码一行没改,只是数据变得可预测了\n",
		float64(dRand)/float64(dSorted))
	fmt.Println("(sum 相同证明两种情况下计算逻辑完全一致)")
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测)

```
随机数据: 10.0369ms  (sum=401484465)
有序数据: 1.0033ms  (sum=401484465)

有序快 10.0x —— 代码一行没改,只是数据变得可预测了
```

## 分析要点

- 误预测惩罚 = 流水线深度 ≈ 15~25 周期;4M 次迭代 × 50% 误预测 × 20 周期 ≈ 4000 万周期 ≈ 13ms@3GHz,
  量级与实测吻合
- TAGE 预测器(七-2)能学会**有模式**的历史(循环、交替),但对真随机数据无能为力——
  信息论上 50/50 就是不可压缩的
- 验证方法:用 uProf/perf 看 `branch-misses` 计数,随机版会高出几个数量级
- 工程启示:热点路径上,**数据的统计特性**和代码本身一样重要——
  "线上数据分布"变了,没改代码性能也会变(这也是回归检测难做的原因之一,见第三章)

## 常见误区

- ❌ "if 越少越快" —— 一个永远猜中的 if 接近免费;贵的是猜不中的 if
- ❌ 把分支优化用在冷路径 —— 只有占 CPU 时间显著份额的分支才值得处理(先用 pprof 确认)
- ❌ 用纯算术分支体测不到效果就说"分支预测不重要" —— 是编译器用 cmov 把分支消灭了,
  真实代码的分支体大多含内存访问或函数调用,无法被 cmov 化
