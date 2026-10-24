# SIMD 实验场（dscode/simd）

一组**可复现**的 SIMD 微基准与正确性测试，用来回答三个问题：

1. SIMD 到底能快多少？—— 同一内核 × 6 种实现 × 4 档工作集，全部实测。
2. 什么情况下加速会消失？—— 归约、AoS 布局、gather、小规模、DRAM 带宽，各有独立用例。
3. 怎么写才不会写错？—— 边界 N、未对齐、NaN/Inf、原地别名、int8 溢出，共 13 组、10,247 条断言。

结论先行：**理论上限是"向量宽度"，实际收益在 2–7 倍之间，且完全由工作集和访存模式决定**。
L1 内 6–8 倍，L2/L3 2–3 倍，DRAM 约 1.1–1.2 倍，gather 与 AoS 约 1.0 倍。

---

## 1. 目录结构

```
dscode/simd/
├── include/
│   ├── simd_kernels.h        公共接口：10 个内核 × 6 种实现 + 运行期分发表
│   └── simd_impl.h           向量模板（128/256/512 位共用同一份内核代码）
├── src/
│   ├── kernels_generic.inc   纯 C++ 参考实现（标量与自动向量化共用）
│   ├── kernels_scalar.cpp    -fno-tree-vectorize，标量基线
│   ├── kernels_auto_sse2.cpp -O3（x86-64 默认 SSE2）自动向量化
│   ├── kernels_auto_native.cpp -O3 -march=native 自动向量化
│   ├── kernels_sse41.cpp     手写 SSE4.1（128-bit）
│   ├── kernels_avx2.cpp      手写 AVX2 + FMA（256-bit）
│   ├── kernels_avx512.cpp    手写 AVX-512F/BW/VL（512-bit）
│   ├── dispatch.cpp          CPUID 运行期探测 + 分发表
│   └── test_main.cpp         正确性测试（无第三方依赖）
├── bench/bench_simd.cpp      Google Benchmark 性能用例
├── scripts/report.ps1        JSON → 加速比表
├── scripts/analyze.py        JSON → cycles/元素、带宽、效率（详细分析）
├── scripts/clock_probe.cpp   频率标定（把 ns 换算成 cycles）
├── RESULTS.md                测试结果深度分析（带宽/延迟/宽度的解释）
├── CODE-COMPARISON.md        代码对比：源码 → 生成的汇编 → 实测数据
├── bench.yaml                bench-runner 接入配置
├── CMakePresets.json         VS2022(ClangCL) / MinGW 预设
└── CMakeLists.txt
```

## 2. 快速开始

### 2.1 MinGW-GCC（原有工作流）

```powershell
cd dscode/simd
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 1) 正确性：13 组断言，失败返回非 0
.\build\test_simd.exe

# 2) 性能：全部 282 个用例（约 4~5 分钟）
.\build\bench_simd.exe --benchmark_min_time=0.05s --benchmark_repetitions=3 `
    --benchmark_format=json --benchmark_out=build/bench.json

