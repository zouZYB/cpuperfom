# 分支预测实验场（dscode/fzyc）

一组**可复现**的分支预测微基准，回答三个问题：

1. 分支到底能有多慢？—— 同一段代码，只改"分支结果的可预测性"，实测 0.4 → 3.2 ns/元素（约 5.5 倍）。
2. 哪些形态会打崩预测器？—— 数据顺序、周期长度、分支相关性、分支密度、间接分支目标、
   循环次数、代码布局，各自独立成组，全部实测。
3. 无分支改写值不值？—— CMOV / 掩码 / 查找表 / 算术 / 谓词逐个对照分支版本。

结论先行见 [RESULTS.md](RESULTS.md)：**方向可预测时分支几乎免费，方向随机时 ~10 个周期/次误预测；
间接分支的目标预测比方向预测更脆；小规模微基准必须防"预测器记住重复序列"的假象。**

场景与参考实现对齐《Performance Analysis and Tuning on Modern CPUs》中文版
第 3.3.4 节（分支预测）、第 10 章（优化分支预测）、第 11.4 节（基本块对齐）。

---

## 1. 目录结构

```
dscode/fzyc/
├── include/branch_lab.h      公共接口：场景注册表、输入生成、内核声明、编译器适配
├── src/
│   ├── pattern_gen.cpp       数据分布 / 分支模式 / 跳转目标 / 循环次数生成器
│   ├── prepare.cpp           Config → Input（一次性生成所有数组）
│   ├── kernels_data.cpp      数据相关条件分支：if vs CMOV（求和/计数/max/abs/clamp）
│   ├── kernels_pattern.cpp   周期模式、双分支、全局历史链、相关分支、分支密度
│   ├── kernels_indirect.cpp  switch 跳转表、if-else 链、函数指针、虚函数
│   ├── kernels_loop.cpp      内层循环次数、嵌套循环、循环展开
│   ├── kernels_branchless.cpp 查找表 / 算术 / 谓词替换分支
│   ├── kernels_meta.cpp      likely/unlikely/unpredictable、热路径顺序、调用/返回、对齐
│   ├── registry.cpp          (内核 × 数据 × 模式 × 结构参数) 组合成 224 个场景
│   └── test_main.cpp         正确性测试：21,137 条断言
├── bench/bench_branch.cpp    每个场景一个 Google Benchmark 用例
├── scripts/report.ps1        JSON → ns/元素、cycles/元素、相对倍数
├── scripts/analyze.py        JSON → 分组分析与误预测成本估算
├── scripts/check_branches.ps1 反汇编统计：确认"分支版真的有分支、CMOV 版真的没分支"
├── bench.yaml                bench-runner 接入配置
├── CMakePresets.json         VS2022(ClangCL) / MinGW 预设
└── CMakeLists.txt
```

## 2. 快速开始

### 2.1 MinGW-GCC

```powershell
cd dscode/fzyc
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 1) 正确性：21,137 条断言，失败返回非 0
.\build\test_branch.exe

# 2) 性能：224 个场景（约 1~2 分钟）
.\build\bench_branch.exe --benchmark_min_time=0.05s --benchmark_repetitions=3 `
    --benchmark_format=json --benchmark_out=build/bench.json

