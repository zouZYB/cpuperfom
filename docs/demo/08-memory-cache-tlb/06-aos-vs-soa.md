# 06 数据布局:AoS(结构体数组)vs SoA(数组结构体)

> 对应笔记:0.3 术语表(AoS/SoA)、第六章-8(ISPC:SPMD 与向量化布局)

## 目的

同一个需求("对数百万个粒子求总动能"),对比两种内存布局:
**AoS 每读一个有用字段就要拖进整行垃圾数据;SoA 让每字节的内存带宽都花在有用数据上。**

## 原理

```
AoS: [{pos3,vel3,mass,id,meta×8}, ...]   每个粒子 128B(2 条缓存行)
  求总动能只需 vel3+mass = 32B → 带宽浪费 4 倍
  工作集 128B × 8M = 1GB,全部从内存搬运

SoA: velX[], velY[], velZ[], mass[] 各自成数组
  只搬运 32B × 8M = 256MB 有用数据
  四个数组各自连续 → 预取器全速 → 且天然适合 SIMD 打包
⇒ 理论差距 ≈ 128B/32B = 4x
```

## 代码

```go
package main

import (
	"fmt"
	"math/rand"
	"time"
)

const (
	N    = 1 << 23 // 8M 个粒子
	Reps = 3       // 单次计时窗口内重复次数(应对粗粒度计时器)
)

// 胖结构体:128B,本次计算只用 32B(vx,vy,vz,mass)
type Particle struct {
	px, py, pz float64 // 位置(本计算用不到)
	vx, vy, vz float64 // 速度
	mass       float64
	id         uint64
	m1, m2, m3, m4, m5, m6, m7, m8 float64 // 其他模块用的元数据
}

var aos = make([]Particle, N)

func kineticAoS() float64 {
	var e float64
	for i := range aos {
		p := &aos[i]
		e += 0.5 * p.mass * (p.vx*p.vx + p.vy*p.vy + p.vz*p.vz)
	}
	return e
}

type Particles struct {
	vx, vy, vz, mass []float64
	_                [4]int // pos/id 数组省略,模拟"用不到的字段"
}

var soa = Particles{
	vx: make([]float64, N), vy: make([]float64, N),
	vz: make([]float64, N), mass: make([]float64, N),
}

func kineticSoA() float64 {
	var e float64
	vx, vy, vz, m := soa.vx, soa.vy, soa.vz, soa.mass
	for i := 0; i < N; i++ {
		e += 0.5 * m[i] * (vx[i]*vx[i] + vy[i]*vy[i] + vz[i]*vz[i])
	}
	return e
}

func bench(f func() float64) (time.Duration, float64) {
	f()
	best := time.Hour
	var r float64
	for i := 0; i < 5; i++ {
		start := time.Now()
		for k := 0; k < Reps; k++ {
			r = f()
		}
		if d := time.Since(start) / Reps; d < best {
			best = d
		}
	}
	return best, r
}

func main() {
	rng := rand.New(rand.NewSource(5))
	for i := 0; i < N; i++ {
		aos[i] = Particle{
			vx: rng.Float64(), vy: rng.Float64(), vz: rng.Float64(),
			mass: rng.Float64(),
		}
		soa.vx[i], soa.vy[i], soa.vz[i], soa.mass[i] =
			aos[i].vx, aos[i].vy, aos[i].vz, aos[i].mass
	}

	dAos, r1 := bench(kineticAoS)
	dSoa, r2 := bench(kineticSoA)

	fmt.Printf("AoS: %v  (E=%.3f)\n", dAos, r1)
	fmt.Printf("SoA: %v  (E=%.3f)\n", dSoa, r2)
	fmt.Printf("\nSoA 快 %.1fx —— 计算量完全相同,差的只是搬运的字节是否物尽其用\n",
		float64(dAos)/float64(dSoa))
}
```

## 运行

```bash
go run main.go
# 需要约 1.5GB 空闲内存
```

## 预期结果(本仓库开发机实测)

```
AoS: 26.59ms  (E=2098041.094)
SoA: 6.86ms   (E=2098041.094)

SoA 快 3.9x —— 计算量完全相同,差的只是搬运的字节是否物尽其用
```

与理论值(128B/32B = 4x)吻合。

## 分析要点

- **结构体越胖、用到的字段越少,AoS 浪费越大**;把 meta 字段从 8 个减到 0(64B 结构体),
  差距就从 3.9x 降到 ~1.2x——可以自己改代码验证
- **工作集必须超过 L3** 才能看到差距:把 N 改成 `1<<20`(AoS 共 128MB 或 64MB),
  两者都装进大 L3 后差距消失(实测 1.0x)——内存带宽差异只在真正走内存时存在
- SoA 是 **SIMD 向量化的前提**:编译器/ISPC 需要连续同构数据才能打包成 AVX 指令(六-8)
- AoS 并非一无是处:当访问模式是"一次用到对象的所有字段"(如按 id 取完整粒子),
  AoS 两行搞定,SoA 反而要 4 次分散访问——**布局跟随访问模式**
- 工程实践:热循环统计型计算 → SoA;OLTP 式整对象存取 → AoS;很多系统两者各存一份

## 常见误区

- ❌ 教条地"SoA 总是快" —— 单点整对象访问时 SoA 是负优化;流多于 4~8 条时
  预取器跟踪能力和 DRAM 行缓冲 locality 也会打折扣
- ❌ 小数据量测不出差距就下结论 —— 全在缓存里时,带宽差异根本不存在
