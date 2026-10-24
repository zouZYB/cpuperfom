# 03 间接分支:函数指针/虚调用的预测代价

> 对应笔记:七-3《Avoiding the Cost of Branch Misprediction》(Kapoor, Intel 2009:
> 条件分支与**间接分支**两类误预测)、七-2 TAGE(间接目标预测器 ITTAGE)

## 目的

[01](01-sorted-vs-unsorted.md) 演示的是**条件分支**(if,两个方向二选一)。
还有一类更贵的分支:**间接分支**——`call [target]`、函数指针、C++ 虚调用、
switch 跳转表、Go interface 调用。它的目标地址可以是任意值,预测器要猜的不是
"跳不跳"而是"跳到哪"。本例用同一个调用点对比三种目标分布,实测 **5x** 阶梯。

## 原理

```
s += ops[i](x);   // 编译为间接 call,目标在运行时才知

预测器对间接目标的学习能力:
  单目标       → BTB 直接命中,零误预测
  短周期模式   → ITTAGE 类预测器能用历史模式猜下一目标,但容量有限
  随机多目标   → 无法预测,每次都 ~15-20 拍流水线清空

真实代码中的对应物:
  解释器主循环(opcode 分发)、虚函数多态热路径、状态机跳转表——
  目标分布越"杂",越慢;这正是 Go PGO 反虚拟化(05-06-01)要消除的开销
```

## 代码(main.c)

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 间接分支预测:同一个调用点,目标可预测性决定速度 */

#define N (1<<22)
typedef int (*op_t)(int);
static op_t ops[N];
static int data[N];
static long long sink;

static int f0(int x){return x+1;} static int f1(int x){return x*3+1;}
static int f2(int x){return x^0x5a5a;} static int f3(int x){return x-7;}
static int f4(int x){return x*5-3;} static int f5(int x){return (x>>1)+11;}
static int f6(int x){return x^x>>3;} static int f7(int x){return x*7+13;}
static op_t table[8] = {f0,f1,f2,f3,f4,f5,f6,f7};

static long long run(void){
    long long s=0;
    for(int i=0;i<N;i++) s += ops[i](data[i]);   /* 同一调用点的间接调用 */
    return s;
}

static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

static double bench(void){
    run();
    double best=1e9;
    for(int r=0;r<7;r++){ double t0=now_ms(); sink=run(); double d=now_ms()-t0; if(d<best)best=d; }
    return best;
}

int main(void){
    srand(7);
    for(int i=0;i<N;i++) data[i]=rand();

    for(int i=0;i<N;i++) ops[i]=f0;                          /* A 单目标 */
    printf("A 单目标     : %7.3f ms\n", bench());

    for(int i=0;i<N;i++) ops[i]=table[i&7];                  /* B 周期 8 */
    printf("B 周期模式8  : %7.3f ms\n", bench());

    for(int i=0;i<N;i++) ops[i]=table[rand()&7];             /* C 随机 8 */
    printf("C 随机目标8  : %7.3f ms\n", bench());
    return 0;
}
```

## 运行

```bash
gcc -O2 -o ind.exe main.c
./ind.exe
```

## 预期结果(本仓库开发机实测)

```
A 单目标     :   4.168 ms
B 周期模式8  :  10.237 ms   (2.5x 慢于 A)
C 随机目标8  :  20.680 ms   (5.0x 慢于 A)
```

## 分析要点

- **三段阶梯各自有信息**:
  - A→B(2.5x):周期为 8 的模式**理论上** ITTAGE 类预测器能学会,但本机
    (Zen 4)只学会了一部分——预测器容量是有限的,模式长度、目标数、历史深度
    共同决定学习上限(七-2 TAGE 的几何级历史长度设计就是为了拉长可学周期)
  - B→C(2x):彻底随机的目标完全无法预测,每次间接调用都付全额清空代价
- **消除手段**(按代价从低到高):
  1. **排序/分组**:把同目标的迭代聚到一起(数据层修复,对应 01 的思路)
  2. **PGO 反虚拟化**:热目标提升为直接调用([05-06-01](../05-06-binary-compiler/01-go-pgo-devirtualization.md) 实测 3x)
  3. **去分发**:模板/泛型特化,编译期确定目标(C++ CRTP、Go 泛型)
  4. 解释器场景:threaded code(直接跳转的 opcode 分发)让每个 opcode
     有自己的间接跳转点,各自学习——比单一 switch 分发点好得多
- 用 [01-pmu-uprof](../01-pmu-uprof/01-uprof-counters.md) 的 branch miss 指标
  可以直接看到 C 路的误预测率飙升

## 常见误区

- ❌ 以为"分支预测"只管 if —— 间接分支通常更贵,且更难优化(目标在数据里)
- ❌ 在热路径上用 interface/虚调用"反正没多少开销" —— 单次 ~5ns(见 05-06-01),
  百万次循环就是毫秒级;但**冷路径**上多态的灵活性完全值得,别过度特化
- ❌ 以为预测器万能 —— 它能学规律,学不了真随机;数据分布决定上限
