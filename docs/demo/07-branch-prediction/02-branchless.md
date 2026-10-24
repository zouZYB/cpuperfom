# 02 无分支化:消灭不可预测的分支

> 对应笔记:第七章-3《Avoiding the Cost of Branch Misprediction》(Intel, 2009)、七-4 Lemire
> 前置实验:[01-sorted-vs-unsorted](01-sorted-vs-unsorted.md)

## 目的

对上一用例的同一个问题(随机数据条件求和),不排序数据,而是**把分支改写为算术**,
让流水线无分支可猜,从根本上消除误预测惩罚。

## 原理

```
有分支:  if (v >= 128) sum += lut[v];    // 方向随机 → 50% 误预测

无分支:  bit = v >> 7;                    // uint8: v>=128 时 bit=1,否则 0(纯算术)
         sum += lut[v] * bit;             // 查表与乘法代替判断,无分支
```

代价是每次迭代**无条件**多做一次查表+乘法——但这些只要几周期,
而一次误预测罚 15~25 周期。**分支"可预测"时无分支版反而更贵;
分支"不可预测"时无分支版血赚**——必须按数据特性选方案。

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

const N = 1 << 22

// 版本 A:有分支(分支体内含查表,编译器无法转成条件移动)
func sumBranch(data []uint8) int {
	sum := 0
	for _, v := range data {
		if v >= 128 {
			sum += lut[v]
		}
	}
	return sum
}

// 版本 B:无分支(v>>7 给出 0/1,乘法代替判断;查表无条件执行)
func sumBranchless(data []uint8) int {
	sum := 0
	for _, v := range data {
		sum += lut[v] * int(v>>7)
	}
	return sum
}

func bench(f func([]uint8) int, data []uint8) (time.Duration, int) {
	best := time.Hour
	res := 0
	for r := 0; r < 10; r++ {
		start := time.Now()
		res = f(data)
		if d := time.Since(start); d < best {
			best = d
		}
	}
	return best, res
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

	run := func(label string) {
		dBr, r1 := bench(sumBranch, data)
		dBl, r2 := bench(sumBranchless, data)
		fmt.Printf("%s:  有分支=%v  无分支=%v  (倍数 %.1fx, sum一致=%v)\n",
			label, dBr, dBl, float64(dBr)/float64(dBl), r1 == r2)
	}

	run("随机数据")
	sort.Slice(data, func(i, j int) bool { return data[i] < data[j] })
	run("有序数据")

	fmt.Println("\n结论:分支不可预测时无分支化血赚;可预测时有分支反而略快(白做了一次乘法+查表)")
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测)

```
随机数据:  有分支=10.0261ms  无分支=1.427ms  (倍数 7.0x, sum一致=true)
有序数据:  有分支=999.8µs    无分支=1.5074ms (倍数 0.7x, sum一致=true)
```

两个版本结果**完全一致**(sum 相同),但适用场景相反:

| 数据特性 | 更快的版本 | 原因 |
|---------|-----------|------|
| 分支方向可预测(有序、有模式) | 有分支(1.0ms) | 猜测免费,还省了查表+乘法 |
| 分支方向随机 | 无分支(1.4ms vs 10ms) | 消除 15~25 周期/次的误预测 |

## 分析要点

- 编译器有时自动做这件事(把短 if 转成 `cmov` / 位运算),但**无法替你判断数据分布**——
  它保守时你就得手写;Go 编译器偏保守,手写无分支版本收益更稳
- 常见无分支手法:`x & mask` 选择、`min/max` 指令、查表替代条件链、`bool→int` 算术
- 决策框架(七-3 Intel 白皮书):先测 `branch-misses / branches` 比率,>10% 且该路径是热点,才值得无分支化
- 注意反面教材:对**永远猜得中**的分支做无分支化是负优化(见"有序数据"一行的 0.7x)

## 常见误区

- ❌ 无分支 = 无条件更快 —— 它用固定的小成本替换波动的大成本,只赢在后者的期望值更高时
- ❌ 在分支体内做重型操作还想着无分支化 —— 只有分支体是轻量算术/单查表时才划算
