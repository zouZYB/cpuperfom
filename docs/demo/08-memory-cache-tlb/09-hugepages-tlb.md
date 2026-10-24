# 09 大页(Hugepage):把 TLB 覆盖率扩大 512 倍

> 对应笔记:八-6《Improving TLB Performance by Increasing Hugepage Ratio》(Luo et al.,
> CCGrid 2015)、六-10(代码大页蓝图)。配套实验:[08-tlb-walk-stride](08-tlb-walk-stride.md)
> 展示"病",本例展示"药"。

## 目的

[08](08-tlb-walk-stride.md) 实测了:工作集超过 TLB 覆盖范围后,随机访问延迟从 ~5ns
涨到 ~50ns(page walk 代价)。大页是最直接的解法:**2MB 页让单条 TLB 项的覆盖范围
扩大 512 倍**。本例用同一个 512MB 随机指针链负载对比:

```
4KB 页  → 131072 个页 → 远超 STLB(~2K 项)→ 每次跨页都可能 page walk
2MB 大页 → 256 个页    → STLB 轻松装下    → walk 基本消失
```

> ✅ 本机已实测两路对比:**4.1x**(见"预期结果")。大页需要 `SeLockMemoryPrivilege`
> 特权,授权方法见文末"一次性配置"(本机已配置完成)。

## 原理

```
x86-64 地址翻译:虚拟地址 → 4 级页表 walk(即便页表项被缓存也要十几~几十拍)
TLB 是这翻译结果的缓存:
  L1 DTLB  ~64-72 项  → 4KB 页覆盖 ~256KB
  L2 STLB  ~1536-3072 项 → 4KB 页覆盖 ~6-12MB

512MB 工作集随机访问:131072 页 >> STLB 容量
  → 几乎每次访存都 STLB miss → page walk 叠加在 DRAM 延迟上

换 2MB 页:512MB 只需 256 个 TLB 项
  → STLB 全命中 → 延迟回落到纯缓存/内存层级(见 02-cache-levels-latency)

Windows 大页 API:VirtualAlloc(MEM_LARGE_PAGES),页大小 = GetLargePageMinimum()
(本机 2048KB);前置条件是进程令牌持有并启用 SeLockMemoryPrivilege
(Linux 对应:hugetlbfs / madvise(MADV_HUGEPAGE) / THP)
```

## 代码(main.c)

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>

/* 大页 vs 普通页:同一 512MB 工作集、同一随机指针链负载 */

#define WORKSET (512ULL<<20)   /* 512MB */
#define STEPS   (1<<22)        /* 4M 次追逐 */

static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

/* 启用"锁定内存页"特权(使用大页的前置条件) */
static int enable_lock_privilege(void){
    HANDLE tok;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &tok)) return 0;
    LUID luid;
    if(!LookupPrivilegeValueA(NULL, "SeLockMemoryPrivilege", &luid)){ CloseHandle(tok); return 0; }
    TOKEN_PRIVILEGES tp; tp.PrivilegeCount=1;
    tp.Privileges[0].Luid=luid; tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), NULL, NULL);
    int ok = (GetLastError()==ERROR_SUCCESS);
    CloseHandle(tok);
    return ok;
}

/* 在 base 上铺"一页一节点"的随机页序指针链,返回每步最小 ns */
static double chase(char *base, size_t page_shift){
    size_t page_size = 1ULL<<page_shift;
    size_t npages = WORKSET >> page_shift;
    size_t *perm = malloc(npages*sizeof(size_t));
    for(size_t i=0;i<npages;i++) perm[i]=i;
    srand(12345);
    for(size_t i=npages-1;i>0;i--){ size_t j=(size_t)rand()%(i+1); size_t t=perm[i];perm[i]=perm[j];perm[j]=t; }
    for(size_t i=0;i+1<npages;i++)
        *(size_t*)(base + perm[i]*page_size) = perm[i+1];
    *(size_t*)(base + perm[npages-1]*page_size) = perm[0];

    /* 触摸所有页,确保物理提交(大页尤其需要,否则首次缺页混进计时) */
    for(size_t i=0;i<npages;i++){ volatile char c = base[i*page_size+64]; (void)c; }

    size_t cur = perm[0];
    for(size_t s=0;s<npages;s++) cur = *(size_t*)(base + cur*page_size); /* 预热一圈 */
    double best=1e18;
    for(int r=0;r<5;r++){
        double t0=now_ms();
        for(int s=0;s<STEPS;s++) cur = *(size_t*)(base + cur*page_size);
        double d=(now_ms()-t0)*1e6/STEPS;
        if(d<best) best=d;
    }
    if(cur==0xdeadbeef) printf("!");
    free(perm);
    return best;
}

