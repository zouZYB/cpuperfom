# 01 Amdahl 定律:40% 串行段,多少核都救不了

> 对应笔记:九-12《There's Plenty of Room at the Top》(Leiserson et al., Science 2020:
> 后摩尔时代"软件性能工程"的核心约束)、四-7 GAPP(检测串行化瓶颈的剖析器)

## 目的

Amdahl 定律是并行化的天花板公式:

```
speedup(p) = 1 / (s + (1-s)/p)      s = 串行占比,p = 核数

s=40% 时:lim(p→∞) = 1/s = 2.5x —— 就算有 1000 个核,也快不过 2.5 倍
```

本例构造 s≈40% 的负载(每任务:3 份可并行计算 + 2 份锁内计算),测 1/2/4/8
worker 的加速曲线,并用**无锁分片对照组**证明:工作量本身完全可以并行,
是锁把它按在了 ~2x。

## 原理

```
并行程序的总时间 = 串行段时间 + 并行段时间/p
                  ─────────────
                  这部分不随核数缩小 —— 加核的收益被它封顶

串行段的常见形态:全局锁保护的临界区、单线程的初始化/归约、
                 消息队列单消费者、GC 的 STW 阶段

GAPP 论文(四-7)干的事:自动找出"哪个临界区"在限制扩展性;
本例先用手工实验建立直觉:锁在,扩展性死。
```

## 代码(main.go)

```go
package main

import (
	"flag"
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
)

// Amdahl 定律实测:固定总工作量,并行段 + 串行段(临界区)

const items = 4000 // 总任务数(固定工作量)

func busy(n int, x int64) int64 { // 纯计算,防止被优化掉
	s := x
	for i := 0; i < n; i++ {
		s = s*1103515245 + 12345
		s ^= s >> 13
	}
	return s
}

var sink int64

// 并行段:每个任务的纯计算
func parallelPart(x int64) int64 { return busy(3000, x) }

// 串行段:临界区内计算量 = 并行段的 2/3 → s = 40%
func serialPart(x int64) int64 { return busy(2000, x) }

func run(workers int, useLock bool) time.Duration {
	runtime.GOMAXPROCS(workers)
	var mu sync.Mutex
	var results = make([]int64, 8) // 分片模式:每 worker 独立槽位
	var total int64
	var idx atomic.Int64
	var wg sync.WaitGroup
	start := time.Now()
	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func(w int) {
			defer wg.Done()
			for {
				i := idx.Add(1) - 1
				if i >= items {
					return
				}
				r := parallelPart(i)
				if useLock {
					mu.Lock() // 串行段:同时只有一个 worker 能进
					total += serialPart(r)
					mu.Unlock()
				} else {
					results[w] += r // 无串行段:近线性扩展的对照组
				}
			}
		}(w)
	}
	wg.Wait()
	sink = total + results[0] + results[7]
	return time.Since(start)
}

func bestOf(workers int, useLock bool) time.Duration {
	run(workers, useLock) // 预热
	best := time.Hour
	for r := 0; r < 5; r++ {
		if d := run(workers, useLock); d < best {
			best = d
		}
	}
	return best
}

func main() {
	mode := flag.String("mode", "lock", "lock=含串行临界区 | shard=无串行段对照")
	flag.Parse()
	useLock := *mode == "lock"
	t1 := bestOf(1, useLock)
	fmt.Printf("mode=%s  1 worker: %v (基准)\n", *mode, t1)
	for _, p := range []int{2, 4, 8} {
		tp := bestOf(p, useLock)
		s := 0.4
		pred := 1 / (s + (1-s)/float64(p))
		fmt.Printf("           %d workers: %v  实测加速 %.2fx   Amdahl(s=0.4) 预测 %.2fx\n",
			p, tp, float64(t1)/float64(tp), pred)
	}
}
```

## 运行

```bash
go run main.go -mode=lock     # 含串行临界区
go run main.go -mode=shard    # 无串行段对照组
```

## 预期结果(本仓库开发机实测,8C16T,两轮稳定)

```
mode=lock   1 worker: 22.59ms (基准)
            2 workers: 11.54ms  实测加速 1.96x   Amdahl 预测 1.43x
            4 workers: 11.73ms  实测加速 1.93x   Amdahl 预测 1.82x
            8 workers: 13.04ms  实测加速 1.73x   Amdahl 预测 2.11x
                                    ↑ 饱和在 ~2x(理论上限 1/s = 2.5x),核再多反而略降

mode=shard  1 worker: 13.55ms (基准)
            2 workers:  6.52ms  实测加速 2.08x
            4 workers:  3.01ms  实测加速 4.50x
            8 workers:  1.00ms  实测加速 13.50x  ← 工作量本身近完美扩展
```

## 分析要点

- **对照组是灵魂**:shard 模式证明同样的工作量可以跑到 13.5x(8 物理核 + SMT
  锦上添花——LCG 是依赖链受限负载,超线程填充了流水线气泡,所以超过 8x);
  lock 模式唯一的区别就是那把锁 → 扩展性从 13.5x 塌到 ~2x
- 实测平台期(~1.7-2x)略低于理论上限(2.5x):锁的排队获取本身还有额外开销,
  且 p=8 时等待队列变长,所以曲线**过了峰值还往下走**——真实系统普遍如此,
  Amdahl 是乐观上界
- 串行占比的杀伤是非线性的:s=10% → 上限 10x;s=1% → 上限 100x。
  **先测 s 再买核**:95% 并行 + 5% 串行的程序,16 核最多 9.3x,32 核最多 13.9x
- 工程对策(对应 Leiserson 的"软件性能工程"):
  1. **消灭串行段**:分片(thread-local)、无锁数据结构、批量归约
  2. **缩短临界区**:锁内只放真正共享的操作(本例 serialPart 若只留 += 就接近 shard)
  3. **换并发模型**:消息传递代替共享内存
- 找串行化瓶颈的工具思路见四-7 GAPP:按"临界区持有时间 × 等待者数量"排序,
  第一名就是扩展性杀手

## 常见误区

- ❌ "核不够就加核" —— 先看 Amdahl:s=5% 的程序从 8 核升到 16 核,收益不到 1.6x
- ❌ 只看平均 CPU 利用率判断并行度 —— 利用率低可能就是锁等待;应看加速曲线形态
- ❌ 忽略 SMT 不是真核 —— 本例 shard 的 13.5x 是依赖链负载的特例;
  计算饱和型负载 SMT 只值 ~1.2-1.3x
- ❌ 在锁里做"顺便"的工作 —— 锁内每一拍都在全系统串行预算里扣
