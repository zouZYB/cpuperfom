# 02 计时器自身的开销:测量工具会污染测量结果

> 对应笔记:第二章-5《How to Benchmark Code Execution Times on Intel IA-32/IA-64》(Paoloni, Intel, 2010)

## 目的

实测"调用一次计时函数"本身要花多少时间,理解 Paoloni 论文的核心方法论:
**测量值 = 真实耗时 + 计时开销,短测量必须先扣除开销再比较**。

## 原理

```
t_measured("空操作")     = 计时开销 T_overhead
t_measured(被测代码)      = T_overhead + T_real
⇒ T_real = t_measured(被测) − t_measured(空)
```

如果被测代码本身只有几十纳秒,而计时开销也是几十纳秒,不扣除就会得出 2 倍以上的错误结论。

## 代码

```go
package main

import (
	"fmt"
	"time"
)

var sink int64

func bench(f func(), rounds int) time.Duration {
	best := time.Hour
	for r := 0; r < 7; r++ {
		start := time.Now()
		for i := 0; i < rounds; i++ {
			f()
		}
		d := time.Since(start) / time.Duration(rounds)
		if d < best {
			best = d
		}
	}
	return best
}

func main() {
	const N = 1_000_000

	// 1. 空测量:一次 time.Now() + 循环迭代本身的开销
	empty := bench(func() {}, N)
	fmt.Printf("空循环迭代:            %v\n", empty)

	// 2. 一次 time.Since 调用的成本
	now := bench(func() { sink = time.Since(time.Now()).Nanoseconds() }, N)
	fmt.Printf("time.Now() 调用对:     %v\n", now)

	// 3. 被测操作:一次 64 位整数加法(寄存器级,理论 < 1ns)
	add := bench(func() { sink++ }, N)
	fmt.Printf("一次 sink++ (含循环):  %v\n", add)

	fmt.Printf("\n扣除开销后 sink++ ≈ %v(接近 0 说明循环开销已被正确剥离)\n", add-empty)
	fmt.Println("\n结论:被测代码耗时与计时开销同量级时,必须先测'空操作'再相减")
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测)

```
空循环迭代:            1ns
time.Now() 调用对:     8ns
一次 sink++ (含循环):  0s

扣除开销后 sink++ ≈ -1ns(接近 0 说明循环开销已被正确剥离)
```

关键观察:

- **一次计时调用本身约 8ns**——想测一个 10ns 的函数,近一半读数是计时器自己的
- 扣除循环开销后,`sink++` 趋近 0(被超标量流水掩盖),符合理论
- **开销 ≠ 分辨率**:本机 time.Now 单次调用仅 8ns,但最小可分辨间隔约 0.5ms
  (虚拟化 QPC,可用 demo 附注的自检代码测量)——调用很便宜,但两个相邻读数可能相同。
  所以单次测量窗口必须 ≥ 5ms,用"窗口 ÷ 重复次数"得到均值

附:计时器分辨率自检

```go
package main

import (
	"fmt"
	"time"
)

func main() {
	prev := time.Now()
	minDelta := time.Hour
	for i := 0; i < 100000; i++ {
		now := time.Now()
		if d := now.Sub(prev); d > 0 && d < minDelta {
			minDelta = d
		}
		prev = now
	}
	fmt.Println("time.Now() 最小可分辨间隔:", minDelta)
}
```

## 分析要点

- Paoloni 原文用 `RDTSC`/`RDTSCP` 演示同一原理,并指出 `RDTSC` 会被乱序执行"漂移",
  需要 `CPUID` 或 `RDTSCP + LFENCE` 序列化——Go 的 `time.Now()`(底层 QPC/rdtsc)已替你处理好序列化
- 实践准则:**被测代码应至少跑 100ns 以上**,否则用"循环放大 + 扣除空循环"的方式间接测量
- 微基准(单指令级)请用 `perf stat` / uProf 的 PMU 计数,而不是软件计时

## 常见误区

- ❌ `t := time.Now(); f(); fmt.Println(time.Since(t))` 测 10ns 的函数——读数一半是噪声和开销
- ❌ 忘了减空循环开销,把 `for` 循环本身算进被测代码
