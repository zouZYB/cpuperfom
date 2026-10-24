# 03 测量偏差:链接顺序、环境变量与地址布局(Mytkowicz 复现尝试)

> 对应笔记:二-3《Producing Wrong Data Without Doing Anything Obviously Wrong!》
> (Mytkowicz et al., ASPLOS 2009)、二-4 STABILIZER(Curtsinger & Berger)

## 目的

Mytkowicz 那篇论文的标题就是结论:**没改任何代码,只改链接顺序或环境变量大小,
同一 benchmark 的速度就能差 ±10~16%**——因为这些都改变了代码/栈的绝对地址,
进而改变分支预测器表项冲突、4K 别名等微架构行为。它的方法论推论很扎心:
**单一配置下测出的 <5% 性能差异,可能根本不是你的改动带来的。**

本例在本机(Zen 4)认真复现了论文的三类偏差实验,结果是**全部免疫**——
这个"阴性结果"本身有信息量,也示范了验证测量偏差的标准动作。

## 实验一:链接顺序(经典复现)

320 个 padding 函数 + 同一个热循环 .o,两种链接顺序:

```bash
gcc -O2 hot.o pad1.o ... pad8.o -o a.exe      # hot 链在最前
gcc -O2 pad1.o ... pad8.o hot.o -o b.exe      # hot 链在最后(地址整体平移)
```

**本机实测**:

```
链接顺序 A: 0.623 ms   (sink=879949555205002)
链接顺序 B: 0.623 ms   (sink=879949555205002)   ← 完全相同
```

## 实验二:4K 别名探针(机制级)

论文里偏差的主要机制之一:store 与 load 地址低 12 位相同时发生"假依赖"
(4K aliasing)。环境变量改变初始栈内容 → 栈上数组绝对地址平移 → 别名条件开/关。
不依赖 ASLR 撞运气,直接在程序内精确扫描 src/dst 的页内相对偏移:

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SZ (1<<14)
static char arena[(SZ*4)*2 + 8192] __attribute__((aligned(4096)));
static long long sink;
static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

__attribute__((noinline)) static long long copy_sum(const int *src, int *dst){
    long long s=0;
    for(int r=0;r<64;r++)
        for(int i=0;i<SZ;i++){ dst[i]=src[i]+1; s+=dst[i]; }
    return s;
}

int main(void){
    int *dst = (int*)arena;                            /* 4KB 对齐 */
    for(int off=0; off<=128; off+=4){                /* 细扫"危险区" */
        int *src = (int*)(arena + (SZ*4) + 4096 + off);
        for(int i=0;i<SZ;i++) src[i]=i;
        copy_sum(src,dst);
        double best=1e9;
        for(int r=0;r<5;r++){ double t0=now_ms(); sink=copy_sum(src,dst); double d=now_ms()-t0; if(d<best)best=d; }
        printf("off=%4d  %6.2f ms\n", off, best);
    }
    return 0;
}
```

**本机实测**:off 从 0 到 4096(先 256 步长粗扫,再 4 步长细扫危险区)
全部落在 **0.30~0.34ms**,无悬崖。Zen 4 的内存消歧会多比较地址位,
基本不吃 4K 别名这一套(这是 Intel 老架构的经典陷阱,zen 上罕见)。

## 实验三:循环对齐

```bash
gcc -O2 -falign-loops=1  hot.c -o h1.exe     # 0.625 ms
gcc -O2 -falign-loops=64 hot.c -o h64.exe    # 0.623 ms
```

**本机实测**:0.623~0.625ms,免疫——热循环整个驻留在 op cache(uop 缓存)里,
取指对齐不再影响供给。

## 分析要点

- **阴性结果的正确读法**:不是"Mytkowicz 过时了",而是"偏差的**大小**随
  微架构世代大幅变化"。2009 年的 Core 2 上 ±16%,2024 年的 Zen 4 上这三类
  都测不出。但偏差这个**类别**不会消失——下一款芯片、下一个内核、下一次
  微码更新都可能复活它
- Windows 上环境变量实验还有个额外障碍:ASLR 每次运行都随机化栈基址,
  偏差从"系统性"变成"随机性"——这其实是 STABILIZER(二-4)思想的操作系统版:
  **随机化布局,让偏差摊成噪声**,而不是侥幸地固定在好布局上
- 论文真正的遗产是行动清单:
  1. **<5% 的结论先怀疑测量**:换链接顺序/换个机器/加 dummy 环境变量重测
  2. **多次运行取分布**,别看单次([01-min-vs-mean](01-min-vs-mean.md))
  3. **跨机器验证**:偏差是机器特异性的,两台机器上方向一致的差异才可信
  4. 重要决策用**随机化布局平均**(Stabilizer 思路;Go 1.20+ 的
     `-ldflags=-randlayout` 也是同源防御)
- 本仓库 CI 平台的设计已内置这些防御:ABAB 交替跑(消除环境漂移)、
  统计检验而不是裸对比(见 M2 对比引擎)

## 常见误区

- ❌ "我什么都没改,数字变了一定是抖动" —— 可能是地址布局变化;先排除再归因
- ❌ 在本机复现不出就否认偏差存在 —— 偏差是条件触发的,本实验恰好全免疫;
  正确结论是"本机对这三类稳健",不是"偏差不存在"
- ❌ 为防偏差把所有 buffer 手工对齐 —— 对齐解决一类别名,解决不了 BTB 冲突等
  其他机制;方法论防御(多次/多配置/随机化)才覆盖全类别
