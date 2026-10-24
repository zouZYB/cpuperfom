# 01 采样剖析:用 pprof 定位热点函数

> 对应笔记:第四章-1(TMA 方法论)、四-2(PMU 采样开销)、四-3(Google-Wide Profiling)

## 目的

体验"采样剖析"的基本思想(笔记 0.2-⑧):**不插桩、不逐行统计,而是每秒约 100 次打断程序,
记录当前正在执行的函数,统计出现频率找热点**。Go 内置的 `runtime/pprof` 就是一个采样剖析器。

## 原理

```
每 10ms(100Hz)硬件定时器打断程序一次 → 记录当前调用栈 → 样本数 ∝ 该函数耗时占比
```

- 开销仅 1~5%,所以 Google 敢在**生产环境 7×24 常开**(四-3)
- 代价是不精确:样本是统计估计,且存在 **skid**(采样记录的位置与真实事发指令差几条)

## 代码

```go
package main

import (
	"fmt"
	"math"
	"os"
	"runtime/pprof"
	"time"
)

var sink float64

// 故意制造的热点 A:数学运算密集
func computeA(n int) {
	for i := 0; i < n; i++ {
		sink += math.Sqrt(float64(i%1000 + 1))
	}
}

// 故意制造的热点 B:比 A 更重的三角函数
func computeB(n int) {
	for i := 0; i < n; i++ {
		sink += math.Sin(float64(i%360)) * math.Cos(float64(i%180))
	}
}

// 几乎不占时间的"绿叶"函数
func glue(n int) {
	for i := 0; i < n; i++ {
		sink += float64(i % 3)
	}
}

func main() {
	f, err := os.Create("cpu.pprof")
	if err != nil {
		panic(err)
	}
	defer f.Close()

	if err := pprof.StartCPUProfile(f); err != nil {
		panic(err)
	}

	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		computeA(500000)  // 预期占比 ~30%
		computeB(500000)  // 预期占比 ~65%
		glue(100000)      // 预期占比 ~5%
	}

	pprof.StopCPUProfile()
	fmt.Println("已写出 cpu.pprof,执行: go tool pprof -top cpu.pprof")
}
```

## 运行

```bash
go run main.go
go tool pprof -top cpu.pprof
# 交互模式: go tool pprof cpu.pprof  → 输入 top / list computeB / web
```

## 预期结果(本仓库开发机实测)

```
      flat  flat%   sum%        cum   cum%
     2.15s 43.26% 43.26%      2.25s 45.27%  math.cos
     0.93s 18.71% 61.97%      0.94s 18.91%  main.computeA (inline)
     0.90s 18.11% 80.08%      0.93s 18.71%  math.sin
     0.62s 12.47% 92.56%      3.82s 76.86%  main.computeB
     0.19s  3.82% 96.38%      0.19s  3.82%  main.glue (inline)
```

观察要点:**flat 榜首是 `math.cos` 而不是我们的函数**——`computeB` 自己的时间几乎都花
在调用 sin/cos 里。看 cum 列才知道 `computeB` 占 76.86%,是最大热点;这正是 flat/cum
分工的典型场景——不需要任何代码插桩,采样 5 秒即得。

## 分析要点

- **flat vs cum**:flat 是函数自身耗时,cum 含调用树下游;看热点先看 flat,看调用链看 cum
- 采样频率 100Hz 意味着 <10ms 的函数可能采样不到——微基准别靠采样,靠 PMU 计数(本仓库 uProf 集成)
- 采样只告诉你"**哪里**慢",不告诉你"**为什么**慢"——下一步该用 TMA(四-1)分诊:
  是前端取指堵?后端等内存?还是分支误预测?(可用 `08-memory-cache-tlb` 各用例逐一验证假设)
- 对照四-2:采样间隔越密,开销越大;生产环境取 100Hz 左右是开销/精度的平衡点

## 常见误区

- ❌ 看 cum% 最大的 `main.main` 说"main 是热点" —— 那是调用树根部,要看 flat
- ❌ 采样 1 秒就下结论 —— 样本太少统计误差大,至少跑 5~30 秒
- ❌ 把 runtime 开销(GC、调度)当成业务代码慢 —— pprof 里 `runtime.*` 条目要单独归因
