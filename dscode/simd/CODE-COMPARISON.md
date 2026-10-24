# 代码对比与数据对比

本文对同一组内核给出三件事：

1. **源码对比**：标量 C++、自动向量化（同一份源码 + 不同编译开关）、手写 intrinsics；
2. **生成代码对比**：MinGW GCC 16.1 `-O3` 实际产出的内层循环（`objdump -d --no-show-raw-insn`）；
3. **数据对比**：同一机器（Ryzen 7 7800X3D，5.02 GHz）的实测耗时与加速比。

数据表由 [scripts/gen_tables.py](scripts/gen_tables.py) 从 `build/bench.json` 生成，可复现。
所有汇编均从 `build/CMakeFiles/simd_kernels.dir/src/kernels_*.cpp.obj` 反汇编得到。

---

## 0. 六种实现的关系

| 枚举 | 源码 | 编译方式 | 产物形态 |
|---|---|---|---|
| `scalar` | `kernels_generic.inc`（纯 C++） | `-fno-tree-vectorize -fno-tree-slp-vectorize` | 1 元素/循环 |
| `auto_sse2` | **同上，一字不改** | `-O3`（x86-64 默认目标） | SSE2，4 元素/循环 |
| `auto_native` | **同上，一字不改** | `-O3 -march=native` | AVX-512，16 元素/循环 |
| `sse41` | `simd_impl.h` 模板 + `kernels_sse41.cpp` | `-msse4.1` | 手写 128 位 |
| `avx2` | 同上模板 | `-mavx2 -mfma` | 手写 256 位 |
| `avx512` | 同上模板 | `-mavx512f -mavx512bw -mavx512vl -mfma` | 手写 512 位 |

**关键点**：`scalar / auto_sse2 / auto_native` 是**同一份 C++ 源码**，差异只在编译开关；
`hand-written` 是另一份代码（模板 + intrinsics）。下面的对比就是"改编译器"与"改代码"两条路线的对照。

---

## 1. add：最基础的逐元素

### 源码

标量与自动向量化共用下面这段（`src/kernels_generic.inc`）：

```cpp
void add(float* dst, const float* a, const float* b, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = a[i] + b[i];
}
```

手写版本的核心是模板（`include/simd_impl.h`），一次编写、三种位宽实例化：

```cpp
template <class V, class T>
inline void add_impl(float* dst, const float* a, const float* b, size_t n) {
    size_t i = 0;
    for (; i + T::W <= n; i += T::W)
        T::store(dst + i, T::add(T::load(a + i), T::load(b + i)));
    for (; i < n; ++i) dst[i] = a[i] + b[i];        // 尾部标量
}
```

### 生成的代码

**scalar**（1 元素/循环，依赖 1 次 load + 1 次 add）：

```asm
movss  (%rdx,%rax,4),%xmm0
addss  (%r8,%rax,4),%xmm0
movss  %xmm0,(%rcx,%rax,4)
add    $0x1,%rax
cmp    %rax,%r9
jne    ...
```

**auto_sse2**（同一份源码，4 元素/循环；注意进入循环前的运行时别名检查）：

```asm
sub    %rdx,%r10          ; 若 dst 与 a 距离过近（可能重叠）
cmp    $0x8,%r10
jbe    <scalar loop>      ; 就退回标量版本
...
movups (%rdx,%rax,1),%xmm0
movups (%r8,%rax,1),%xmm2
addps  %xmm2,%xmm0
movups %xmm0,(%rcx,%rax,1)
add    $0x10,%rax
cmp    %r11,%rax
jne    ...
```

**auto_native**（同一份源码，16 元素/循环 + 掩码尾部，不再有标量尾巴）：

```asm
vmovups (%r8,%rax,1),%zmm0
vaddps  (%rdx,%rax,1),%zmm0,%zmm0
vmovups %zmm0,(%rcx,%rax,1)
add     $0x40,%rax
...
; 尾部：用 k1 掩码一次处理剩余 0~15 个
vpcmpnleub ...,%xmm0,%k1
vmovups (%rdx,%rax,4),%zmm2{%k1}{z}
vmovups (%r8,%rax,4),%zmm1{%k1}{z}
vaddps  %zmm1,%zmm2,%zmm0{%k1}{z}
vmovups %zmm0,(%rcx,%rax,4){%k1}
```

