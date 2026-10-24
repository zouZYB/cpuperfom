# 03 编译器自动向量化:-O 级别与 -march=native 的真实差距

> 对应笔记:第六章-11/12/13(Agner Fog 三部曲)、六-8(ISPC:为什么手写 SPMD)
> 数据类型:C(gcc 16.1)

## 目的

回答两个日常疑问:① 编译优化等级到底值多少?② `-march=native`(让编译器使用本机
AVX2/AVX-512 指令)到底值多少?用两个不同特性的循环核给出量化答案,并学会
**用 `-fopt-info-vec` 检查循环是否真的被向量化了**——不猜,看编译器报告。

## 原理

```
标量:  一条指令处理 1 个 float
SSE2:  一条指令处理 4 个(128 位,x86-64 基线,-O2 默认可用)
AVX2:  一条指令处理 8 个(256 位)
AVX-512:一条指令处理 16 个(512 位,-march=native 在 7800X3D 上启用)

瓶颈分两种(对应 Roofline 图):
  内存密集核:带宽先饱和,SIMD 收益有限
  计算密集核:每元素运算多,SIMD 宽度直接决定吞吐
```

## 代码(combined.c)

```c
#include <stdio.h>
#include <time.h>
#define N (1<<24)
static float a[N], b[N], c[N];
static float sink;
static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

/* 核 A:流式乘加(内存密集:3 条 64MB 流,带宽主导) */
static void multiply_add(const float *x, const float *y, float *z, int n) {
    for (int i = 0; i < n; i++) z[i] = x[i] * y[i] + 1.0f;
}

/* 核 B:Horner 多项式(计算密集:每元素 6 乘 6 加) */
static void poly(const float *in, float *out, int n) {
    for (int i = 0; i < n; i++) {
        float v = in[i], r = 1.0f;
        r = r * v + 0.5f; r = r * v - 0.25f; r = r * v + 0.125f;
        r = r * v + 2.0f; r = r * v - 1.0f;  r = r * v + 0.75f;
        out[i] = r;
    }
}

static double bench(void (*f)(const float*, const float*, float*, int)) {
    double best = 1e9;
    for (int r = 0; r < 8; r++) {
        double t0 = now_ms(); f(a, b, c, N); double d = now_ms() - t0;
        if (d < best) best = d;
    }
    return best;
}

static void poly_wrap(const float *x, const float *y, float *z, int n) { (void)y; poly(x, z, n); }

int main(void) {
    for (int i = 0; i < N; i++) { a[i] = (float)(i % 97) * 0.01f; b[i] = (float)(i % 7); }
    printf("核A 流式乘加   : %.2f ms\n", bench(multiply_add));
    printf("核B Horner多项式: %.2f ms\n", bench(poly_wrap));
    sink = c[123];
    return 0;
}
```

## 运行

```bash
# 三档对比
gcc -O0                -o comb.exe combined.c && ./comb.exe
gcc -O2                -o comb.exe combined.c && ./comb.exe
gcc -O3 -march=native  -o comb.exe combined.c && ./comb.exe

# 关键技能:查看向量化报告(哪些循环被向量化、用了多宽的向量)
gcc -O3 -march=native -fopt-info-vec combined.c -o comb.exe
gcc -O2                -fopt-info-vec-missed combined.c -o comb.exe   # 看"没向量化"的原因
```

## 预期结果(本仓库开发机实测,7800X3D 支持 AVX-512)

```
              -O0       -O2        -O3 -march=native
核A(内存密集) 22.50 ms   6.28 ms    3.22 ms     ← -O0→O2: 3.6x;O2→native: 2.0x
核B(计算密集) 127.39 ms  15.12 ms   1.93 ms     ← -O0→O2: 8.4x;O2→native: 7.8x!

向量化报告(-O3 -march=native):
main.c:10:23: optimized: loop vectorized using 64 byte vectors and unroll factor 16
```

读法:

- **-O0 → -O2 是永远要拿的钱**:3~8 倍,一行代码不改
- **-march=native 的收益取决于核的类型**:内存密集核 2x(带宽先到顶),
  计算密集核 7.8x(SSE2→AVX-512 宽度 ×4 + FMA 融合乘加)——Roofline 模型的现场版
- `-fopt-info-vec` 显示 "64 byte vectors" = AVX-512 正在工作

## 分析要点

- gcc 从 12 起 `-O2` 也开向量化(very-cheap 成本模型),但核 B 在 -O2 下仍未充分
  向量化——**优化级别够不够,必须看报告,不能假设**
- `-march=native` 的代价:二进制**不可移植**(在旧 CPU 上非法指令崩溃);
  服务器同构集群可以用,分发软件用 `-march=x86-64-v3` 等分级基线
- 编译器不向量化常见原因:指针可能重叠(见 [04-aliasing-restrict](04-aliasing-restrict.md))、
  浮点重排风险(`-ffast-math` 才放行)、循环体有函数调用、迭代数未知
- 当自动向量化失败时的出路:手写 intrinsics(见 [05-simd-avx2](05-simd-avx2.md))、
  ISPC(六-8)、改数据布局(SoA,见 08-06)

## 常见误区

- ❌ "开了 -O3 就一定向量化了" —— 用 `-fopt-info-vec-missed` 验证
- ❌ 忽略 `unroll factor` —— 向量化 + 展开的乘积才是真实并行度
- ❌ 在生产集群混用不同代 CPU 还用 native —— 迁移即崩溃
