# 02 指针追逐:实测 L1/L2/L3/内存的延迟阶梯

> 对应笔记:第八章导言、`images/memory-hierarchy.svg`(寄存器到 DRAM 延迟差 100 倍)

## 目的

用**指针追逐(pointer chasing)**测出本机各级缓存的实际延迟和容量边界:
工作集超过某级缓存容量时,每次访问的耗时出现台阶式上升。

## 原理

```
把数组改造成一个"随机环":next[i] = 下一个要访问的下标。
访问序列 p = next[p] 严格串行(下一次地址依赖本次结果),
CPU 无法预取、无法并行——每次访问就是一次完整的"缓存/内存往返延迟"。

工作集  < L1  → 每步 ~1ns   (L1 命中)
工作集  > L1  → 每步 ~4ns   (L2 命中)
工作集  > L2  → 每步 ~15ns  (L3 命中)
工作集  > L3  → 每步 ~80ns+ (内存)
```

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"time"
)

var sink uint32

// 构造覆盖 n 个元素的随机单环(保证每个元素恰好被访问一次)
func buildCycle(n int) []uint32 {
	perm := make([]uint32, n)
	for i := range perm {
		perm[i] = uint32(i)
	}
	rand.New(rand.NewSource(1)).Shuffle(n, func(i, j int) {
		perm[i], perm[j] = perm[j], perm[i]
	})
	next := make([]uint32, n)
	for i := 0; i < n; i++ {
		next[perm[i]] = perm[(i+1)%n]
	}
	return next
}

func chaseLatency(next []uint32) float64 {
	n := len(next)
	const steps = 1 << 22 // 固定步数
	p := uint32(0)

	// 预热:沿环走一圈,让 TLB/缓存进入稳态
	for i := 0; i < n; i++ {
		p = next[p]
	}

	best := time.Hour
	for r := 0; r < 5; r++ {
		start := time.Now()
		for i := 0; i < steps; i++ {
			p = next[p]
		}
		if d := time.Since(start); d < best {
			best = d
		}
	}
	sink = p
	return float64(best.Nanoseconds()) / steps
}

func main() {
	fmt.Println("工作集大小   每步延迟(ns)")
	for kb := 4; kb <= 256*1024; kb *= 4 {
		n := kb * 1024 / 4 // uint32 元素数
		next := buildCycle(n)
		lat := chaseLatency(next)
		var size string
		if kb < 1024 {
			size = fmt.Sprintf("%d KB", kb)
		} else {
			size = fmt.Sprintf("%d MB", kb/1024)
		}
		fmt.Printf("%9s   %8.2f\n", size, lat)
	}
	fmt.Println("\n结论:延迟台阶出现的位置 = 各级缓存容量;典型台阶比 ≈ 1 : 3 : 10 : 60")
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测,数值随 CPU 型号变化,看台阶位置)

```
工作集大小   每步延迟(ns)
     4 KB       0.96
    16 KB       0.84       ← L1 平台期(≤32KB)
    64 KB       1.92
   256 KB       2.75       ← L2 平台期
     1 MB       5.50
     4 MB      10.88
    16 MB      13.51       ← L3 平台期(本机 L3 很大,直到 64MB+)
    64 MB      19.38
   256 MB      63.35       ← 落到内存(含 TLB miss)
```

## 分析要点

- 台阶位置直接告诉你本机 L1/L2/L3 容量,与 `wmic cpu get L2CacheSize,L3CacheSize` 或 uProf 报告对照验证
- 典型台阶比例 ≈ 1 : 3 : 10 : 60+(L1 : L2 : L3 : DRAM);本机 L3 很大,所以内存台阶出现得晚
- **工作集思维**:优化程序的本质之一是把热点数据压进更小的台阶——
  一个把 20MB 工作集压到 1MB 的重构,延迟直接从 L3 台阶掉到 L2 台阶
- 随机环同时污染了 TLB,大工作集的数字里混有页表行走开销,见 [08-tlb-walk-stride](08-tlb-walk-stride.md)
- 对应 TMA 分诊:这个延迟就是 Back-End Bound 中 `Memory Bound` 层的物理来源

## 常见误区

- ❌ 用顺序遍历测"缓存延迟" —— 硬件预取器会掩盖真实延迟,必须让地址依赖前一次结果
- ❌ 步数太少 —— Go 计时粒度有限,每步几纳秒时需要上千万步摊薄计时误差