**手写 avx2 / avx512**（编译器把第二个操作数折叠进 `vaddps` 的内存操作数，3 条指令/向量）：

```asm
; avx2：8 元素/循环
vmovups -0x20(%rdx,%rax,4),%ymm0
vaddps  -0x20(%r8,%rax,4),%ymm0,%ymm0
vmovups %ymm0,-0x20(%rcx,%rax,4)
add     $0x8,%rax

; avx512：16 元素/循环
vmovups -0x40(%rdx,%rax,4),%zmm0
vaddps  -0x40(%r8,%rax,4),%zmm0,%zmm0
vmovups %zmm0,-0x40(%rbx,%rax,4)
add     $0x10,%rax
```

对比要点：

- 手写版本**没有别名检查**（约定不重叠），主循环就是 3 条指令；
- 编译器版本因源码未加 `restrict`，必须插入运行时重叠检查，但检查和尾部都被移出热循环；
- `auto_native` 用 AVX-512 掩码指令消除了标量尾巴（小 N 场景的额外优势）。

### 数据

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 1K | 226.5ns | 69.9ns (3.24×) | 32.9ns (6.88×) | 66.6ns (3.40×) | 36.1ns (6.27×) | 32.9ns (6.88×) |
| 64K | 15.6µs | 5.4µs (2.88×) | 5.1µs (3.08×) | 5.4µs (2.87×) | 5.2µs (3.00×) | 5.1µs (3.05×) |
| 4M | 1.08ms | 520.5µs (2.08×) | 451.5µs (2.40×) | 490.2µs (2.21×) | 419.6µs (2.58×) | 433.9µs (2.50×) |
| 32M | 9.43ms | 7.62ms (1.24×) | 9.27ms (1.02×) | 8.46ms (1.11×) | 8.32ms (1.13×) | 8.15ms (1.16×) |

L1 内 6.3–6.9×，到 L2/L3/DRAM 依次缩到 3× / 2.5× / 1.1×；此时 6 种实现的差距只剩 ±10%，
**说明瓶颈已经从"指令"变成"带宽"**。

---

## 2. saxpy / relu：同族内核的数据

代码形态与 add 相同（saxpy 有 FMA，relu 是 `max(x,0)`），这里只列数据，用于交叉验证"字节数决定加速比"：

**saxpy**（12 B/元素，但 y 先读后写，无 RFO）

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 1K | 253.0ns | 76.1ns (3.32×) | 32.4ns (7.80×) | 80.4ns (3.15×) | 35.2ns (7.19×) | 32.9ns (7.69×) |
| 64K | 15.4µs | 4.8µs (3.21×) | 3.4µs (4.56×) | 4.9µs (3.18×) | 3.4µs (4.48×) | 3.4µs (4.60×) |
| 4M | 1.04ms | 346.1µs (3.02×) | 270.5µs (3.86×) | 349.4µs (2.99×) | 259.9µs (4.02×) | 270.2µs (3.87×) |
| 32M | 9.42ms | 5.15ms (1.83×) | 4.92ms (1.91×) | 5.71ms (1.65×) | 4.80ms (1.96×) | 5.01ms (1.88×) |

**relu**（8 B/元素，流量最小，所以加速比最高）

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 1K | 218.2ns | 67.0ns (3.26×) | 26.2ns (8.34×) | 64.3ns (3.40×) | 36.8ns (5.93×) | 26.5ns (8.22×) |
| 64K | 14.0µs | 4.0µs (3.49×) | 3.4µs (4.09×) | 4.1µs (3.42×) | 3.6µs (3.88×) | 3.5µs (4.01×) |
| 4M | 917.3µs | 306.6µs (2.99×) | 265.9µs (3.45×) | 310.1µs (2.96×) | 273.3µs (3.36×) | 271.9µs (3.37×) |
| 32M | 7.33ms | 5.07ms (1.45×) | 5.37ms (1.36×) | 5.12ms (1.43×) | 5.18ms (1.42×) | 5.38ms (1.36×) |

