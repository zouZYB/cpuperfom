# 性能优化演示用例集(demo)

> 配套阅读:《perf-book-references-notes.md》。每个用例一个 md 文件,内含**完整可运行的代码**
> (Go 为主,二进制/编译器类用 C+gcc),把笔记中的理论(缓存、分支预测、测量方法论、回归检测、
> 采样剖析、二进制优化、真实性能 bug)变成可以亲手复现的实验。

## 目录与笔记章节对应

| 文件夹 | 用例 | 演示的现象 | 对应笔记章节 |
|--------|------|-----------|-------------|
| `01-pmu-uprof/` | [01-uprof-counters](01-pmu-uprof/01-uprof-counters.md) | 用 AMD uProf PCM 硬件计数器给程序拍"CT" | 一-章导言、四-TMA |
| `02-benchmarking/` | [01-min-vs-mean](02-benchmarking/01-min-vs-mean.md) | 噪声环境下"取最小值"比均值更稳健 | 二-2 Robust Benchmarking |
| `02-benchmarking/` | [02-timer-overhead](02-benchmarking/02-timer-overhead.md) | 计时器自身的开销与误差 | 二-5 Paoloni (Intel) |
| `02-benchmarking/` | [03-measurement-bias](02-benchmarking/03-measurement-bias.md) | 链接顺序/环境变量/4K 别名偏差(Zen 4 复现:免疫) | 二-3 Mytkowicz、二-4 |
| `03-regression-detection/` | [01-changepoint-detection](03-regression-detection/01-changepoint-detection.md) | 从噪声时间序列中自动定位性能回归点 | 三-8/9/10 变点检测 |
| `04-profiling/` | [01-sampling-profiler-pprof](04-profiling/01-sampling-profiler-pprof.md) | 采样剖析器定位热点函数 | 四-1/2/3 采样与 TMA |
| `04-profiling/` | [02-profiler-overhead](04-profiling/02-profiler-overhead.md) | 剖析器自身开销:计数≈0,100Hz 采样 +0.2% | 四-2 Nowak & Bitzes、四-3 |
| `05-06-binary-compiler/` | [01-go-pgo-devirtualization](05-06-binary-compiler/01-go-pgo-devirtualization.md) | Go PGO:不改代码,重新编译提速 3x | 五-2 AutoFDO、六-10 |
| `05-06-binary-compiler/` | [02-gcc-fdo](05-06-binary-compiler/02-gcc-fdo.md) | GCC FDO:画像驱动间接调用提升 | 五-2 AutoFDO、六-5 |
| `05-06-binary-compiler/` | [03-auto-vectorization](05-06-binary-compiler/03-auto-vectorization.md) | -O 级别与 -march=native 的量化收益 | 六-11/12/13 Agner |
| `05-06-binary-compiler/` | [04-aliasing-restrict](05-06-binary-compiler/04-aliasing-restrict.md) | 指针别名阻止优化,__restrict 提速 6.9x | 六-7 编译器极限 |
| `05-06-binary-compiler/` | [05-simd-avx2](05-06-binary-compiler/05-simd-avx2.md) | 手写 AVX2 intrinsics 22x | 九-16 Base64 SIMD、六-8 |
| `07-branch-prediction/` | [01-sorted-vs-unsorted](07-branch-prediction/01-sorted-vs-unsorted.md) | 有序 vs 随机数据,分支预测命中率天壤之别 | 七-4 Lemire |
| `07-branch-prediction/` | [02-branchless](07-branch-prediction/02-branchless.md) | 无分支化消除误预测 | 七-3/七-4 |
| `07-branch-prediction/` | [03-indirect-branch](07-branch-prediction/03-indirect-branch.md) | 间接分支(函数指针/虚调用)的预测阶梯 5x | 七-3 Kapoor、七-2 |
| `08-memory-cache-tlb/` | [01-cache-line-size](08-memory-cache-tlb/01-cache-line-size.md) | 步长扫描实测缓存行 = 64B | 八-章导言 |
| `08-memory-cache-tlb/` | [02-cache-levels-latency](08-memory-cache-tlb/02-cache-levels-latency.md) | 指针追逐实测 L1/L2/L3/内存延迟阶梯 | 八-章导言、memory-hierarchy 图 |
| `08-memory-cache-tlb/` | [03-sequential-vs-random](08-memory-cache-tlb/03-sequential-vs-random.md) | 空间局部性:顺序 vs 随机访问 | 八-章导言 |
| `08-memory-cache-tlb/` | [04-false-sharing](08-memory-cache-tlb/04-false-sharing.md) | 伪共享与 64B 对齐填充 | 八-章导言、缓存一致性协议.md |
| `08-memory-cache-tlb/` | [05-loop-tiling-matmul](08-memory-cache-tlb/05-loop-tiling-matmul.md) | 循环顺序 + 分块(Tiling)优化矩阵乘 | 八-10 SPEC 特征化、roofline |
| `08-memory-cache-tlb/` | [06-aos-vs-soa](08-memory-cache-tlb/06-aos-vs-soa.md) | 数据布局:AoS vs SoA | 0.3 术语表 |
| `08-memory-cache-tlb/` | [07-eytzinger-search](08-memory-cache-tlb/07-eytzinger-search.md) | Eytzinger 层序布局加速二分查找 | 八-5 Khuong & Morin |
| `08-memory-cache-tlb/` | [08-tlb-walk-stride](08-memory-cache-tlb/08-tlb-walk-stride.md) | 步长扫描观测 TLB miss 代价 | 八-6 Hugepage |
| `08-memory-cache-tlb/` | [09-hugepages-tlb](08-memory-cache-tlb/09-hugepages-tlb.md) | 2MB 大页扩大 TLB 覆盖 512 倍(4.1x,需特权) | 八-6 Hugepage、六-10 |
| `08-memory-cache-tlb/` | [10-soft-vs-hard-pagefault](08-memory-cache-tlb/10-soft-vs-hard-pagefault.md) | 软缺页 vs 硬缺页(12x,HDD 上为 1000x) | 八-章导言、八-6 |
| `09-realworld-bugs/` | [01-regexp-compile-in-loop](09-realworld-bugs/01-regexp-compile-in-loop.md) | 循环内重复编译正则(58x) | 九-15 Jin et al. 性能 bug |
| `09-realworld-bugs/` | [02-string-concat-allocation](09-realworld-bugs/02-string-concat-allocation.md) | 循环内字符串拼接的 O(n²) 分配(~4000x) | 九-15 Jin et al. 性能 bug |
| `10-parallelism/` | [01-amdahl-lock-contention](10-parallelism/01-amdahl-lock-contention.md) | Amdahl 定律:s=40% 时 8 核仅 ~2x,对照组 13.5x | 九-12 Leiserson、四-7 GAPP |

