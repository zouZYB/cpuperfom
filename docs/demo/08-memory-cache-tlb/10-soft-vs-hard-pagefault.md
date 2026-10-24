# 10 软缺页 vs 硬缺页:内存层级的最后一层是磁盘

> 对应笔记:八-章导言(内存层次)、八-6(TLB 与大页的系统层视角)。
> 本目录延迟链的收官:寄存器 → L1/L2/L3 → DRAM → **页缓存(软缺页)→ 磁盘(硬缺页)**。

## 目的

访问一个"还没有物理页 backing"的虚拟地址会触发**缺页异常(page fault)**,操作系统
接管处理,分两种:

```
软缺页(soft/minor fault):页其实已经在物理内存里
  - demand-zero:首次触摸新提交的私有页,内核拿一个空闲页清零给你
  - 文件页在系统缓存:只需把它映射进你的页表
  成本 = 一次内核往返 + 页表更新 ≈ 几百 ns ~ 1µs

硬缺页(hard/major fault):页不在内存,必须从磁盘读(pagefile 或映射文件)
  成本 = 一次块设备 IO ≈ NVMe 几 µs ~ HDD 几 ms
  → 比软缺页贵 1~4 个数量级
```

本例用同一个 512MB、同一个随机页序、每页读 1 字节,三路对比,并用
`GetProcessMemoryInfo` 的 **PageFaultCount** 证明每页恰好缺页一次。

## 原理

```
内存层级延迟链(本目录全部实测,约值):
  L1 命中      ~1 ns      (02-cache-levels-latency)
  L3 命中      ~14 ns     (同上)
  DRAM         ~63 ns     (同上)
  + TLB walk   +30~50 ns  (08-tlb-walk-stride;大页可消,见 09)
  软缺页        ~700 ns    ← 本例 A/B
  硬缺页        ~9 µs      ← 本例 C(VM 虚拟 NVMe;HDD 上为 ms 级)

关键陷阱:硬缺页是"同步等待磁盘",期间线程被切走——
  mmap 读文件、内存超售触发的换页,延迟毛刺的根源都在这。
```

## 代码(main.c)

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <psapi.h>

/* 软缺页 vs 硬缺页:同一 512MB、同一随机页序、每页读 1 字节
   A. demand-zero 软缺页:VirtualAlloc 提交的私有页,首次触摸
   B. 热文件映射软缺页:文件页已在系统缓存,缺页只需建映射
   C. 冷文件映射硬缺页:文件用 FILE_FLAG_NO_BUFFERING 写出(绕过缓存),
      缺页必须从磁盘读 —— 每次都等 IO                                       */

#define FILESZ (512ULL<<20)
#define PGSZ   4096
#define NPAGES (FILESZ/PGSZ)

static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

static DWORDLONG faults(void){
    PROCESS_MEMORY_COUNTERS pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    return pmc.PageFaultCount;
}

/* 用无缓冲写创建 512MB 文件(数据不进系统缓存 → 之后映射它必然硬缺页) */
static int write_nobuf(const char *path){
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH, NULL);
    if(h==INVALID_HANDLE_VALUE){ printf("create failed err=%lu\n", GetLastError()); return 0; }
    char *buf = VirtualAlloc(NULL, 1<<20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    memset(buf, 0x5a, 1<<20);
    DWORD nw;
    for(int i=0;i<512;i++) if(!WriteFile(h, buf, 1<<20, &nw, NULL)){ printf("write failed\n"); return 0; }
    CloseHandle(h);
    VirtualFree(buf, 0, MEM_RELEASE);
    return 1;
}

static double touch(char *base, size_t *perm){
    volatile unsigned long long s = 0;
    double t0 = now_ms();
    for(size_t i=0;i<NPAGES;i++) s += (unsigned char)base[perm[i]*PGSZ];
    double d = (now_ms()-t0)*1e6/NPAGES; /* ns/页 */
    if(s==0xdeadbeef) printf("!");
    return d;
}