---

## 3. dot：代码差异最大的一组

### 源码

标量/自动版本（同一份）：

```cpp
float dot(const float* a, const float* b, size_t n) {
    float acc = 0.0f;
    for (size_t i = 0; i < n; ++i) acc += a[i] * b[i];
    return acc;
}
```

手写版本（4 个独立累加器，显式打破依赖链）：

```cpp
template <class V, class T>
inline float dot_impl(const float* a, const float* b, size_t n) {
    V a0 = T::zero(), a1 = T::zero(), a2 = T::zero(), a3 = T::zero();
    for (; i + 4 * T::W <= n; i += 4 * T::W) {
        a0 = T::fmadd(T::load(a + i + 0 * T::W), T::load(b + i + 0 * T::W), a0);
        a1 = T::fmadd(T::load(a + i + 1 * T::W), T::load(b + i + 1 * T::W), a1);
        a2 = T::fmadd(T::load(a + i + 2 * T::W), T::load(b + i + 2 * T::W), a2);
        a3 = T::fmadd(T::load(a + i + 3 * T::W), T::load(b + i + 3 * T::W), a3);
    }
    float s = T::hsum(T::add(T::add(a0, a1), T::add(a2, a3)));
    ...
}
```

### 生成的代码

**scalar**：单累加器 `addss` 串行链，每元素约 2.87 cycle（FADD 延迟）：

```asm
movss  (%rcx,%rax,4),%xmm0
mulss  (%rdx,%rax,4),%xmm0
addss  %xmm0,%xmm1        ; xmm1 是唯一累加器 → 每元素都要等上一条
add    $0x1,%rax
jne    ...
```

**auto_sse2**：乘法被向量化，但归约仍是 4 条串行 `addss`：

```asm
movups (%rcx,%rax,1),%xmm1
movups (%rdx,%rax,1),%xmm3
mulps  %xmm3,%xmm1        ; 4 个乘积一次算完
addss  %xmm1,%xmm0        ; 然后逐个抽 lane 串行加
shufps $0x55,%xmm1,%xmm2
addss  %xmm2,%xmm0
unpckhps %xmm1,%xmm2
addss  %xmm2,%xmm0
addss  %xmm1,%xmm0
```

**auto_native**：zmm 载入 + `vmulps`，然后 **16 条串行 `vaddss`**（这是它比标量还慢的直接原因）：

```asm
vmovups (%rdx,%rax,1),%zmm1
vmulps  (%rcx,%rax,1),%zmm1,%zmm1   ; 16 个乘积
vshufps $0x55,%xmm1,%xmm1,%xmm4
vaddss  %xmm1,%xmm0,%xmm0          ; 串行抽 lane
vaddss  %xmm4,%xmm0,%xmm0
...                                 ; 共 16 条 vaddss
vextractf32x8 $0x1,%zmm1,%ymm1
```

**手写 avx2**：4 个累加器 × 4 条 FMA，每轮 32 个元素，无依赖串行：

```asm
vmovups (%rdx),%ymm4
vmovups 0x20(%rdx),%ymm5
vfmadd231ps -0x80(%rax),%ymm4,%ymm0   ; acc0
vfmadd231ps -0x60(%rax),%ymm5,%ymm2   ; acc1
vmovups -0x40(%rdx),%ymm4
vmovups -0x20(%rdx),%ymm5
vfmadd231ps -0x40(%rax),%ymm4,%ymm1   ; acc2
vfmadd231ps -0x20(%rax),%ymm5,%ymm3   ; acc3
```

