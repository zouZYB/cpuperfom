# 05 循环顺序与分块(Tiling):矩阵乘法的缓存优化

> 对应笔记:第八章-10(SPEC 内存层次特征化)、`images/roofline.svg`(内存墙 vs 计算墙)
> 本仓库 `examples/matmul-demo` 有进阶版本

## 目的

同一个矩阵乘法(浮点运算次数完全相同),对比三种循环结构:
**ijk(跨行访问,缓存灾难)→ ikj(连续访问)→ 分块 ikj(工作集裁剪)**。
同时展示一条重要经验:**优化是否有效取决于机器的缓存容量,必须实测**(本机 96MB 3D V-Cache 就是活教材)。

## 原理

```
C[i][j] += A[i][k] * B[k][j]   (行主序存储)

ijk 顺序(差):内层访问 B[k][j] 按列走,步长 = N×8B
  → 每次内层迭代都是一次 cache miss,B 的整行 64B 只用 8B

ikj 顺序(好):内层 B[k][j]、C[i][j] 都按行连续走
  → 空间局部性 + 预取器全速工作

分块(理论最好):把矩阵切成 BS×BS 小块,块内数据在 L1/L2 里反复复用
  → 但只有当"整个工作集 > 末级缓存"时才有收益
```

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"time"
)

const N = 1024 // 3 个矩阵共 24MB;1024³ ≈ 10 亿次乘加

func makeMat() []float64 {
	m := make([]float64, N*N)
	r := rand.New(rand.NewSource(3))
	for i := range m {
		m[i] = r.Float64()
	}
	return m
}

// 版本 1:ijk —— 内层跨行访问 B,缓存最不友好
func mulIJK(a, b, c []float64) {
	for i := 0; i < N; i++ {
		for j := 0; j < N; j++ {
			sum := 0.0
			for k := 0; k < N; k++ {
				sum += a[i*N+k] * b[k*N+j] // b 按列跳,步长 8KB
			}
			c[i*N+j] = sum
		}
	}
}

// 版本 2:ikj —— 内层全部连续访问
func mulIKJ(a, b, c []float64) {
	for i := 0; i < N; i++ {
		ci := c[i*N : i*N+N]
		ai := a[i*N : i*N+N]
		for k := 0; k < N; k++ {
			aik := ai[k]
			bk := b[k*N : k*N+N]
			for j := 0; j < N; j++ {
				ci[j] += aik * bk[j] // b、c 都按行连续
			}
		}
	}
}

// 版本 3:分块 ikj —— 块内工作集 < L1,复用最大化
func mulTiled(a, b, c []float64) {
	const BS = 32
	for ii := 0; ii < N; ii += BS {
		for kk := 0; kk < N; kk += BS {
			for jj := 0; jj < N; jj += BS {
				for i := ii; i < ii+BS; i++ {
					cRow := c[i*N+jj : i*N+jj+BS]
					for k := kk; k < kk+BS; k++ {
						aik := a[i*N+k]
						bRow := b[k*N+jj : k*N+jj+BS]
						for j := 0; j < BS; j++ {
							cRow[j] += aik * bRow[j]
						}
					}
				}
			}
		}
	}
}

func bench(name string, f func(a, b, c []float64), a, b, c []float64) float64 {
	for i := range c {
		c[i] = 0
	}
	start := time.Now()
	f(a, b, c)
	fmt.Printf("%-12s %v\n", name, time.Since(start))
	return c[12345]
}

func main() {
	a, b := makeMat(), makeMat()
	c := make([]float64, N*N)

	r1 := bench("ijk(缓存差)", mulIJK, a, b, c)
	r2 := bench("ikj(连续)", mulIKJ, a, b, c)
	r3 := bench("分块 ikj", mulTiled, a, b, c)

	ok := func(x, y float64) bool { d := x - y; return d < 1e-6 && d > -1e-6 }
	fmt.Println("\n结果一致(浮点容差内):", ok(r1, r2) && ok(r2, r3))
}
```

## 运行

```bash
go run main.go
# ijk 版本较慢,整体约 5 秒
```

## 预期结果(本仓库开发机实测,Ryzen 7 7800X3D,96MB L3)

```
ijk(缓存差) 3.58s
ikj(连续)   285.8ms   ← 快 12.5x
分块 ikj    483.1ms   ← 快 7.4x,但反而比 ikj 慢!

结果一致(浮点容差内): true
```

**重点解读**——为什么分块在这里"翻车":

- ijk → ikj 的 **12.5x** 是空间局部性的纯收益,任何机器都成立
- 分块的收益前提是"**工作集 > 末级缓存**";本机是 96MB 3D V-Cache,
  N=1024 的 24MB 工作集整个装进 L3,ikj 已经是极限,分块只剩额外的循环开销
- 把 N 加到 2048(96MB)依然如此(L3 刚好还装得下)——这就是
  [02-cache-levels-latency](02-cache-levels-latency.md) 里 64MB 仍未跳出 L3 台阶的同一颗 CPU
- 在 32MB L3 的普通 CPU 上、或 N ≥ 4096 时,分块才会反超 ikj;
  生产级 BLAS(OpenBLAS/MKL)在分块之上还有 packing、SIMD 微内核、多线程分治

**这本身就是第二章方法论的现场教学:优化没有普适结论,按目标机器实测。**

## 分析要点

- 三个版本乘加次数完全相同(10 亿次)——差距全部来自内存子系统,对应 Roofline 图中
  "从内存瓶颈区爬到计算瓶颈区"的过程
- ijk 的内层步长 = 1024×8B = 8KB:同时踩中"跨页 + 2 的幂步长"两个陷阱
- 分块大小经验起点:BS×BS×8B×3 ≈ L1 容量的 50~80%,再用实验微调
- 同类技术适用于一切"大数组多重循环":卷积、stencil、图遍历

## 常见误区

- ❌ 先上 SIMD/多线程再管缓存 —— 内存墙不解决,加宽计算单元只会让等待更贵
- ❌ 把"分块一定更快"当教条 —— 工作集装得下 L3 时,分块是纯开销(本例即证)
- ❌ 分块越大越好 —— 块超出 L1/L2 后收益反转为负,必须实测