# 3) 出表与分析
powershell -ExecutionPolicy Bypass -File scripts/report.ps1 -Json build/bench.json
python scripts/analyze.py build/bench.json
```

### 2.2 VS2022 + ClangCL（生成 `.sln`）

前置：VS2022 勾选 **"C++ Clang tools for Windows"** 组件。

```powershell
cd dscode/fzyc
cmake --preset vs2022-clangcl              # 生成 build-vs2022/fzyc.sln
cmake --build --preset vs2022-clangcl-release
ctest --preset vs2022-clangcl-release
```

也可以直接用 VS2022 "打开本地文件夹" 指向 `dscode/fzyc`，IDE 会读取
[CMakePresets.json](CMakePresets.json)。

## 3. 场景矩阵（224 个）

| 组 | 内核 | 变体维度 | 用例数 |
|---|---|---|---|
| `data` | 求和 / 计数 / max / abs / clamp 的 if 版与 CMOV 版 | 12 种数据分布（随机、升/降序、全真、全假、交替、块状 4/8/16/32/64/256）+ 1M 大数组 | 60 |
| `store` | 条件存储 if 版 vs CMOV 版 | 随机 / 有序 / 交替 | 6 |
| `pattern` | `pattern_sum` 单分支、`pattern_two` 双分支、`history_chain` 全局历史 | 25 种方向模式（全真/全假/交替/周期 3..32768/游程 3,8,32/随机/相位切换）+ 历史长度 2..8 | 36 |
| `corr` | `nested_corr` 内外分支相关、`xor_prev` 相邻相关 | 完全相关 / 反相关 / 独立 / 跨迭代异或 | 9 |
| `density` | `density` 每元素 N 个独立分支 | N = 1,2,3,4,8 × 可预测/随机/周期 4 | 15 |
| `indirect` | `switch_ways` 跳转表、`ifchain_ways` 分支链、`callptr` 函数指针、`virtual_call` 虚函数 | 2/4/8/16/64 路 × 顺序/随机/固定目标 | 33 |
| `loop` | `inner_trip` 内层循环次数、`nested_loop`、`unroll` 展开 | 循环次数 1..16 × 恒定/交替/随机；展开 1/2/4/8 | 27 |
| `branchless` | `map_*` 分支链/查表/算术、`predication_*` 分支 vs 两条都算 | 数据集中/含越界；随机/交替/周期 4 | 12 |
| `hint` | `hint_none/likely/unlikely/unpredictable`、`hot_first/cold_first` | 90% / 99% 偏斜 | 10 |
| `call` | 直接调用、8 个调用点轮转、固定深度递归 | 1/2/4/8 个目标；递归深度 1/4/8/16 | 7 |
| `random` | `random_mix` 长随机序列（≈50% 误预测） | 1M 元素 × 6 个 seed | 6 |
| `align` | 同一循环、函数入口对齐 16/32/64 字节 | 3 种对齐 | 3 |

## 4. 测量方法（为什么这些数字可信）

1. **保证被测代码里真的有分支。** 两个分支体里各放一条编译期屏障
   `FZ_BARRIER()`：不产生任何机器指令，但阻止编译器把 if/else 折叠成
   `cmov`/`sel`。屏障带一个唯一立即数（`__COUNTER__`），否则 LLVM 会把两个
   分支里相同的空 asm 提到分支外、照样 if-convert —— 这一点在 clang-cl 上实测踩过。
   `scripts/check_branches.ps1` 对编译产物做静态统计，确认分支版 `cmov=0`、
   CMOV 版 `cmov>0`（见 RESULTS.md 第 7 节）。
2. **关掉自动向量化。** 本实验只看控制流；CMOV 版如果被向量化，对比就失去意义。
   CMake 对 GCC/Clang/ClangCL 统一加 `-fno-tree-vectorize` /
   `-fno-vectorize -fno-slp-vectorize`。
3. **防止预测器"记住"重复序列。** 微基准会反复调用同一内核；n 太小时同一段
   分支结果序列会在调用间重复，实测 n=4K 的随机分支只跑出 0.8 ns/元素
   （n≥64K 时 2.5 ns/元素），这是测量假象而不是 CPU 变快。因此：
   - 默认 n = 64K；
   - 每个用例准备 8 组不同 seed 的输入轮转使用（n ≥ 256K 时 2 组，避免内存膨胀）。
4. **不测 PMU 计数器。** Windows 上没有 `perf`，本实验只测墙钟时间；把 ns 换算成
   周期的系数取 Google Benchmark 报告的标称频率，不追求绝对周期数，只做同机对比。
5. 每个用例 `--benchmark_min_time=0.05s --benchmark_repetitions=3`，取中位数。

## 5. 正确性测试覆盖（21,137 条断言）

`test_branch.exe` 对**每个场景**用**独立参考实现**（语义相同的朴素写法）逐场景比对：

- n 边界：`0,1,2,3,4,5,7,8,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,1000,4096,4097`
- 全部 19 种数据分布 × 全部 25 种分支模式 × 全部 4 种相关性
- 结构参数：p1 ∈ {1,2,4,8}、p2 ∈ {0,1,2}、a/b 多组（含 0、255、非法区间）
- 2 个不同 seed（可复现性）
- 条件存储类内核额外**逐元素**比对 `dst` 数组
- 生成器自检：排序性、偏斜比例、周期检测、游程长度、取值范围、确定性
- 注册表自检：224 个场景名称唯一、每组非空、每个 case 的 fn/ref/cfg 合法

## 6. 实测结果

见 [RESULTS.md](RESULTS.md)（同机 MinGW-GCC 与 VS2022-ClangCL 均跑通 21,137 条断言；
性能数字取自 GCC 构建，ClangCL 产物形态一致）。关键数字：

| 现象 | 可预测 | 不可预测 | 倍数 |
|---|---:|---:|---:|
| 条件求和（64K 元素） | 0.386 ns/元素 | 1.810 ns/元素 | 4.7x |
| 分支方向模式（单分支） | 1.89 cycle/元素 | 13.54 cycle/元素 | 7.2x |
| 8 路 switch 目标 | 1.381 ns/元素 | 5.293 ns/元素 | 3.8x |
| mapToBucket（6 分支链 vs 查表） | 0.381 ns/元素 | 5.210 ns/元素 | 13.7x |
| 随机数据：分支版 vs 掩码版 | 1.810 | 0.514 | 3.5x（掩码更快） |
| 偏斜数据：分支版 vs CMOV 版 | 0.878 | 0.513 | 1.7x（CMOV 更快） |

预测器容量：周期 ≤256 位几乎完全可学，8192 位以上接近随机。

## 7. 代码形态检查

```powershell
# GCC 目标文件（MSYS2 objdump）
powershell -ExecutionPolicy Bypass -File scripts/check_branches.ps1 `
    -Obj build/CMakeFiles/branch_lab.dir/src/kernels_data.cpp.obj
# ClangCL 静态库（VS 自带 llvm-objdump）
powershell -ExecutionPolicy Bypass -File scripts/check_branches.ps1 `
    -Obj build-vs2022/Release/branch_lab.lib
