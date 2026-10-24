# 01 指针追逐步进:实测缓存行大小(64B)

> 对应笔记:第八章导言(存储层次)、`images/memory-hierarchy.svg`;另见《缓存一致性协议.md》

## 目的

不查手册,用实验**测出**本机缓存行是 64 字节:沿指针链逐跳访问,
控制"每条缓存行内跳几跳",当每跳都落在新行上时,每跳耗时饱和——饱和点即缓存行大小。

## 原理

```
缓存以 64B 行为单位取数。用指针追逐(p=buf[p])制造**严格串行**的地址依赖,
让预取器和乱序执行都无法作弊(下一跳地址必须等本次结果):

行内步长 8B : 每行跳 8 跳 → 8 跳分摊 1 次 miss → 每跳 ≈ miss/8 + L1命中
行内步长 64B: 每跳都跨行   → 每跳 = 完整 miss 延迟
步长 > 64B  : 不再变化(每行仍只用一次)→ 曲线饱和
⇒ 饱和拐点 = 缓存行大小
```

为什么不用"顺序遍历 + 大步长"?实测硬件预取器会掩盖大步长顺序流的 miss,
乱序执行也会让独立 load 互相重叠(MLP),把差异抹平;**串行指针链是测缓存行/延迟的唯一可靠姿势**。

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"time"
)

const (
	Lines = 1 << 21 // 2M 条缓存行 × 64B = 128MB(远大于 L3)
	Elems = Lines * 8
)

var buf = make([]int64, Elems) // 原地存链:buf[i] = 下一个下标
var sink int64

// 构造指针链:缓存行按随机顺序跳转,行内按 stride 个元素步进
// stride=1: 行内 8 跳后才跨行(8 跳摊 1 次 miss)
// stride=8: 每跳都跨行(每跳 1 次 miss)
func buildChain(stride int) {
	perm := make([]int, Lines)
	for i := range perm {
		perm[i] = i
	}
	rand.New(rand.NewSource(1)).Shuffle(Lines, func(i, j int) { perm[i], perm[j] = perm[j], perm[i] })
	prev := -1
	first := -1
	for _, ln := range perm {
		for off := 0; off < 8; off += stride {
			cur := ln*8 + off
			if prev >= 0 {
				buf[prev] = int64(cur)
			} else {
				first = cur
			}
			prev = cur
		}
	}
	buf[prev] = int64(first) // 闭环
}

func chase(steps int) int64 {
	p := int64(0)
	for i := 0; i < steps; i++ {
		p = buf[p]
	}
	return p
}

func main() {
	for i := 0; i < Elems; i += 512 {
		buf[i] = 0 // 触摸每页,先分配物理页
	}
	fmt.Println("行内步长(B)  每行跳跃数  每跳耗时")
	for stride := 1; stride <= 16; stride *= 2 {
		buildChain(stride)
		hopsPerLine := (8 + stride - 1) / stride
		steps := Lines * hopsPerLine
		chase(steps / 4) // 预热
		start := time.Now()
		sink = chase(steps)
		d := time.Since(start)
		fmt.Printf("%6d      %6d     %7.2f ns\n",
			stride*8, hopsPerLine, float64(d.Nanoseconds())/float64(steps))
	}
	fmt.Println("\n每跳耗时随步长上升,在 64B 处饱和 → 缓存行 = 64B")
}
```

## 运行

```bash
go run main.go
# 需要约 200MB 空闲内存,运行约 1 分钟
```

## 预期结果(本仓库开发机实测)

```
行内步长(B)  每行跳跃数  每跳耗时
     8           8        8.25 ns
    16           4       14.90 ns
    32           2       27.75 ns
    64           1       49.76 ns   ← 达到满 miss 延迟
   128           1       52.00 ns   ← 饱和,不再增长
```

每跳耗时近似按 2 倍爬升(每行分摊的 miss 次数翻倍),**在步长 = 64B 处饱和** → 缓存行 64 字节。

## 分析要点

- 64B 与 Intel/AMD/ARM 主流 CPU 一致;`wmic cpu get` 查不到缓存行,这个实验是通用探测法
- 直接推论:遍历 `[]int64` 时前 8 个元素"搭第一个元素的便车";
  跨行跳跃访问的数据结构(大结构体、哈希表)浪费带宽
- 64B 同时是**伪共享**的最小间隔,见 [04-false-sharing](04-false-sharing.md)
- 饱和值 ~50ns ≈ 本机内存级延迟,可与 [02-cache-levels-latency](02-cache-levels-latency.md) 的内存台阶互相印证

## 常见误区

- ❌ 用顺序遍历测 —— 硬件预取器识别线性/定步长模式,会把大步长的 miss 也提前取好,曲线被抹平
- ❌ 用互相独立的随机下标测 —— 乱序执行让多个 load 并行在飞(MLP),延迟被重叠,差异缩水
- ❌ 数组太小(< L3)—— 全部命中缓存,看不到 miss 摊销效应;本例用 128MB 强制落到内存
