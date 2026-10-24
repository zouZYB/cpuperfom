# 04 指针别名:编译器最大的"不敢",__restrict 怎么解

> 对应笔记:第六章-7《Future Directions for Optimizing Compilers》(Lopes & Regehr:
> "编译器不是魔术师")、六-11(Agner Fog 优化指南"编译器的障碍"一节)

## 目的

C/C++ 里两个指针**可能指向同一块内存**,编译器必须按最保守假设生成代码。
本例展示一种连"运行时别名检查(loop versioning)"都救不了的情况:
**循环边界本身通过指针读取,而循环体又写同类型指针**——边界每轮都要重读,
向量化彻底没戏。`__restrict` 一句话解除,提速 6.9x。

## 原理

```
void bump(int *v, int *n) {
    for (int i = 0; i < *n; i++) v[i] += 1;
}

编译器视角:v[i] += 1 是写 int,而 *n 也是 int
  → 万一 v 指向 n 呢?写 v[i] 可能改变 *n → 循环边界可能变化!
  → 每次迭代必须重新读 *n(内存读进关键路径),且无法向量化

加 __restrict:程序员承诺"v 和 n 绝不重叠"
  → *n 提升出循环只读一次 → 循环整体 AVX-512 向量化

现代 GCC 的"loop versioning"(生成运行时别名检查)能救简单的两指针读写,
但救不了这种"边界依赖"情形——检查本身就要先确定边界。
```

## 代码(main.c)

```c
#include <stdio.h>
#include <time.h>
#define N (1<<24)
static int v[N];
static long long sink;
static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

/* v[i] 的写可能修改 *n(同类型指针):边界每轮重读,无法向量化 */
__attribute__((noinline))
static void bump_unknown(int *v, int *n) {
    for (int i = 0; i < *n; i++) v[i] += 1;
}

/* restrict 承诺不重叠:边界提升,循环向量化 */
__attribute__((noinline))
static void bump_restrict(int *__restrict v, int *n) {
    for (int i = 0; i < *n; i++) v[i] += 1;
}

int main(void){
    int n = N;
    for (int i = 0; i < N; i++) v[i] = i;
    double b1=1e9,b2=1e9;
    for (int r=0;r<10;r++){ double t0=now_ms(); bump_unknown(v,&n); double d=now_ms()-t0; if(d<b1)b1=d; }
    for (int r=0;r<10;r++){ double t0=now_ms(); bump_restrict(v,&n); double d=now_ms()-t0; if(d<b2)b2=d; }
    sink=v[N-1];
    printf("unknown : %.2f ms\nrestrict: %.2f ms  (%.1fx)\n", b1, b2, b1/b2);
    return 0;
}
```

## 运行

```bash
gcc -O3 -march=native -o alias.exe main.c
./alias.exe

# 查看编译器"不敢"的证据
gcc -O3 -march=native -fopt-info-vec-all main.c -o alias.exe 2>&1 | grep -i "clobber\|missed\|vectorized"
```

## 预期结果(本仓库开发机实测)

```
unknown : 4.38 ms
restrict: 0.63 ms  (6.9x)
```

## 分析要点

- 这是**正确性问题不是性能偏好**:如果 v 真的和 n 重叠,提升边界就改变了程序语义——
  编译器保守是因为它必须对
- 解除别名的三种手段:
  1. `__restrict`(承诺指针不重叠,本例)
  2. **类型别名规则(TBAA)**:不同类型指针默认可假定不重叠(float* vs int*);
     这也是 `-fno-strict-aliasing`(Linux 内核用)会显著变慢的原因
  3. **loop versioning**:编译器生成运行时检查,两版本代码都留着(简单情形有效,
     可用 `-fopt-info-vec` 看到 "loop versioned for vectorization" 字样)
- **违反 restrict 承诺是未定义行为**:承诺了就要做到,否则优化后的代码可能算错
- Go/Rust 没有这个问题:Go 的 slice 语义 + 逃逸分析、Rust 的借用规则
  (`&mut` 独占)在语言层面就排除了大部分别名——这是 Rust 优化潜力的来源之一

## 常见误区

- ❌ 以为 restrict 是"性能提示"可随便加 —— 它是**正确性契约**,撒谎=UB
- ❌ 在 C++ 里指望编译器总能靠运行时检查兜底 —— 边界/多重依赖情形 versioning 会放弃,
  用 `-fopt-info-vec-missed` 检查
- ❌ 到处加 `-fno-strict-aliasing` 求"稳" —— 它关闭了整类优化,应只用于确有类型双关的老代码
