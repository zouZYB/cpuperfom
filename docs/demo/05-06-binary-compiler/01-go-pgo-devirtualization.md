# 01 Go PGO:画像引导的二进制优化(接口调用反虚拟化)

> 对应笔记:第五章-2《AutoFDO》(Chen et al., 2016)、第六章-10(运行时优化蓝图)
> 这是"**不改一行代码,只靠重新编译二进制就提速 3 倍**"的完整演示。

## 目的

PGO(Profile-Guided Optimization)的核心思想:**先跑一次程序收集"画像"(哪条路热、
哪个调用目标是常客),再让编译器按画像重新优化二进制**。Go 1.20+ 内置 PGO,
本例展示它最强大的能力之一:**热接口调用的反虚拟化(devirtualization)**——
把每次迭代都要查虚表的间接调用,变成可内联的直接调用。

## 原理

```
无 PGO:  ops[i].Transform(x)
         → 查接口虚表 → 间接跳转 → 无法内联,每次 ~5ns

有 PGO:  画像告诉编译器"95%+ 的调用目标是 Double"
         → 生成: if ops[i] is Double { 内联 Double.Transform } else { 间接调用 }
         → 直接调用 + 内联 → 每次 ~1.5ns

这正是 AutoFDO 在数据中心做的事:生产环境 perf 采样 → 画像喂给编译器 → 白捡 5~15%
```

## 代码(main.go)

```go
package main

import (
	"flag"
	"fmt"
	"os"
	"runtime/pprof"
	"time"
)

// Transformer 是接口:循环里每次调用都是"间接调用"(查虚表),
// CPU 无法预知目标,也无法内联 —— 典型的二进制层面开销。
type Transformer interface{ Transform(int64) int64 }

type Double struct{}

func (Double) Transform(x int64) int64 { return x*3 + 1 }

var data = make([]int64, 1<<20)
var ops = make([]Transformer, 1<<20)
var sink int64

func run() int64 {
	var s int64
	for i := range data {
		s += ops[i].Transform(data[i]) // 每轮一次接口间接调用
	}
	return s
}

func main() {
	prof := flag.String("profile", "", "采集 CPU profile 到文件后退出")
	flag.Parse()

	for i := range data {
		data[i] = int64(i)
		ops[i] = Double{}
	}

	if *prof != "" {
		f, _ := os.Create(*prof)
		pprof.StartCPUProfile(f)
		deadline := time.Now().Add(3 * time.Second)
		for time.Now().Before(deadline) {
			sink = run()
		}
		pprof.StopCPUProfile()
		f.Close()
		fmt.Println("profile 已写入", *prof)
		return
	}

	run() // 预热
	best := time.Hour
	for r := 0; r < 7; r++ {
		start := time.Now()
		sink = run()
		if d := time.Since(start); d < best {
			best = d
		}
	}
	fmt.Printf("每次循环耗时: %v\n", best)
}
```

## 运行(完整 PGO 工作流)

```bash
# ① 编译普通二进制
go build -o nopgo.exe main.go

# ② 运行采集画像(生产环境对应:perf record / 持续 profiling)
./nopgo.exe -profile=cpu.pgo

# ③ 带画像重新编译(二进制优化发生在这里)
go build -pgo=cpu.pgo -o withpgo.exe main.go

# ④ 对比
./nopgo.exe       # 无 PGO
./withpgo.exe     # 有 PGO

# ⑤ 查看优化证据(编译日志直接打印反虚拟化)
go build -pgo=cpu.pgo -gcflags=-m -o nul main.go 2>&1 | findstr devirt
```

## 预期结果(本仓库开发机实测)

```
--- 无 PGO ---
每次循环耗时: 1.523ms
--- 有 PGO ---
每次循环耗时: 507.6µs          → 3.0x

--- 反虚拟化证据 ---
main.go:26:24: PGO devirtualizing interface call ops[i].Transform to Double.Transform
```

## 分析要点

- **优化发生在二进制层面**:源码一行没改,改变的是编译器掌握的"运行时真相"
- 画像≠源码:编译器看源码只知道"这是个接口调用";看画像才知道"95% 是 Double"——
  这与 AutoFDO 论文的核心主张一致:**采样画像足以驱动高质量优化,无需插桩**
- Go PGO 的其他收益:热路径内联预算提升、基本块重排(热代码聚到一起,I-cache 友好)——
  与第五章 HFSort/BOLT 的函数放置思想同源
- 生产用法:把 `default.pgo` 提交到仓库根目录,`go build` 自动使用;
  画像可以跨版本复用(Go 对过期画像有宽容处理)
- 微基准 3x 是理想值;真实程序典型收益 2~7%(Go 官方数据),数据中心 FDO 5~15%

## 常见误区

- ❌ 用开发机画像优化生产二进制 —— 画像必须来自**有代表性的负载**,否则优化错方向
- ❌ 画像越旧越好用 —— 代码大改后画像失准,应定期重新采集(持续 profiling 的意义)
- ❌ 以为 PGO 只对 C/C++ 有效 —— Go、Java(HotSpot 本质是运行时 PGO)、Rust 都有