### 数据

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 1K | 586.1ns | 588.9ns (1.00×) | 725.8ns (0.81×) | 55.8ns (10.51×) | 28.5ns (20.60×) | 28.5ns (20.58×) |
| 64K | 39.0µs | 42.1µs (0.93×) | 48.4µs (0.81×) | 3.7µs (10.58×) | 3.3µs (11.87×) | 3.3µs (11.98×) |
| 4M | 2.51ms | 2.51ms (1.00×) | 3.07ms (0.82×) | 264.2µs (9.50×) | 254.8µs (9.85×) | 253.8µs (9.89×) |
| 32M | 20.11ms | 20.08ms (1.00×) | 24.65ms (0.82×) | 4.14ms (4.86×) | 4.50ms (4.47×) | 4.87ms (4.13×) |

**代码 → 数据的一一对应**：

- 串行 `addss` 链（scalar）：2.87 cycle/元素；
- 向量乘 + 串行抽 lane（auto_*）：每 4/16 个元素仍有 4/16 级依赖，**比标量还慢**（0.81×）；
- 4 累加器 + FMA（手写）：0.14 cycle/元素 → 20.6×，**超过向量宽度**——优化的是延迟，不是指令数；
- 到 32M（DRAM）后手写也掉到 4–5×：此时带宽成为新瓶颈。

---

## 4. int8 点积：指令数差 30 倍

### 源码

```cpp
// 标量
int32_t dot_int8(const int8_t* a, const int8_t* b, size_t n) {
    int64_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc += (int32_t)a[i] * (int32_t)b[i];
    return (int32_t)acc;
}

// 手写 AVX2：32 字节/轮 = 2×(widen + madd)
__m256i va = _mm256_loadu_si256((const __m256i*)(a + i));
__m256i vb = _mm256_loadu_si256((const __m256i*)(b + i));
__m256i alo = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(va));      // 低 16 字节 → 16×int16
__m256i ahi = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(va, 1)); // 高 16 字节
acc = _mm256_add_epi32(acc, _mm256_madd_epi16(alo, blo));
acc = _mm256_add_epi32(acc, _mm256_madd_epi16(ahi, bhi));
```

### 生成的代码

**scalar**（每元素：2 次符号扩展 + 1 次乘法 + 1 次 64 位加）：

```asm
movsbl (%rcx,%r10,1),%r9d
movsbl (%rdx,%r10,1),%r11d
imul   %r11d,%r9d
movslq %r9d,%r9
add    %r9,%rax
```

**手写 avx2**（每 32 字节：2 条 load + 4 条 widen + 2 条 madd + 2 条 add）：

```asm
vmovdqu -0x20(%rcx,%rax,1),%ymm0
vmovdqu -0x20(%r9,%rax,1),%ymm1
vpmovsxbw %xmm0,%ymm2
vpmovsxbw %xmm1,%ymm5
vpmaddwd %ymm5,%ymm2,%ymm2
vpmaddwd %ymm1,%ymm0,%ymm0
vpaddd   %ymm2,%ymm0,%ymm0
```

### 数据

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 4K | 1.8µs | 588.0ns (3.03×) | 341.4ns (5.22×) | 183.8ns (9.69×) | 93.5ns (19.06×) | 91.6ns (19.44×) |
| 64K | 28.5µs | 9.4µs (3.04×) | 5.5µs (5.17×) | 2.9µs (9.74×) | 1.5µs (18.96×) | 1.6µs (17.95×) |
| 4M | 1.83ms | 602.3µs (3.03×) | 340.3µs (5.37×) | 187.5µs (9.74×) | 99.4µs (18.38×) | 97.0µs (18.84×) |

标量每元素 2.18 cycle，SIMD 降到 0.11 cycle；**19× 超过 16 宽**，因为标量还要付符号扩展的代价。
`auto_sse2` 只有 3×：编译器对 int8 的符号扩展处理保守得多。

---

## 5. normalize：布局决定指令，指令决定性能

### 源码

