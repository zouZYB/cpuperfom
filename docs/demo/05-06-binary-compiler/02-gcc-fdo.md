# 02 GCC FDO:反馈导向优化(间接调用提升)

> 对应笔记:第五章-2《AutoFDO》、第六章-5《Engineering a Compiler》
> 与 [01-go-pgo](01-go-pgo-devirtualization.md) 同一思想的 C/C++ 版本。

## 目的

用 GCC 的 `-fprofile-generate` / `-fprofile-use` 完成一次完整的 FDO(Feedback-Directed
Optimization)流程:**先编译"带计数器"的二进制 → 跑代表性负载收集执行计数 →
重新编译时按计数优化**。本例展示其杀手锏:**间接调用提升(indirect call promotion)**——
把函数指针调用按画像提升为直接调用并内联。

## 原理

```
无 FDO:  s += ops[i](x);
         → 每次迭代一次间接调用:不可内联、分支预测间接目标

有 FDO:  .gcda 计数文件告诉 GCC"95% 的调用目标是 op_double"
         → 生成: if (ops[i] == op_double) s += 内联展开; else s += ops[i](x);
         → 热目标变直接调用 + 内联

数据中心同款技术:AutoFDO 用 perf 采样代替插桩计数,BOLT 在链接后做同样的事
```

## 代码(main.c)

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N (1<<22)
typedef int (*op_t)(int);
static op_t ops[N];
static int data[N];
static long long sink;

static int op_double(int x) { return x * 3 + 1; }   /* 热目标:占 95% */
static int op_half(int x)   { return x / 2; }
static int op_neg(int x)    { return -x; }

static long long run(void) {
    long long s = 0;
    for (int i = 0; i < N; i++)
        s += ops[i](data[i]);       /* 间接调用:不看 profile 无法内联 */
    return s;
}

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
    srand(42);
    for (int i = 0; i < N; i++) {
        int r = rand() % 100;
        ops[i] = r < 95 ? op_double : (r < 98 ? op_half : op_neg);
        data[i] = rand();
    }
    run(); /* 预热 */
    double best = 1e9;
    for (int r = 0; r < 10; r++) {
        double t0 = now_ms();
        sink = run();
        double d = now_ms() - t0;
        if (d < best) best = d;
    }
    printf("run: %.3f ms (sink=%lld)\n", best, sink);
    return 0;
}
```

## 运行(注意:同一输出文件名贯穿全程)

```bash
# ① 无 FDO 基线
gcc -O2 -o fdo_baseline.exe main.c
./fdo_baseline.exe

# ② 插桩编译(-fprofile-generate:二进制里埋入执行计数器)
gcc -O2 -fprofile-generate -o fdo.exe main.c

# ③ 跑代表性负载 → 生成 fdo-main.gcda 计数文件
./fdo.exe

# ④ 带画像重新编译(优化发生在这里;输出文件名必须与③一致,
#    否则 GCC 找不到同名 .gcda)
gcc -O2 -fprofile-use -o fdo.exe main.c

# ⑤ 对比
./fdo.exe
```

## 预期结果(本仓库开发机实测,gcc 16.1)

```
--- 无 FDO ---
run: 5.617 ms
--- 有 FDO ---
run: 2.958 ms        → 1.9x
```

## 分析要点

- **三个产物要分清**:插桩二进制(慢,带计数)、`.gcda` 画像、优化后二进制(快)
- 工作流铁律:**`-fprofile-generate` 和 `-fprofile-use` 用同一个输出文件名**,
  否则报 `profile count data file not found`;重新插桩前要删旧 `.gcda`,
  否则报 `overwriting an existing profile data with a different checksum`
- FDO 的其他收益(大二进制上更明显):基本块重排(热代码聚集,I-cache 友好)、
  冷热函数分离(`-fprofile-reorder-functions`,对应五-3 HFSort)、寄存器分配倾斜
- AutoFDO 与它的区别:AutoFDO 用 `perf record` 采样代替插桩,生产环境零侵入;
  用法 `gcc -fauto-profile=perf.data.gcov`(需 Linux + autofdo 工具链转换)
- 本例 1.9x 是微基准理想值;真实大型 C++ 程序 FDO 典型收益 5~15%

## 常见误区

- ❌ 画像与负载不匹配 —— 用单元测试画像优化在线服务,会把冷路径当热路径优化
- ❌ 插桩二进制直接上线 —— 它带计数开销,只用于采集
- ❌ 以为 -O3 已经"最优" —— -O3 只有静态信息;画像提供的是运行时分布信息,两者正交