# 3) 出加速比表
powershell -ExecutionPolicy Bypass -File scripts/report.ps1 -Json build/bench.json
```

### 2.2 VS2022 + ClangCL（生成 `.sln`）

前置：VS2022 勾选 **“C++ Clang tools for Windows”** 组件（纯 MSVC v143 工具集仍不支持，
原因见第 9 节）。源码无需改动，CMake 里已按 clang-cl 自动切换编译开关。

```powershell
cd dscode/simd
cmake --preset vs2022-clangcl              # 生成 build-vs2022/simd_lab.sln
cmake --build --preset vs2022-clangcl-release
ctest --preset vs2022-clangcl-release
```

或直接用 VS2022“打开本地文件夹”指向 `dscode/simd`：IDE 会读取
[CMakePresets.json](CMakePresets.json) 列出 `vs2022-clangcl` 预设并完成配置；
生成后打开 `build-vs2022/simd_lab.sln` 即可编辑、断点调试、单步看 SIMD 指令。

> 结果解读、反汇编证据与根因分析见 [RESULTS.md](RESULTS.md)；
> 源码 / 汇编 / 数据的逐场景对照见 [CODE-COMPARISON.md](CODE-COMPARISON.md)。

只跑某类用例（VS2022 构建把 `.\build\` 换成 `.\build-vs2022\Release\`）：

```powershell
.\build\bench_simd.exe --benchmark_filter=BM_Add --benchmark_min_time=0.05s
.\build\bench_simd.exe --benchmark_filter=BM_Dot/avx2 --benchmark_repetitions=5
```

## 3. 实现矩阵

| 枚举 | 实现 | 编译方式 | 向量宽度 |
|---|---|---|---|
| `scalar` | 标量基线 | `-fno-tree-vectorize -fno-tree-slp-vectorize` | 1 元素 |
| `auto_sse2` | 编译器自动向量化（默认目标） | `-O3`（x86-64 默认只有 SSE2） | 4 元素 |
| `auto_native` | 编译器自动向量化（本机最优） | `-O3 -march=native` | 由编译器决定 |
| `sse41` | 手写 SSE4.1 | `-msse4.1`，无 FMA | 4 元素 |
| `avx2` | 手写 AVX2 + FMA | `-mavx2 -mfma` | 8 元素 |
| `avx512` | 手写 AVX-512 | `-mavx512f -mavx512bw -mavx512vl -mfma` | 16 元素 |

所有实现在同一个二进制里，`dispatch.cpp` 用 `__builtin_cpu_supports` 做运行期选路；
不可用的 ISA 自动回落到标量表（测试里也断言了这一点）。

工具链差异：GCC / Clang（GNU 驱动）用 `-m*` 开关；VS2022 + ClangCL 用 `/arch:AVX2`、
`/arch:AVX512`，SSE4.1 与 `-march=native` 通过 `/clang:-m*` 直通，`compile` 选项由
[CMakeLists.txt](CMakeLists.txt) 按工具链自动选择。clang-cl 下 `__builtin_cpu_supports`
在链接期需要 compiler-rt 的 `__cpu_model`，CMake 会从编译器 resource dir 自动补链
`clang_rt.builtins-x86_64.lib`。

两个刻意的设计：

- **标量基线真的标量**：单独 TU 用 `-fno-tree-vectorize` 编译，否则 `-O3` 会把"标量"也向量化，对比失去意义。
- **`auto_sse2` 与 `auto_native` 分开**：前者代表"只开 -O3、没开 -march"的常见工程状态，后者代表编译器拿到本机全部指令集后的上限。

## 4. 内核与规模矩阵

10 个内核覆盖"适合/不适合 SIMD"的典型形态：

| 内核 | 形态 | 关注点 |
|---|---|---|
| `add` / `saxpy` / `relu` | 逐元素、无依赖 | 基础吞吐、FMA、分支消除 |
| `dot` / `dot_f64` | 归约 | 标量依赖链 vs 多累加器；编译器自动向量化的盲区 |
| `normalize_soa` / `normalize_aos` | 3D 归一化 | SoA vs AoS 布局；gather + 标量写回 |
| `gather_scale` | 间接访问 | 随机索引，延迟受限 |
| `dot_int8` | 低精度整数 | 位宽利用率；`int32` 累加 |
| `add_f64` | 双精度 | 通道数减半后的收益 |

规模（本机 7800X3D：L1 32KB/核、L2 1MB/核、L3 96MB 共享）：

| 规模 | 工作集（add，三数组） | 预期瓶颈 |
|---|---|---|
| 1,024 | 12 KB | L1 + 执行端口 |
| 65,536 | 768 KB | L2 带宽 |
| 4,194,304 | 48 MB | L3 带宽 |
| 33,554,432 | 384 MB | DRAM 带宽 |

## 5. 正确性测试覆盖（"全部情况"）

| 测试组 | 覆盖内容 | 断言数 |
|---|---|---|
| `dispatch` | ISA 名称/宽度/可用性/回落表非空 | 11 |
| `add` | N=0…1,048,577，指针偏移 0/1/2/3/4/7，原地 `dst==a`，前后哨兵 | 2,412 |
| `saxpy` | 7 个 alpha（含 NaN、0、负数）× 全 N × 偏移 | 4,158 |
| `relu` | NaN/±Inf/±0/次正规数/±FLT_MAX 的位级语义（NaN→0） | 792 |
| `dot` | 与 double 参考比对，误差界随 √N 放宽；NaN 传播、Inf×0 | 606 |
| `gather` | 逆序/重复/步长/随机索引，scale ∈ {0, 负, NaN, 1e30} | 1,512 |
| `normalize` | SoA/AoS × 随机/零向量/NaN/1e30，AoS 越界写检查 | 174 |
| `int8` | -128/127 边界、随机、全 N；与 int64 参考逐位一致 | 126 |
| `int8-large` | 1M 字节长循环 + 尾部 | 6 |
| `f64` | add/dot 全 N，未对齐；`inf + -inf`、`inf × 0` | 288 |
| `f64-special` | 特殊值语义 | 12 |
| `aliasing` | `dst==b`、`relu` 原地、`saxpy(y==x)` | 144 |
| `cross-isa` | 所有 ISA 的 `add` 结果与 scalar **逐位一致** | 6 |

关键边界：N 覆盖 `0,1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,1000,1024,4095,4096,4097,65536,1000003,1048576,1048577`，
每个内核都在向量宽度 ±1 处验证尾部处理。

## 6. 实测结果

环境：AMD Ryzen 7 7800X3D（Zen4，AVX2 + AVX-512，512 位由 256 位数据通路双泵），
Windows 11 + MinGW GCC 16.1，`-O3`，3 次重复取中位数，缓冲区 4KB 页对齐。
表中数字为**相对标量基线的加速比**（>1 为更快）。

| 内核 | 规模 | scalar 耗时 | auto_sse2 | auto_native | sse4.1 | avx2 | avx512 |
|---|---|---|---|---|---|---|---|
| add | 1K | 226.5 ns | 3.24× | 6.88× | 3.40× | 6.27× | 6.88× |
| add | 64K | 15.6 µs | 2.88× | 3.08× | 2.87× | 3.00× | 3.05× |
| add | 4M | 1.08 ms | 2.08× | 2.40× | 2.21× | 2.58× | 2.50× |
| add | 32M | 9.43 ms | 1.24× | 1.02× | 1.11× | 1.13× | 1.16× |
| saxpy | 1K | 253.0 ns | 3.32× | 7.80× | 3.15× | 7.19× | 7.69× |
| saxpy | 32M | 9.42 ms | 1.83× | 1.91× | 1.65× | 1.96× | 1.88× |
| relu | 1K | 218.2 ns | 3.26× | 8.34× | 3.40× | 5.93× | 8.22× |
| dot | 1K | 586.1 ns | 1.00× | **0.81×** | 10.51× | **20.60×** | 20.58× |
| dot | 64K | 39.0 µs | 0.93× | **0.81×** | 10.58× | 11.87× | 11.98× |
| dot | 4M | 2.51 ms | 1.00× | **0.82×** | 9.50× | 9.85× | 9.89× |
| dot | 32M | 20.1 ms | 1.00× | 0.82× | 4.86× | 4.47× | 4.13× |
| dot_f64 | 1K | 587.3 ns | 1.00× | 0.95× | 3.81× | 7.07× | 10.34× |
| dot_int8 | 4K | 1.78 µs | 3.03× | 5.22× | 9.69× | **19.06×** | 19.44× |
| normalize_soa | 64K | 112.7 µs | 3.82× | 7.58× | 3.70× | 7.36× | 7.64× |
| normalize_aos | 64K | 118.6 µs | 1.37× | **2.83×** | 1.65× | **1.11×** | 1.58× |
| gather | 64K | 41.4 µs | 1.00× | 1.00× | 1.03× | 1.03× | 1.04× |
| add_f64 | 1K | 231.3 ns | 1.85× | 3.54× | 1.73× | 3.66× | 3.80× |
| add_f64 | 64K | 18.0 µs | 1.31× | 1.49× | 1.30× | 1.46× | 1.47× |
| misaligned add (off=0) | 64K | 15.4 µs | — | — | — | 2.98× | 3.03× |
| misaligned add (off=1) | 64K | 15.6 µs | — | — | — | 3.07× | 2.89× |
| tiny add | N=1 | 1.0 ns | 0.55× | 0.57× | 0.60× | 0.50× | 0.45× |
| tiny add | N=3 | 1.4 ns | 0.84× | 0.84× | 0.78× | 0.68× | 0.68× |
| tiny add | N=8 | 2.5 ns | 1.51× | 1.43× | 1.57× | 1.80× | 1.26× |
| tiny add | N=16 | 4.2 ns | 1.77× | 2.59× | 2.05× | 2.54× | 2.58× |
| tiny add | N=33 | 8.3 ns | 2.31× | 3.26× | 2.44× | 3.61× | 3.24× |

完整表格（51 行 / 282 个基准用例）：`scripts/report.ps1` 直接输出。

### 从数据里能读出的结论

1. **上限确实是向量宽度，但只在 L1 内接近**：`add@1K` 6.3–6.9×（理论 8），`relu@1K` 8.3×，
   `normalize_soa` 7.6×。`dot` 的 20× 超出宽度，因为标量版被"单累加器依赖链"卡住——
   手写版用 4 个累加器消除了延迟瓶颈，这属于**算法+ISA 的复合收益**。
2. **规模一大就掉，而且掉得很有规律**：`add` 64K→3.0×（L2 带宽）、4M→2.5×（L3）、
   32M→1.1×（DRAM）。到了 DRAM，SIMD 只剩"少吃指令"的价值，带宽是硬墙。
3. **归约是编译器自动向量化的盲区**：`auto_native` 的 `dot` 只有 0.81–0.95×（比标量还慢，
   因为浮点归约不允许重排，编译器只做标量展开），而手写 4 累加器版本有 10–20×。
   **热点归约必须手写或换库（std::reduce/oneDNN 等）**。
4. **布局决定成败**：同样算法，SoA 7.6× vs AoS 1.1×（AVX2）。AoS 上 `auto_native`（2.83×）
   反而赢过手写 AVX2（1.11×）——编译器选择了更合适的策略，说明**先改数据结构比先写 intrinsics 更值**。
5. **gather 让所有 ISA 归零**：1.00–1.04×，全部被内存延迟支配。遇到随机索引，换算法/换布局，
   ISA 宽度帮不上忙。
6. **int8 收益最大**：19×（每次吞吐 32 字节且转换便宜）；**fp64 收益约减半**（3.8× vs 6.9×），
   完全符合"fp64 通道数只有 fp32 一半"的预期。
7. **小规模会亏本**：N≤3 时 SIMD 只有 0.45–0.68×，N=8 才回本，N=16–33 有 2–3.6×。
   这也解释了书上"一次迭代约 40 个元素才值得向量化"的经验值。
8. **AVX-512 在 Zen4 上并不比 AVX2 快多少**（1K 时略好，64K 以上基本持平）：512 位指令由
   256 位通路双泵执行。换到 Intel Sapphire Rapids 会看到不同结论——**ISA 收益必须实测**。
9. **未对齐只损失约 5%**（off=0 时 2.98–3.03× vs off=1 时 2.89–3.07×）：现代 x86 的
   `loadu/storeu` 在 L1 内代价很小，"必须对齐"的古老教条只在跨 cache line / 页边界时才成立。

## 7. 一个真实的微基准陷阱：页内偏移

调试过程中发现：如果三个数组（两个源 + 一个目的）的**页内偏移不同**，Zen4 的 512 位路径会
掉到约 1/3 吞吐，而 256 位路径几乎不变：

```
skew(目的数组相对页首)   avx2        avx512
0                        5.4 µs      5.6 µs     ← 三个数组页内偏移相同
64                       5.4 µs     15.6 µs     ← 只差 64 字节
128                      5.3 µs     11.4 µs
512                      5.7 µs      5.4 µs
```

这属于 Zen4 的内存消歧/存储转发细节。它对本项目的直接影响是：**微基准必须固定内存布局**，
否则 `malloc` 每次返回的偏移不同，你会把布局噪声当成 ISA 差异（第一版数据里 AVX-512 时快时慢
就是这个原因）。因此 `bench/bench_simd.cpp` 里所有数组都用 **4KB 页对齐** 分配，保证页内偏移一致、
结果可复现。

## 8. 接入 bench-runner

`bench.yaml` 里每个条目只过滤一个 `(内核, 实现, 规模)`，保证 `parseGBJSON` 拿到的样本不混：

```bash
cd dscode/simd
../../bin/bench-runner run -c bench.yaml --label baseline
```

注意：bench-runner 的 `compare` 比较的是**两次 run**（例如优化前后两个提交）；
而"同一提交内 scalar vs avx2"属于变体对比，请用 `scripts/report.ps1`。

## 9. 局限与注意事项

- 只支持 GCC/Clang（含 VS2022 的 ClangCL 工具集）：依赖 `__builtin_cpu_supports` 与逐 TU 指令集开关；
  纯 MSVC（MSBuild v143）仍不支持，需把 CPU 探测换成 `__cpuid`/`_xgetbv` 并调整 `__builtin_sqrtf` 等用法。
- `auto_native` / `avx512` 的产物绑定了构建机指令集，**不要直接分发**；生产代码应像
  `dispatch.cpp` 那样做运行期多版本分发。
- 数字是单线程、无绑核、未锁频的桌面环境结果，适合看趋势，不适合当绝对基准。
- Windows 无 `perf` 计数器；需要 cycles/instructions/L3 miss，请到 Linux 下配合
  bench-runner 的 `counters` 使用。
- 内核刻意保持"教科书形态"（无并行、无 tiling），便于隔离变量；真实优化还需叠加
  缓存分块、多线程等（见 `docs/cpu-perf-pdf` 第 8、9 章）。

## 参考

- 第 3 章 CPU 微体系结构（SIMD/流水线/存储层次）：`docs/cpu-perf-pdf/3 CPU微体系结构.pdf`
- 第 9 章 优化计算（向量化）：`docs/cpu-perf-pdf/9 优化计算.pdf`
- 全书文本：`docs/cpu-perf-pdf/perf-book-cn.md`
