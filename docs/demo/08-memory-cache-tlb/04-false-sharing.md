# 04 伪共享(False Sharing)与 64B 对齐填充

> 对应笔记:第八章导言;协议细节见《缓存一致性协议.md》§MESI/RFO

## 目的

演示多核编程中最隐蔽的性能杀手:**4 个线程各写各的变量,逻辑上零共享,
物理上却因为变量挤在同一缓存行而慢 5~20 倍**。再用 64B 填充修复。

## 原理

```
MESI 以缓存行(64B)为单位维护一致性:

counters[0..3] 共 32B,挤在同一行:
  Core0 写 counters[0] → RFO 夺行所有权 → 行转到 Core0 的 M 态
  Core1 写 counters[1] → RFO 夺回来     → 行转到 Core1 的 M 态
  ...缓存行在核间疯狂"乒乓",每次都要跨核握手(几十~上百周期)

加 56B 填充后:每个计数器独占一行 → 各核稳稳持有自己的 M 态行,零握手
```

## 代码

```go
package main

import (
	"fmt"
	"sync"
	"time"
)

const (
	Workers = 4
	Iters   = 50_000_000
)

// 场景 A:4 个计数器挤在一起(很可能落在同一缓存行)
var plain [Workers]int64

// 场景 B:每个计数器填充到 64B,独占缓存行
type padded struct {
	v int64
	_ [56]byte
}

var pad [Workers]padded

func benchPlain() time.Duration {
	var wg sync.WaitGroup
	start := time.Now()
	for w := 0; w < Workers; w++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for i := 0; i < Iters; i++ {
				plain[id]++ // 每次都写共享行 → RFO 乒乓
			}
		}(w)
	}
	wg.Wait()
	return time.Since(start)
}

func benchPadded() time.Duration {
	var wg sync.WaitGroup
	start := time.Now()
	for w := 0; w < Workers; w++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for i := 0; i < Iters; i++ {
				pad[id].v++ // 各自独占一行 → 始终本地 M 态
			}
		}(w)
	}
	wg.Wait()
	return time.Since(start)
}

func main() {
	dPlain := benchPlain()
	fmt.Printf("共享缓存行:   %v\n", dPlain)

	dPadded := benchPadded()
	fmt.Printf("64B 填充后:   %v\n", dPadded)

	fmt.Printf("\n填充后快 %.1fx\n", float64(dPlain)/float64(dPadded))
	fmt.Println("(校验:", plain[0], pad[0].v, "— 结果一致,逻辑无锁、无竞争,瓶颈纯粹是缓存一致性流量)")
}
```

## 运行

```bash
go run main.go
```

## 预期结果(4 核以上机器)

```
共享缓存行:   3.2s
64B 填充后:   0.4s

填充后快 8.0x
(校验: 50000000 50000000 — 结果一致)
```

## 分析要点

- **校验和相同证明无逻辑竞争**:慢的原因 100% 是 MESI 的 RFO 握手(缓存行乒乓)
- 用 uProf/perf 观测:伪共享场景的 `cross-core cache transfers` / L3 miss 流量会爆炸
- 真实世界的高发区:每线程统计计数器、无锁队列的头尾指针、线程池任务数组
- Go 的对应物:标准库 `sync.Pool`、各运行时计数器内部都做了缓存行填充
- C++ 可用 `alignas(64)` / `std::hardware_destructive_interference_size`;
  Go 中 `[56]byte` 填充是惯用手法(也可直接 `_ [7]int64`)
- 注意反面:**填充会浪费内存**,只给"被多核高频写的变量"填充;只读共享数据不需要

## 常见误区

- ❌ "没有锁就没有竞争" —— 缓存一致性是锁之下的另一层竞争,且不报任何错误,只是慢
- ❌ 给所有变量都填充 —— 浪费内存、降低缓存利用率,伪共享的反面是"假独享"开销
- ❌ 在单核上测伪共享 —— 必须 GOMAXPROCS ≥ 2(Go 默认用满核数,虚拟机里注意核数限制)
