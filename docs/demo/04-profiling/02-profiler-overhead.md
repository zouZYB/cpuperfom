# 02 剖析器自身的开销:计数 ≈ 免费,采样看频率

> 对应笔记:四-2《The Overhead of Profiling Using PMU Hardware Counters》
> (Nowak & Bitzes, 2014)、四-3 Google-Wide Profiling(持续剖析为什么可行)

## 目的

"开剖析会不会把程序拖慢?"分两种情况:

```
PMU 计数(只读计数器,不打断程序):开销 ≈ 0 —— 硬件替你数
采样剖析(定时中断,抓栈):开销 ∝ 采样频率
  100Hz(默认)    → 远低于 1%,生产可常开(四-3 的立足点)
  10kHz+         → 百分之几,只在深挖时临时用
```

本例实测三档:AMD uProf PCM 计数、Go pprof 100Hz / 10kHz / 100kHz。

## 实验一:PMU 计数开销(uProf PCM)

```bash
# 直接跑
./simd.exe
# 在 PCM 整机采集下跑
AMDuProfPcm.exe -m ipc -C -A system -q -a -d 20 -o pcm.csv -- ./simd.exe
```

**本机实测**(scalar 一路,见 [05-06-05](../05-06-binary-compiler/05-simd-avx2.md)):

```
无采集   : 96.73 / 97.15 ms
PCM 采集 : 96.57 ms          → 差异 < 0.2%,淹没在噪声里
```

## 实验二:采样频率开销(Go pprof)

```go
package main

import (
	"flag"
	"fmt"
	"os"
	"runtime"
	"runtime/pprof"
	"time"
)

var sink int64

func work() int64 { // 纯计算,依赖链受限
	var s int64
	for i := int64(0); i < 200000000; i++ {
		s = s*1103515245 + 12345
		s ^= s >> 13
	}
	return s
}

func main() {
	rate := flag.Int("rate", 0, "采样率 Hz,0=不采集")
	flag.Parse()
	if *rate > 0 {
		f, _ := os.Create("cpu.pprof")
		runtime.SetCPUProfileRate(*rate)
		pprof.StartCPUProfile(f)
		defer func() { pprof.StopCPUProfile(); f.Close() }()
	}
	best := time.Hour
	for r := 0; r < 5; r++ {
		start := time.Now()
		sink = work()
		if d := time.Since(start); d < best {
			best = d
		}
	}
	fmt.Printf("%v\n", best)
}
```

```bash
go build -o profov.exe main.go
./profov.exe -rate=0        # 不采集
./profov.exe -rate=100      # Go 默认
./profov.exe -rate=10000
./profov.exe -rate=100000
```

**本机实测**:

```
不采集   : 238.14 ms
100Hz    : 238.70 ms   (+0.2%)
10kHz    : 242.72 ms   (+1.9%)
100kHz   : 243.37 ms   (+2.2%)
```

## 分析要点

- **数量级结论**:100Hz 采样 ≈ 每次中断 ~10µs × 100 次/秒 ≈ 0.1%——
  这就是 Google 敢在全数据中心**常开**剖析的原因(四-3);
  采样率提高 100 倍,开销只涨到 ~2%(中断处理有固定路径,部分被流水线吸收)
- 本机 100kHz 只 +2.2% 的一个原因:**虚拟化环境的 OS 定时器粒度 ~0.5ms**
  (见 [02-02](../02-benchmarking/02-timer-overhead.md)),实际有效采样率
  被限制在 ~2kHz 以下——高采样率在裸机上开销更大,但也绝不是线性增长
- Nowak & Bitzes 论文的完整结论还包括:读计数器本身(rdpmc/ioctl)有成本,
  高频轮询计数器(如每 100µs 读一次)才会产生可观开销——所以 PCM 用
  秒级间隔(`-d` 参数)不是偷懒,是设计
- 实践推论:
  1. 生产常开 100Hz 级采样 + 秒级计数器采集,放心
  2. 需要精细事件(如每次 L3 miss 都记录=PEBS/IBS 高采样)时,**先在测试
     环境量化开销**,再决定生产采多久
  3. 剖析期间观察到的绝对值会略偏慢,但**热点分布**(相对占比)依然可靠——
     采样开销是均匀撒在所有代码上的

## 常见误区

- ❌ "开剖析慢 30%"的传说 —— 那是插桩(profiling by instrumentation,
  如 gcc -pg)或 tracing(每个事件都记录);采样和计数是另一个物种
- ❌ 为省开销把采样率调低到 1Hz —— 开销没省多少,统计功效先崩了
  (短函数永远采不到);100Hz 是开销与分辨率的好平衡点
- ❌ 在虚拟化环境不设防地用超高采样率 —— 定时器粒度可能让实际频率
  远低于设定值,先校准(用本例方法)