```

输出每个函数的 `jcc`（条件跳转）/`jmp`/`cmov`/`call` 静态条数。

## 8. 接入 bench-runner

```bash
cd dscode/fzyc
../../bin/bench-runner run -c bench.yaml --label baseline
```

注意：bench-runner 的 `compare` 比较的是两次 run（优化前后）；同一提交内的
"可预测 vs 不可预测"属于变体对比，用 `scripts/report.ps1` 更直接。

## 9. 局限与注意事项

- 只看**方向/目标预测**，不测 PMU（`BR_MISP_RETIRED` 等）；Windows 上无 `perf`。
- 现代 CPU 的分支预测器细节未公开，本实验只给"行为学"结论：哪些形态快、哪些慢，
  不解释到具体预测器结构。
- `hint_*` 组只影响**代码布局**（fallthrough/分支顺序），现代预测器并不消费
  likely/unlikely 提示；`hint_unpredictable` 只有 Clang-17+ 才会转 CMOV。
- `align` 组在 GCC/Clang(GNU 驱动) 下用 `__attribute__((aligned(N)))` 生效；
  MSVC 前端忽略该属性，三个变体代码相同（结果应接近）。代码布局的完整实验
  需要 `-mllvm -align-all-blocks` 或 PGO/BOLT，超出本实验范围。
- 绝对数字依赖 CPU、频率、SMT、操作系统调度；只适合同机对比趋势。

## 参考

- 第 3.3.4 节 分支预测（BTB、局部/全局相关性、TAGE）
- 第 10 章 优化分支预测（查找表、算术、谓词/CMOV、`__builtin_unpredictable`）
- 第 11.4 节 基本块对齐
- Google Benchmark 文档
