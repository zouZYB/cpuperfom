# 01 硬件性能计数器:用 AMD uProf PCM 给 demo 拍"CT"

> 对应笔记:第一章(硬件厂商优化手册:PMU 是地面真相)、第四章(TMA:
> 自顶向下归因方法)

## 目的

前面的 demo 都在测**时间**(快了多少倍)。但"为什么快/慢"要靠 **PMU**
(Performance Monitoring Unit):CPU 内置的硬件计数器,直接告诉你 IPC、缓存
命中率、分支误预测率、TLB miss。本例用 AMD 官方的 `AMDuProfPcm.exe` 给本目录的
demo 拍 CT,学会"看计数器做归因"——这是 TMA 方法的手工版。

## 原理

```
时间测量:  "慢 10 倍" —— 现象
PMU 测量:  "L1 miss 率从 2% → 38%,IPC 从 3.1 → 0.4" —— 机理

TMA 自顶向下:
  前端瓶颈?(取指/解码不够)
  后端瓶颈?—— 内存绑定(dc/l2/l3 miss 高) or 计算绑定(IPC 接近理论峰值)
  分支预测?(branch miss 高)
  投机取消?(bad speculation)

典型计数器对应 demo:
  07-* 分支预测  → branch misses(误预测率)
  08-* 缓存/TLB  → dc misses(L1D)、l2/l3 miss、tlb
  05-* SIMD      → IPC / avx_imix(向量指令占比)
```

## 运行(本仓库开发机实测命令)

AMDuProfPcm 在 AMD 机上是系统级采集,命令模板:

```bash
PCM="C:\Program Files\AMD\AMDuProf\bin\AMDuProfPcm.exe"

# 采 60 秒、按 socket 聚合(-A system)、静默模式、附带运行被测程序
"$PCM" -m ipc,l1,l2,l3,dc -C -A system -q -a -d 60 -o out.csv -- your-demo.exe

# 支持的指标组(-m 可组合):
#   ipc, l1, l2, l3, dc, tlb, fp, avx_imix, cache_miss, pipeline_util
```

> ⚠️ 两个坑:
> 1. PCM 的输出必须用 `-o` 写**真实文件**,管道重定向拿不到数据
> 2. `-A system` 是**全系统**采集:跑 demo 期间系统里其他程序的计数也会混进来——
>    demo 要让被测负载占满足够长的时间(如循环 30~60 秒),让它的信号压过噪声

## 读数指南(各 demo 该看什么)

| demo | 跑的负载 | 关键指标 | 预期变化(优化前→后) |
|---|---|---|---|
| [07-01](../07-branch-prediction/01-sorted-vs-unsorted.md) | 排序 vs 乱序求和 | branch miss rate | 乱序版 ~50% → 排序版 ~0% |
| [08-01](../08-memory-cache-tlb/01-cache-line-size.md) | 跨步指针追逐 | dc(L1D)miss、l2/l3 miss | 步长 ≥64B 后 miss 率饱和 |
| [08-02](../08-memory-cache-tlb/02-cache-levels-latency.md) | 随机环追逐 | l1/l2/l3 命中率 | 工作集越大,低层 miss 越高 |
| [08-04](../08-memory-cache-tlb/04-false-sharing.md) | 4 线程假共享 | dc miss + IPC | 假共享版 IPC 崩到 1 以下 |
| [05-05](../05-06-binary-compiler/05-simd-avx2.md) | AVX2 vs 标量 | IPC、avx_imix | SIMD 版 IPC 反而可能降(带宽先满)但 avx 占比飙升 |

读数时的纪律(TMA 精神):

1. **先看 IPC**:>3 偏计算健康;<1 必有瓶颈(内存/分支/依赖链)
2. **再分前后端**:分支 miss 高 → 前端;cache miss 高 → 后端内存;都低但 IPC 低 → 后端计算
3. **最后定位**:时间维度找热点函数(用 [04-01](../04-profiling/01-sampling-profiler-pprof.md)
   的采样器),计数器维度找机理——两者交叉验证

## 与仓库的集成

本仓库的 runner 已经把 PCM 集成进 CI 性能回归流程:见 `runner/counters/uprof.go`,
benchd 可以在跑基准的同时拉起 `AMDuProfPcm.exe` 采集计数器,与耗时一起入库——
**时间的回归 + 计数器的归因**一次跑完。注意 PCM 需要管理员权限且整机独占,
CI 里应保证该 agent 同一时间只跑一个基准(与 [02-01](../02-benchmarking/01-min-vs-mean.md)
的噪声控制一致)。

## 常见误区

- ❌ 拿计数器绝对值跨机器比较 —— 不同代 CPU 计数器定义不同,只在同机做相对对比
- ❌ 虚拟化环境里盲信计数器 —— 部分 PMU 事件需要宿主机透传,数值可能残缺
  (先跑一个已知负载验证计数器会动)
- ❌ 只看计数器不看时间 —— "IPC 很高"不代表快(可能在空转自旋锁),时间永远是终审
- ❌ 用系统级采集却跑 100ms 的负载 —— 信号淹没在系统噪声里;要么加长负载,
  要么找 per-process 采集方式(Linux 上对应 `perf stat ./prog`)