int main(void){
    const char *path = "C:/temp/pf/test.bin";
    size_t *perm = malloc(NPAGES*sizeof(size_t));
    for(size_t i=0;i<NPAGES;i++) perm[i]=i;
    srand(999);
    for(size_t i=NPAGES-1;i>0;i--){ size_t j=(size_t)rand()%(i+1); size_t t=perm[i];perm[i]=perm[j];perm[j]=t; }

    /* A. demand-zero 软缺页 */
    char *priv = VirtualAlloc(NULL, FILESZ, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    DWORDLONG f0 = faults();
    double ta = touch(priv, perm);
    printf("A 私有页首次触摸(软缺页·demand-zero): %9.1f ns/页  (缺页 %llu 次)\n",
           ta, faults()-f0);
    VirtualFree(priv, 0, MEM_RELEASE);

    /* 写无缓冲文件(缓存保持冷) */
    if(!write_nobuf(path)) return 1;

    /* C. 冷文件映射 → 硬缺页 */
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE m = CreateFileMappingA(h, NULL, PAGE_READONLY, 0, 0, NULL);
    char *base = MapViewOfFile(m, FILE_MAP_READ, 0, 0, FILESZ);
    f0 = faults();
    double tc = touch(base, perm);
    printf("C 冷文件映射(硬缺页·磁盘读):        %9.1f ns/页  (缺页 %llu 次)\n",
           tc, faults()-f0);
    UnmapViewOfFile(base); CloseHandle(m); CloseHandle(h);

    /* B. 同一文件再映射(页已在缓存)→ 软缺页 */
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    m = CreateFileMappingA(h, NULL, PAGE_READONLY, 0, 0, NULL);
    base = MapViewOfFile(m, FILE_MAP_READ, 0, 0, FILESZ);
    f0 = faults();
    double tb = touch(base, perm);
    printf("B 热文件映射(软缺页·缓存命中):      %9.1f ns/页  (缺页 %llu 次)\n",
           tb, faults()-f0);
    UnmapViewOfFile(base); CloseHandle(m); CloseHandle(h);

    printf("\n硬/软 = %.0fx\n", tc/tb);
    return 0;
}
```

## 运行

```bash
gcc -O2 -o pf.exe main.c -lpsapi
./pf.exe        # 会自动创建 C:\temp\pf\test.bin(512MB,可事后删除)
```

## 预期结果(本仓库开发机实测,两次运行均稳定)

```
A 私有页首次触摸(软缺页·demand-zero):     779.2 ns/页  (缺页 131329 次)
C 冷文件映射(硬缺页·磁盘读):           8897.0 ns/页  (缺页 131328 次)
B 热文件映射(软缺页·缓存命中):          712.7 ns/页  (缺页 131328 次)

硬/软 = 12x
```

读法:

- 三路缺页次数都是 **131328 ≈ 512MB/4KB** —— 每页恰好缺页一次,计时全花在缺页处理上,
  这是"计时差=缺页成本差"的直接证据
- **A ≈ B(~700ns)**:软缺页的两种形态成本相当——内核往返 + 页表更新是主项,
  清零 4KB / 建缓存页映射的额外开销都被掩盖
- **C = 12x(~8.9µs)**:每次都等一次随机 4KB 磁盘读(本机为 Hyper-V 虚拟 NVMe);
  顺序访问会触发预读聚合从而便宜得多,**随机页序是硬缺页的最坏形态**——
  这正是数据库"随机 IO 恐惧症"的根源
- HDD 上同样实验硬缺页会是 **ms 级**(再贵 100 倍)

## 分析要点

- **mmap 不是"免费读文件"**:mmap 把 read 的显式 IO 变成了隐式缺页 IO——
  冷数据上 mmap 遍历 = 一页一次同步磁盘读,没有并发;显式 `ReadFile` 可以排队、
  可以异步。这就是数据库自管 buffer pool 而不用 mmap 的原因(笔记九-15 的同源话题)
- **内存超售的代价**:工作集超过物理内存时,每次访问都可能是"换入"硬缺页——
  延迟从 100ns 级直接跳到 µs~ms 级,毛刺呈断崖而非渐变;监控上对应
  hard faults/sec(Windows 性能计数器 `Memory\Page Reads/sec`)
- 与 09 大页联动:大页把 512MB 的缺页**次数**从 131072 降到 256(页越大单次缺页
  越贵但次数越少),demand-zero 场景首次触摸成本可降一个数量级
- Linux 观测:`/proc/self/stat` 的 minflt/majflt、`ps -o min_flt,maj_flt`、
  `perf stat -e page-faults,minor-faults,major-faults`
- 注意顺序:C 必须先跑(冷),B 后跑(已被 C 暖进缓存);文件用
  `FILE_FLAG_NO_BUFFERING` 写出就是为了保证 C 跑时缓存是冷的

## 常见误区

- ❌ 把"已提交内存"当成"已在物理内存" —— Windows 的 MEM_COMMIT / Linux 的
  malloc 都只是承诺,**首次触摸才分配物理页**(demand-zero),实时系统要预热
- ❌ 用 mmap 遍历冷数据还奇怪为什么这么慢 —— 每页一次同步 IO,且无并发;
  要么预热(`PrefetchVirtualMemory` / `readahead`),要么换异步 IO
- ❌ 看平均值漏掉硬缺页 —— 1000 次访问里 1 次硬缺页就把均值拉高 10 倍,
  p99/p999 才能看见它(呼应 [02-01](../02-benchmarking/01-min-vs-mean.md) 的分布思维)
- ❌ 把 PageFaultCount 高当成问题 —— 软缺页是正常机制(demand-zero 每次都要),
  只有**硬缺页率**才是危险信号
