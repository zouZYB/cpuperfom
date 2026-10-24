// SIMD 实验场：统一的 10 个内核接口，可切换 6 种实现（标量 / 自动向量化 SSE2 /
// 自动向量化 native / 手写 SSE4.1 / AVX2 / AVX-512）。
//
// 坐标约定：所有内核的 n 都是"元素个数"；normalize_aos 的 n 是"点个数"（每个点 3 个 float）。
// 内存约定：同 memcpy，dst 与 src 不允许重叠（原地操作 dst==src 由各实现显式支持）。
#pragma once

#include <cstddef>
#include <cstdint>

namespace simd {

enum class Isa : int {
    Scalar = 0,   // 标量基线：-fno-tree-vectorize，关闭编译器向量化
    AutoSse2,     // -O3（不给 -march）：x86-64 默认只有 SSE2，观察"没开 -march"的自动向量化
    AutoNative,   // -O3 -march=native：完整自动向量化（本机 Zen4 → AVX-512）
    Sse41,        // 手写 SSE4.1（128-bit）
    Avx2,         // 手写 AVX2 + FMA（256-bit）
    Avx512,       // 手写 AVX-512F（512-bit，Zen4 内部 256-bit 双泵）
    Count
};

const char* isa_name(Isa isa);

// 运行期是否可用。Scalar / Auto* 恒为 true（已在当前机器上编译）；其余靠 CPUID 探测。
bool isa_available(Isa isa);

// 每条向量指令处理的 float 数：标量=1，Auto*=0（未知），SSE=4，AVX2=8，AVX-512=16。
size_t isa_vector_width(Isa isa);

// 内核签名。每个 Isa 都提供全部 10 个实现（未手写的路径不会出现在分发表里，
// kernels() 对不可用 ISA 返回标量表，调用前应先用 isa_available() 过滤）。
struct Kernels {
    void    (*add)(float* dst, const float* a, const float* b, size_t n);
    void    (*saxpy)(float* y, const float* x, float alpha, size_t n);
    void    (*relu)(float* dst, const float* src, size_t n);
    float   (*dot)(const float* a, const float* b, size_t n);
    void    (*gather_scale)(float* dst, const float* src, const int32_t* idx, float scale, size_t n);
    void    (*normalize_aos)(float* xyz, size_t npoints);              // AoS: x,y,z,x,y,z...
    void    (*normalize_soa)(float* x, float* y, float* z, size_t n);  // SoA: 三个独立数组
    void    (*add_f64)(double* dst, const double* a, const double* b, size_t n);
    double  (*dot_f64)(const double* a, const double* b, size_t n);
    int32_t (*dot_int8)(const int8_t* a, const int8_t* b, size_t n);   // 累加器为 int32
};

const Kernels& kernels(Isa isa);

}  // namespace simd
