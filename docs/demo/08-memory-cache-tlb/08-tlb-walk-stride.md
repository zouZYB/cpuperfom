# 08 随机页序指针追逐:观测 TLB Miss 的代价

> 对应笔记:第八章-6《Improving TLB Performance by Increasing Hugepage Ratio》(Luo et al., 2015)

## 目的

缓存之下还有一层常被忽视的缓存:**TLB(虚拟→物理地址翻译缓存)**。
用"每页一个节点、随机页序"的指针链,测出当页数超过 TLB 容量后,
每次访问要多付出多少**页表行走(page walk)**时间。

## 原理

```
内存按 4KB 页管理;TLB 只缓存约 1536~2048 个页的翻译。

页数 < TLB 容量  → 翻译全命中,每跳 ≈ 纯缓存/内存延迟
页数 > TLB 容量  → 每跳落在随机新页 → TLB miss
                 → 硬件页表行走(最多 4 级页表)→ 额外 +20~60ns

为什么用指针链:顺序步进会被步长预取器和页表预取掩盖(实测顺序流每页仅 ~4ns),
随机页序 + 串行依赖让每次 TLB miss 都暴露在关键路径上。

大页(HugePage 2MB)思路:一页顶 512 个 4KB 页,同样 TLB 容量覆盖范围 ×512 → miss 率骤降
```

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"time"
)

var sink int64

// 在区域内以随机页序构造指针链(每页一个节点):buf[节点] = 下一节点下标
func buildPageChain(buf []int64, pages int, seed int64) {
	perm := make([]int, pages)
	for i := range perm {
		perm[i] = i
	}
	rand.New(rand.NewSource(seed)).Shuffle(pages, func(i, j int) { perm[i], perm[j] = perm[j], perm[i] })
	for i := 0; i < pages; i++ {
		cur := perm[i] * 512 // 每页 4096B = 512 个 int64
		nxt := perm[(i+1)%pages] * 512
		buf[cur] = int64(nxt)
	}
}

func chase(buf []int64, steps int) int64 {
	p := int64(0)
	for i := 0; i < steps; i++ {
		p = buf[p]
	}
	return p
}

func bench(buf []int64, pages int) float64 {
	buildPageChain(buf, pages, 7)
	const steps = 1 << 22 // 固定 4M 步,保证计时窗口 >> 计时器粒度
	chase(buf, pages)     // 预热
	best := time.Hour
	for r := 0; r < 3; r++ {
		start := time.Now()
		sink = chase(buf, steps)
		if d := time.Since(start); d < best {
			best = d
		}
	}
	return float64(best.Nanoseconds()) / steps
}

func main() {
	fmt.Println("区域大小    页数      每跳耗时(ns)  说明")
	for _, mb := range []int{4, 32, 256, 1024} {
		size := mb << 20
		pages := size / 4096
		buf := make([]int64, size/8)
		lat := bench(buf, pages)
		note := ""
		switch mb {
		case 4:
			note = "页数 < TLB 容量,且区域装进 L2/L3 → 全命中"
		case 32:
			note = "页数超 TLB,数据仍在缓存 → 露出 TLB miss 增量"
		case 1024:
			note = "TLB miss + 内存延迟叠加"
		}
		fmt.Printf("%5d MB  %8d   %8.2f    %s\n", mb, pages, lat, note)
	}
	fmt.Println("\n结论:页数超过 TLB 容量后,每跳多付出 10~40ns 的页表行走代价")
}
```

## 运行

```bash
go run main.go
# 需要约 1.5GB 空闲内存
```

## 预期结果(本仓库开发机实测,Ryzen 7 7800X3D)

```
区域大小    页数      每跳耗时(ns)  说明
  4 MB      1024        4.93    页数 < TLB 容量,且区域装进 L2/L3 → 全命中
 32 MB      8192       16.14    页数超 TLB,数据仍在缓存 → 露出 TLB miss 增量
256 MB     65536       17.71
1024 MB   262144       50.59    TLB miss + 内存延迟叠加
```

读法:

- 4MB→32MB:数据都还在缓存层级里,但每跳从 4.9ns 涨到 16.1ns——**+11ns 就是 TLB miss 的页表行走**
  (页表项本身在缓存里时便宜)
- 1GB:页表行走可能要走内存,加上数据本身也在内存,叠加到 50.6ns

## 分析要点

- TLB miss 代价 = 页表行走(最多 4 级,每级一次潜在 cache miss),实测 10~60ns,
  **是"隐藏的内存延迟"**——perf/uProf 里看 `dTLB-load-misses`,TMA 体系计入 Memory Bound
- 受害典型:大堆内存上的随机访问(大哈希表、图计算、JVM 大堆)——
  正是第八章-6 论文的场景:提高大页比例后,数据中心应用白捡 5~10%
- Windows 验证大页:`GetLargePageMinimum()`;Linux:`/proc/meminfo` 的 `HugePages_*`;
  Go 在 Linux 可用 `madvise(MADV_HUGEPAGE)` 或 THP
- 与大页的对照实验:同样的 1GB 区域改用 2MB 大页后只需 512 页,TLB 轻松装下,
  每跳会从 50ns 落回纯内存延迟(~40ns)——Linux 上可直接验证

## 常见误区

- ❌ 只看 cache-miss 忽略 dTLB-miss —— 大内存随机访问场景两者同样致命
- ❌ 用顺序步长流测 TLB —— 步长预取器 + 页表预取会把 miss 全部掩盖(实测每页仅 ~4ns)
- ❌ 以为分配了内存就分配了物理页 —— 首次触摸才有缺页中断