## 运行方法

每个 md 文件里是一段完整可编译的程序。运行步骤:

```bash
# Go 用例:保存为 main.go,直接运行(单文件模式无需 go.mod)
mkdir C:\temp\demo01 && cd C:\temp\demo01
# (复制代码到 main.go)
go run main.go

# C 用例(05-06 目录的 02~05):保存为 main.c,用 gcc 编译
gcc -O2 -march=native -o demo.exe main.c
./demo.exe
```

> Windows 下 gcc 用 MSYS2 自带的(gcc 16.1/clang 22.1.8 实测可用)。

## 测量纪律(所有用例通用,对应第二章)

1. **关闭无关程序**,插上电源(笔记本),避免降频干扰
2. 每个用例内部已**重复多次取最小值**(第二章的核心结论)
3. 第一遍运行包含 JIT/预热开销,以程序自动取的最小值为准
4. 不同 CPU 的绝对数字不同,关注**倍数关系和拐点位置**

## 实测环境说明

各用例的"预期结果"均为本仓库开发机实测:**AMD Ryzen 7 7800X3D**(8C16T,**96MB 3D V-Cache L3**,
支持 AVX-512),Windows 11(虚拟化环境,`time.Now()` 粒度约 0.5ms)。两个与本机强相关的结果:

- 大容量 L3 会让"小工作集"的缓存对比实验失效(如 06 AoS/SoA 在 64MB 时无差距,
  05 分块在 24~96MB 工作集下无收益)——用例中已按实测选择能暴露现象的规模
- 粗粒度计时器要求所有计时窗口 ≥ 5ms,用例统一采用"窗口 ÷ 重复次数"

注意:`02-benchmarking/02-timer-overhead.md` 内含**两段独立程序**(主实验 + 计时器分辨率自检),
需分别保存为两个 `main.go` 运行。

## 未覆盖的章节说明

以下章节的内容在本机(Windows、单机)不适合做演示,建议按笔记指引在对应环境实操:

- **第五章(数据中心优化)剩余部分**:AutoFDO/BOLT/HFSort 需要 Linux + perf + 大二进制;
  但其核心思想"画像驱动的二进制优化"已由 05-06 的 Go PGO / GCC FDO 两个用例覆盖
- **第四章 4~8 篇(虚拟化/PEBS/IBS/Coz)**:需要 Linux 或专用工具
- **第九章宏观趋势综述部分**:综述类,无可执行实验
