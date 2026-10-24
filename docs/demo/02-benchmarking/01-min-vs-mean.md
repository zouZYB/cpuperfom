# 01 噪声环境下:取最小值 vs 取均值

> 对应笔记:第二章-2《"Robust Benchmarking in Noisy Environments"(Chen & Revels, 2016)》
> 插图:`images/measurement-noise.svg`

## 目的

证明论文的核心结论:**测量噪声几乎只会"让程序变慢",不会让程序变快**(中断、缓存被挤、频率下降都是加时间)。
因此在噪声环境下,多次测量的**最小值**比均值/中位数更接近"真实耗时"。

## 原理

```
真实耗时 t0(恒定)
实测值  = t0 + 噪声(≥ 0,长尾分布)
均值    = t0 + 平均噪声   ← 噪声越大越偏离
最小值  ≈ t0 + 最小噪声   ← 最幸运的一次,最接近真相
```

> 实现细节:本机(虚拟机)time.Now 粒度约 0.5ms(见 [02-timer-overhead](02-timer-overhead.md)),
> 单次测量会被量化,所以一个"样本"= 连续 50 次负载的计时窗口取平均。

## 代码

```go
package main

import (
	"fmt"
	"sort"
	"sync/atomic"
	"time"
)

var sink int64
var noiseFlag int32

// 固定工作负载
func workload() {
	var s int64
	for i := int64(0); i < 100000; i++ {
		s += (i * i) % 97
	}
	sink = s
}

// 背景噪声:不断分配内存,触发 GC 并污染缓存
func noiseWorker(stop <-chan struct{}) {
	for {
		select {
		case <-stop:
			return
		default:
			if atomic.LoadInt32(&noiseFlag) == 1 {
				garbage := make([]byte, 1<<20)
				garbage[0] = 1
				sink += int64(garbage[0])
			} else {
				time.Sleep(10 * time.Millisecond) // 安静期:不占用核
			}
		}
	}
}

// 一个"样本" = 连续 perWin 次负载的计时窗口取平均
func measure(rounds int) (min, median, mean time.Duration) {
	const perWin = 50
	samples := make([]time.Duration, rounds)
	for i := 0; i < rounds; i++ {
		start := time.Now()
		for k := 0; k < perWin; k++ {
			workload()
		}
		samples[i] = time.Since(start) / perWin
	}
	sort.Slice(samples, func(i, j int) bool { return samples[i] < samples[j] })
	var total time.Duration
	for _, s := range samples {
		total += s
	}
	return samples[0], samples[rounds/2], total / time.Duration(rounds)
}

func main() {
	stop := make(chan struct{})
	defer close(stop)
	go noiseWorker(stop)

	measure(50) // 预热

	fmt.Println("=== 安静环境 ===")
	atomic.StoreInt32(&noiseFlag, 0)
	time.Sleep(100 * time.Millisecond)
	min, median, mean := measure(200)
	fmt.Printf("min=%v  median=%v  mean=%v\n", min, median, mean)

	fmt.Println("=== 噪声环境(后台 1MB 分配) ===")
	atomic.StoreInt32(&noiseFlag, 1)
	time.Sleep(100 * time.Millisecond)
	min2, median2, mean2 := measure(200)
	fmt.Printf("min=%v  median=%v  mean=%v\n", min2, median2, mean2)

	fmt.Printf("\n噪声造成的漂移: min %.2fx | median %.2fx | mean %.2fx\n",
		float64(min2)/float64(min), float64(median2)/float64(median), float64(mean2)/float64(mean))
	fmt.Println("结论:min 漂移最小 → 对比两个版本快慢时应取最小值")
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测)

```
=== 安静环境 ===
min=40.1µs  median=60.2µs  mean=62.1µs
=== 噪声环境(后台 1MB 分配) ===
min=60.7µs  median=144.0µs  mean=124.7µs

噪声造成的漂移: min 1.51x | median 2.39x | mean 2.01x
结论:min 漂移最小 → 对比两个版本快慢时应取最小值
```

关键观察:噪声来袭时,**median 漂了 2.4 倍、mean 漂了 2 倍,而 min 只漂了 1.5 倍**——
最小值永远是最稳的那个统计量。

## 分析要点

- 噪声(中断、GC、其他进程抢核、缓存被污染)只会**增加**耗时,所以样本分布是右偏长尾
- 最小值 ≈ "最幸运的一次测量",噪声最少,最接近真实值
- 这也是本 demo 集所有用例内部统一 `取最小值` 的原因
- 注意例外:如果要做容量规划(关心平均体验),中位数/P99 才有意义;最小值只用于**对比两个版本的快慢**
- 在物理机(非 VM)上,min 的漂移通常能压到 ±5% 以内,效果比本机更明显

## 常见误区

- ❌ "多跑几次取平均更科学" —— 对噪声长尾分布,平均值被最差样本绑架
- ❌ 只看一次运行的结果就下结论 —— 预热、频率爬升都会污染首轮数据
- ❌ 单次测量小于计时器粒度 —— 样本会变成 0 或固定量子,本例用"窗口 ÷ 次数"规避
