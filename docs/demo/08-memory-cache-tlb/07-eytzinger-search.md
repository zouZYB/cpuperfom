# 07 Eytzinger 层序布局:让二分查找摆脱串行 cache miss

> 对应笔记:第八章-5《Array Layouts for Comparison-Based Searching》(Khuong & Morin, 2015)
> 插图:`images/eytzinger.svg`(串行 miss → 可预取的并行 miss)

## 目的

对同样的有序数据做同样的查找,仅改变**数组中的布局**:
经典二分(中间元素跳来跳去,每次都是不可预取的串行 cache miss)
vs Eytzinger 层序(节点 k 的孩子固定在 2k / 2k+1,后续层地址可提前算出,提前拉取)。

## 原理

```
经典二分:每一步的位置依赖上一步比较结果 → 地址依赖链 → 串行 cache miss
         26 步 × 内存延迟 ≈ 每次查找 400+ ns

Eytzinger:把二叉搜索树按"层序"存进数组(堆式下标:孩子 = 2k, 2k+1)
         → 前若干层挤在头几条缓存行(免费命中)
         → 下降途中,4 层后的候选位置(16k)已经可以算出来 → 提前读拉取
         → 多次 miss 并行进行,每步摊薄

类似 B+ 树思想:让每条缓存行装下尽可能多的决策信息
```

建布局的正确姿势:对**隐式完全二叉树**做中序遍历,依次填入有序值
(不要用"取中点递归"——非满树时下标会越界)。

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"time"
)

const (
	N = 1 << 26 // 64M 个有序 int64 = 512MB(远超 L3)
	Q = 1 << 21 // 200 万次查询
)

var sorted = make([]int64, N)
var eyt = make([]int64, N+1) // 1-based,0 号位闲置
var queries = make([]int64, Q)
var sink int64

// 经典二分(lower_bound:返回最小 ≥ x 的元素)
func binarySearch(x int64) int64 {
	lo, hi := 0, N-1
	for lo < hi {
		mid := int(uint(lo+hi) >> 1)
		if sorted[mid] < x {
			lo = mid + 1
		} else {
			hi = mid
		}
	}
	return sorted[lo]
}

// Eytzinger 建布局:对隐式完全二叉树做中序遍历,依次填入有序值
// 节点 k 的左右孩子在 2k / 2k+1
func buildEytzinger() {
	i := 0
	var walk func(k int)
	walk = func(k int) {
		if k > N {
			return
		}
		walk(2 * k)
		eyt[k] = sorted[i]
		i++
		walk(2*k + 1)
	}
	walk(1)
}

// Eytzinger 查找:无条件下降到底,再回溯到"最后一个向左"的祖先
func eytzingerSearch(x int64) int64 {
	k := 1
	for k <= N {
		if 16*k <= N {
			_ = eyt[16*k] // 模拟预取:提前 4 层把候选行拉进缓存
		}
		if eyt[k] < x {
			k = 2*k + 1
		} else {
			k = 2 * k
		}
	}
	// k 越界后:右移(二进制尾部 1 的个数 + 1)位 = 最近 ≥ x 的祖先
	for k&1 == 1 {
		k >>= 1
	}
	k >>= 1
	if k == 0 {
		return sorted[N-1] // x 大于所有元素
	}
	return eyt[k]
}

func bench(f func(int64) int64) time.Duration {
	best := time.Hour
	for r := 0; r < 3; r++ {
		start := time.Now()
		for _, q := range queries {
			sink = f(q)
		}
		if d := time.Since(start); d < best {
			best = d
		}
	}
	return best
}

func main() {
	for i := range sorted {
		sorted[i] = int64(i * 2)
	}
	rng := rand.New(rand.NewSource(9))
	for i := range queries {
		queries[i] = rng.Int63n(int64(2 * N))
	}
	buildEytzinger()

	// 正确性校验
	bad := 0
	for i := 0; i < 10000; i++ {
		if binarySearch(queries[i]) != eytzingerSearch(queries[i]) {
			bad++
		}
	}
	fmt.Println("校验不一致条数(应为 0):", bad)

	dBin := bench(binarySearch)
	fmt.Printf("经典二分:  %v  (%.0f ns/次)\n", dBin, float64(dBin.Nanoseconds())/Q)

	dEyt := bench(eytzingerSearch)
	fmt.Printf("Eytzinger: %v  (%.0f ns/次)\n", dEyt, float64(dEyt.Nanoseconds())/Q)

	fmt.Printf("\nEytzinger 快 %.2fx\n", float64(dBin)/float64(dEyt))
}
```

## 运行

```bash
go run main.go
# 需要约 1.5GB 空闲内存,运行约 1 分钟
```

## 预期结果(本仓库开发机实测)

```
校验不一致条数(应为 0): 0
经典二分:  923ms  (440 ns/次)
Eytzinger: 379ms  (181 ns/次)

Eytzinger 快 2.44x
```

## 分析要点

- 收益来源有二:① 层序让搜索路径前 log2(8)=3 层挤在前几条缓存行(免费命中);
  ② 深层节点地址(2k, 4k, 16k...)可提前计算,miss 可并行
- **规模敏感**:N 缩到 `1<<22`(32MB)时装进大 L3,差距缩到 ~1.45x;论文原数据
  (C++ + 硬件 prefetch 指令)在大数组上可达 2~4 倍——Go 没有显式 prefetch 指令,
  用"提前读一次"模拟,收益打折但方向一致
- 工程权衡:Eytzinger 适合**静态只读**数据(建一次,查百万次);频繁增删用 B+ 树
- 同款思想的应用:数据库索引页布局、编译器跳转表、检索码本

## 常见误区

- ❌ 用"取中点递归"建 Eytzinger 树 —— 非满二叉树下标会越界;正确做法是对隐式完全树中序填充
- ❌ 在能放进 L2/L3 的小数组上测 —— 没有 miss 就没有差距
- ❌ 忽略建树成本 —— 布局转换是 O(n) 一次性开销,查询量小不值得