```cpp
// 标量（AoS：x,y,z 交错存储）
void normalize_aos(float* xyz, size_t npoints) {
    for (size_t i = 0; i < npoints; ++i) {
        float x = xyz[3*i], y = xyz[3*i+1], z = xyz[3*i+2];
        float len = __builtin_sqrtf(x*x + y*y + z*z);
        float inv = 1.0f / len;
        xyz[3*i] = x*inv; xyz[3*i+1] = y*inv; xyz[3*i+2] = z*inv;
    }
}

// 手写 AVX2：gather 取 x/y/z，算完标量写回
for (size_t k = 0; k < T::W; ++k) idxbuf[k] = (int32_t)(3 * (i + k));
V xv = T::gather(xyz + 0, idxbuf);
V yv = T::gather(xyz + 1, idxbuf);
V zv = T::gather(xyz + 2, idxbuf);
...
for (size_t k = 0; k < T::W; ++k) { xyz[3*(i+k)+0] = tx[k]; ... }
```

### 生成的代码

**手写 avx2**：3 条 gather + 标量散写（收益被吃掉）：

```asm
vgatherdps %ymm5,(%r8,%ymm6,4),%ymm8    ; x
vgatherdps %ymm5,(%rsi,%ymm6,4),%ymm1   ; y
vgatherdps %ymm5,(%rbx,%ymm6,4),%ymm9   ; z
...
vmovss %xmm0,0x8(%r8,%rax,1)            ; 逐个标量写回
```

**auto_native**：编译器完全没走 gather，而是用跨 lane 排列把 AoS 拆成 SoA：

```asm
vmovups (%rax),%zmm5          ; 一次读 3×64B = 48 个 float = 16 个点
vmovups 0x40(%rax),%zmm3
vmovups 0x80(%rax),%zmm4
vpermd   %zmm3,%zmm26,%zmm17  ; 按索引重排，拼出 x 向量
vpermt2ps %zmm3,%zmm27,%zmm28
vpermt2ps %zmm4,%zmm25,%zmm16
...
vsqrtps %zmm1,%zmm1
vdivps  %zmm1,%zmm18,%zmm1    ; 16 路 sqrt+div 一次算完
vmovups %zmm1,-0x80(%rax)     ; 连续写回
```

### 数据

**normalize_soa**（手写 avx512 与 auto_native 都走连续访问）

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 1K | 1.8µs | 518.4ns (3.39×) | 230.1ns (7.65×) | 471.7ns (3.73×) | 231.0ns (7.62×) | 221.3ns (7.95×) |
| 64K | 112.7µs | 29.5µs (3.82×) | 14.9µs (7.58×) | 30.4µs (3.70×) | 15.3µs (7.36×) | 14.7µs (7.64×) |
| 4M | 7.29ms | 1.93ms (3.77×) | 940.4µs (7.75×) | 1.89ms (3.85×) | 978.3µs (7.45×) | 937.3µs (7.78×) |

**normalize_aos**（手写 gather 版本反而不如编译器）

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 1K | 1.9µs | 1.4µs (1.36×) | 675.9ns (2.76×) | 1.1µs (1.67×) | 1.7µs (1.11×) | 1.2µs (1.59×) |
| 64K | 118.6µs | 86.5µs (1.37×) | 41.9µs (2.83×) | 71.9µs (1.65×) | 107.2µs (1.11×) | 75.3µs (1.58×) |
| 4M | 7.74ms | 5.65ms (1.37×) | 2.69ms (2.88×) | 4.58ms (1.69×) | 6.87ms (1.13×) | 4.81ms (1.61×) |

同一份标量源码，**AoS→SoA 只是换布局，加速比从 1.1× 变成 7.6×**；
而手写 gather 版本（1.11×）输给编译器的排列方案（2.83×）**1.8 倍**。

---

## 6. gather：代码再花哨也没用

### 源码

```cpp
// 标量
for (size_t i = 0; i < n; ++i) dst[i] = src[idx[i]] * scale;

// 手写 AVX2（模板内）
T::store(dst + i, T::mul(T::gather(src, idx + i), vs));
// T::gather 展开为：_mm256_i32gather_ps(base, indices, 4)
```

### 生成的代码

```asm
vgatherdps %ymm5,(%r8,%ymm6,4),%ymm8   ; 8 个随机 load 合并成一条指令
vmulps ...
vmovups ...
```

### 数据