int main(void){
    printf("LargePageMinimum = %zu KB\n", GetLargePageMinimum()>>10);

    /* 4KB 普通页 */
    char *p4k = VirtualAlloc(NULL, WORKSET, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if(!p4k){ printf("4KB alloc failed\n"); return 1; }
    double t4k = chase(p4k, 12);
    printf("4KB  pages : %7.2f ns/step  (131072 pages)\n", t4k);
    VirtualFree(p4k, 0, MEM_RELEASE);

    /* 2MB 大页 */
    if(!enable_lock_privilege()){ printf("SeLockMemoryPrivilege unavailable - skip large pages\n"); return 0; }
    char *p2m = VirtualAlloc(NULL, WORKSET, MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES, PAGE_READWRITE);
    if(!p2m){ printf("large-page alloc failed (err=%lu)\n", GetLastError()); return 0; }
    double t2m = chase(p2m, 21);
    printf("2MB  pages : %7.2f ns/step  (256 pages)  -> %.1fx\n", t2m, t4k/t2m);
    VirtualFree(p2m, 0, MEM_RELEASE);
    return 0;
}
```

## 运行

```bash
gcc -O2 -o lp.exe main.c
./lp.exe
```

## 预期结果(本仓库开发机实测)

```
LargePageMinimum = 2048 KB
4KB  pages :   19.80 ns/step  (131072 pages)
2MB  pages :    4.80 ns/step  (256 pages)  -> 4.1x
```

读法:

- **19.80 ns/步**:131072 个页远超 STLB 容量,几乎每次跨页追逐都叠加 page walk
- **4.80 ns/步**:256 个页 STLB 全命中,延迟回落到纯缓存层级——与
  [08-tlb-walk-stride](08-tlb-walk-stride.md) 实测的"TLB 内随机追逐 ~4.9ns"精确吻合
- **虚拟化环境也真实受益**:本机是 Hyper-V VM,宿主机显然用真实大页做了 backing;
  若宿主不配合,收益会缩水——云上大内存实例需实测验证,不能照搬

## 一次性配置(授予大页特权,本机已完成)

1. 右键"以管理员身份运行"打开终端,运行 `secpol.msc`
2. 本地策略 → 用户权限分配 → **锁定内存页** → 添加当前用户(或 Administrators 组)
3. **注销并重新登录**(特权在登录时写入令牌,现有进程拿不到)
4. 重跑 `./lp.exe`,此时 `enable_lock_privilege()` 成功,输出两路对比
5. 验证:`whoami /priv` 中出现 `SeLockMemoryPrivilege  锁定内存页  已禁用`
   ("已禁用"是正常默认状态,程序运行时会自行启用)

Linux 等价物(笔记八-6 的原始环境):

```bash
echo 512 > /proc/sys/vm/nr_hugepages        # 预留 512×2MB
echo always > /sys/kernel/mm/transparent_hugepage/enabled   # 或 THP
perf stat -e dTLB-load-misses,iTLB-load-misses -- ./app     # 前后对比
```

## 分析要点

- 大页的收益边界:**只有 TLB 受限的负载有效**。先用 PMU 验证(`-m tlb`,见
  [01-pmu-uprof](../01-pmu-uprof/01-uprof-counters.md)),TLB walk 占比低就别折腾
- 大页是稀缺资源:预留的大页池不可换出、不可他用;数据库/图计算(大内存随机访问)
  受益最大,流式/小工作集负载无感
- 代码大页(I-TLB)是同一思想的另一翼:热代码几十 MB 的服务(数据库、JVM、HHVM)
  把 .text 搬上 2MB 页,配合 BOLT/HFSort 使用(笔记六-10、五-3)——Windows 上无
  成熟机制,Linux 用 libhugetlbfs 或运行时代码重定位
- 虚拟化注意(笔记四-4):guest 大页要宿主配合 backing 才真省 walk——本机 VM 实测
  4.1x 说明宿主配合良好,但这是**需要实测验证**的属性,不是默认假设;
  EPT/NPT 二级翻译下 TLB 压力比裸机更敏感,云上大内存实例常默认开 THP

## 常见误区

- ❌ 不开 PMU 验证就上大页 —— 非 TLB 受限负载零收益,还浪费预留内存
- ❌ 以为分配大页失败是代码问题 —— 99% 是特权未授予或未重新登录;
  `GetLargePageMinimum()` 返回非 0 只说明 CPU 支持,不代表有权用
- ❌ 在虚拟化环境照搬裸机预期 —— 先小工作集验证大页确实生效(对比两路延迟),
  再谈收益
