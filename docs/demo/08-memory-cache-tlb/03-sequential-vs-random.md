# 03 空间局部性:顺序访问 vs 随机访问

> 对应笔记:第八章导言;概念见《缓存一致性协议.md》"空间局部性"

## 目的

量化空间局部性的价值:**同样多的元素、同样的加法,顺序读比随机读快 5~15 倍**。
这是"数组优于链表""连续内存布局"等一切准则的物理基础。

## 原理

```
顺序访问:硬件预取器识别出线性模式,提前把后续缓存行取进 L1
        → 等 CPU 要用时数据已经在手边(几乎零等待)
随机访问:地址无法预测,预取器失效,每次都真实等待内存(~80ns)
        → 而且每行 64B 只用到 8B,带宽浪费 87%
```

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"time"
)

const N = 1 << 23 // 8M 个 int64 = 64MB,远超 L3,强制走内存

var data = make([]int64, N)
var order = make([]int32, N)
var sink int64

func sumSequential() int64 {
	var s int64
	for i := 0; i < N; i++ {
		s += data[i]
	}
	return s
}

func sumRandom() int64 {
	var s int64
	for i := 0; i < N; i++ {
		s += data[order[i]] // 打乱的下标
	}
	return s
}

func bench(f func() int64) (time.Duration, int64) {
	f() // 预热
	const reps = 10 // 单次计时窗口内的重复次数(应对粗粒度计时器)
	best := time.Hour
	var r int64
	for i := 0; i < 5; i++ {
		start := time.Now()
		for k := 0; k < reps; k++ {
			r = f()
		}
		if d := time.Since(start) / reps; d < best {
			best = d
		}
	}
	return best, r
}

func main() {
	rng := rand.New(rand.NewSource(7))
	for i := range data {
		data[i] = int64(i)
		order[i] = int32(i)
	}
	rng.Shuffle(N, func(i, j int) { order[i], order[j] = order[j], order[i] })

	dSeq, r1 := bench(sumSequential)
	fmt.Printf("顺序访问: %v  (sum=%d)\n", dSeq, r1)

	dRand, r2 := bench(sumRandom)
	fmt.Printf("随机访问: %v  (sum=%d)\n", dRand, r2)

	fmt.Printf("\n顺序快 %.1fx —— 元素个数、加法次数完全相同,差的只是访问顺序\n",
		float64(dRand)/float64(dSeq))
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测)

```
顺序访问: 2.26ms  (sum=35184367894528)
随机访问: 28.05ms (sum=35184367894528)

顺序快 12.4x —— 元素个数、加法次数完全相同,差的只是访问顺序
```

## 分析要点

- 顺序版有效带宽接近内存峰值(几十 GB/s);随机版每次访问等一次内存往返,带宽掉到 1/10
- 这就是为什么**数据结构选型**是性能的第一决定因素:
  链表/树/哈希表的指针跳转本质都是随机访问;B+ 树、开放寻址哈希、SoA 都是"向顺序访问靠拢"的设计
- 工程自查清单:热点循环里有没有指针跳转?能不能换成连续数组 + 下标?
- 配合 [02-cache-levels-latency](02-cache-levels-latency.md) 看:随机访问的每步延迟 ≈ 那里测出的内存台阶

## 常见误区

- ❌ "Big-O 一样,性能就一样" —— 两个 O(n) 算法因缓存行为可差 10 倍以上
- ❌ 数组太小导致随机版也全命中 L3 —— 本例用 64MB 强制落到内存