| 规模 | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 4K | 1.9µs | 1.9µs (1.00×) | 1.9µs (1.02×) | 1.8µs (1.04×) | 1.9µs (1.03×) | 1.8µs (1.05×) |
| 64K | 41.4µs | 41.3µs (1.00×) | 41.2µs (1.00×) | 40.1µs (1.03×) | 40.2µs (1.03×) | 39.9µs (1.04×) |
| 1M | 663.4µs | 663.2µs (1.00×) | 665.4µs (1.00×) | 641.9µs (1.03×) | 640.8µs (1.04×) | 640.1µs (1.04×) |

指令从"每元素 1 次 load"变成"8 个 load 一条指令"，数据却只快 4%：
**吞吐被随机访存的延迟/并行度卡死，指令数不再是瓶颈**。

---

## 7. fp64：同代码，收益减半

| 内核 | 规模 | scalar | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| add_f64 | 1K | 231.3ns | 65.3ns (3.54×) | 133.4ns (1.73×) | 63.1ns (3.66×) | 60.9ns (3.80×) |
| add_f64 | 64K | 18.0µs | 12.1µs (1.49×) | 13.8µs (1.30×) | 12.3µs (1.46×) | 12.3µs (1.47×) |
| add_f64 | 4M | 1.47ms | 1.15ms (1.27×) | 1.24ms (1.18×) | 1.08ms (1.36×) | 1.17ms (1.25×) |
| dot_f64 | 1K | 587.3ns | 620.7ns (0.95×) | 154.0ns (3.81×) | 83.1ns (7.07×) | 56.8ns (10.34×) |
| dot_f64 | 64K | 39.1µs | 41.0µs (0.95×) | 10.1µs (3.87×) | 8.0µs (4.91×) | 7.5µs (5.21×) |
| dot_f64 | 4M | 2.52ms | 2.62ms (0.96×) | 669.6µs (3.76×) | 542.0µs (4.65×) | 561.9µs (4.48×) |

同样的代码，`add_f64` 只有 `add` 的约一半加速比（3.8× vs 6.9×），
`dot_f64` 只有 `dot` 的一半（10.3× vs 20.6×）——**因为 512 位只有 8 个 fp64 通道，而 fp32 有 16 个**。
`dot_f64` 的 auto 版本同样失效（0.95–0.96×），原因与 fp32 归约相同。

---

## 8. 边界：小 N 与未对齐

### 小 N：SIMD 什么时候回本（avx2 为例）

| n | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |
|---|---|---|---|---|---|---|
| 1 | 1.0ns | 1.8ns (0.55×) | 1.7ns (0.57×) | 1.7ns (0.60×) | 2.0ns (0.50×) | 2.2ns (0.45×) |
| 2 | 1.2ns | 1.8ns (0.66×) | 1.8ns (0.69×) | 1.7ns (0.71×) | 1.8ns (0.67×) | 1.9ns (0.65×) |
| 3 | 1.4ns | 1.7ns (0.84×) | 1.7ns (0.84×) | 1.8ns (0.78×) | 2.1ns (0.68×) | 2.1ns (0.68×) |
| 7 | 2.3ns | 1.8ns (1.24×) | 1.8ns (1.29×) | 2.0ns (1.13×) | 2.3ns (1.01×) | 2.8ns (0.82×) |
| 8 | 2.5ns | 1.7ns (1.51×) | 1.8ns (1.43×) | 1.6ns (1.57×) | 1.4ns (1.80×) | 2.0ns (1.26×) |
| 16 | 4.2ns | 2.4ns (1.77×) | 1.6ns (2.59×) | 2.1ns (2.05×) | 1.7ns (2.54×) | 1.6ns (2.58×) |
| 33 | 8.3ns | 3.6ns (2.31×) | 2.5ns (3.26×) | 3.4ns (2.44×) | 2.3ns (3.61×) | 2.5ns (3.24×) |

N≤3 全面亏本，N≈7–8 才回本。`auto_native` 因为用掩码处理尾部，小 N 下比手写 avx2 更稳
（N=16 时 2.59× vs 2.54×，N=7 时 1.29× vs 1.01×）。

### 未对齐（off = 指针偏移的 float 个数）

| n | off | scalar | avx2 | avx512 |
|---|---|---|---|---|
| 4K | 0 | 1.0µs | 308.8ns (3.24×) | 299.9ns (3.34×) |
| 4K | 1 | 997.1ns | 349.5ns (2.85×) | 340.8ns (2.93×) |
| 4K | 2 | 999.7ns | 350.1ns (2.86×) | 338.9ns (2.95×) |
| 4K | 3 | 998.8ns | 349.4ns (2.86×) | 338.9ns (2.95×) |
| 64K | 0 | 15.4µs | 5.2µs (2.98×) | 5.1µs (3.03×) |
| 64K | 1 | 15.6µs | 5.1µs (3.07×) | 5.4µs (2.89×) |
| 64K | 2 | 15.4µs | 5.1µs (3.03×) | 5.4µs (2.89×) |
| 64K | 3 | 15.4µs | 5.2µs (2.98×) | 5.6µs (2.74×) |

代码完全相同（`loadu/storeu`），只是地址偏移 4–12 字节：L1 档损失约 12%，
L2 档 avx512 损失 5–10%、avx2 基本无影响。

---

## 9. 同一份源码，两种编译：编译器到底做了什么

`auto_sse2` 与 `auto_native` 是**逐字节相同**的 C++，只是后者多了 `-march=native`：

| 内核 | 1K auto_sse2 | 1K auto_native | 指令数变化 |
|---|---|---|---|
| add | 69.9ns | 32.9ns | 4→16 元素/迭代，1 条 `vaddps` 替代 1 条 `addps`，尾部改掩码 |
| dot | 588.9ns | 725.8ns | 同样 16 路宽，但归约仍是串行 → **变慢 23%** |
| normalize_aos | 1.4µs | 675.9ns | 从"标量拆包"变成 `vpermd` 重排 + 16 路 sqrt/div |
| int8 dot | 588.0ns | 341.4ns | 128 位 widen 变成 512 位 widen |

这张表说明：**开 `-march=native` 不是无脑变快**——对逐元素是正收益，对归约是负收益
（更宽的向量让串行归约的每轮开销更大），对特殊布局则取决于编译器能否找到好策略。

---

## 10. 一页总结：源码 → 指令 → 数据

| 场景 | 标量指令 | 最优实现指令 | 数据（1K） | 结论 |
|---|---|---|---|---|
| add | `movss/addss/movss` | `vmovups/vaddps/vmovups` | 6.88× | 受 L1 带宽限制，宽度收益递减 |
| dot | `mulss/addss` 串行链 | 4×`vfmadd231ps` 累加器 | 20.60× | 打破依赖链比加宽更重要 |
| int8 dot | `movsbl/imul/add` | `vpmovsxbw/vpmaddwd` | 19.44× | 位宽利用率最高，收益最大 |
| normalize SoA | 标量 sqrt/div | 16 路 `vsqrtps/vdivps` | 7.95× | 计算密集，不随规模衰减 |
| normalize AoS | 标量 sqrt/div | 编译器 `vpermd` 重排 | 2.88× | 手写 gather 反而更慢（1.11×） |
| gather | 标量 load | `vgatherdps` | 1.05× | 延迟受限，ISA 无效 |
| add_f64 | `movsd/addsd` | `vmovupd/vaddpd` | 3.80× | 通道减半，收益减半 |
| tiny add | 标量 | 任意 SIMD | 0.45–0.68× | N<8 净亏 |

**三条可以直接落地的对应关系**：

1. **指令数不是瓶颈时，换宽度无用**（add 从 1.1× 到 6.9× 就封顶在带宽）；
2. **依赖链是瓶颈时，加宽甚至更糟**（dot 的 auto 版本 0.81×，手写 4 累加器 20.6×）；
3. **访问模式不友好时，指令再强也被抵消**（AoS 1.1×、gather 1.05×）。

复现数据表：

```powershell
python scripts/gen_tables.py build/bench.json
```
