// 运行期 ISA 探测 + 分发表。
// 关键点：所有实现都在同一个二进制里，靠 CPUID 选路——这正是生产代码
// "一次编译、多代 CPU 通吃" 的标准做法（对比 -march=native 只能本机跑）。
#include "simd_kernels.h"

#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#define SIMD_HAVE_X86 1
#endif

namespace simd {
namespace scalar {
void add(float*, const float*, const float*, size_t);
void saxpy(float*, const float*, float, size_t);
void relu(float*, const float*, size_t);
float dot(const float*, const float*, size_t);
void gather_scale(float*, const float*, const int32_t*, float, size_t);
void normalize_aos(float*, size_t);
void normalize_soa(float*, float*, float*, size_t);
void add_f64(double*, const double*, const double*, size_t);
double dot_f64(const double*, const double*, size_t);
int32_t dot_int8(const int8_t*, const int8_t*, size_t);
}  // namespace scalar

namespace auto_sse2 {
void add(float*, const float*, const float*, size_t);
void saxpy(float*, const float*, float, size_t);
void relu(float*, const float*, size_t);
float dot(const float*, const float*, size_t);
void gather_scale(float*, const float*, const int32_t*, float, size_t);
void normalize_aos(float*, size_t);
void normalize_soa(float*, float*, float*, size_t);
void add_f64(double*, const double*, const double*, size_t);
double dot_f64(const double*, const double*, size_t);
int32_t dot_int8(const int8_t*, const int8_t*, size_t);
}  // namespace auto_sse2

namespace auto_native {
void add(float*, const float*, const float*, size_t);
void saxpy(float*, const float*, float, size_t);
void relu(float*, const float*, size_t);
float dot(const float*, const float*, size_t);
void gather_scale(float*, const float*, const int32_t*, float, size_t);
void normalize_aos(float*, size_t);
void normalize_soa(float*, float*, float*, size_t);
void add_f64(double*, const double*, const double*, size_t);
double dot_f64(const double*, const double*, size_t);
int32_t dot_int8(const int8_t*, const int8_t*, size_t);
}  // namespace auto_native

#ifdef SIMD_HAVE_X86
namespace sse41 {
void add(float*, const float*, const float*, size_t);
void saxpy(float*, const float*, float, size_t);
void relu(float*, const float*, size_t);
float dot(const float*, const float*, size_t);
void gather_scale(float*, const float*, const int32_t*, float, size_t);
void normalize_aos(float*, size_t);
void normalize_soa(float*, float*, float*, size_t);
void add_f64(double*, const double*, const double*, size_t);
double dot_f64(const double*, const double*, size_t);
int32_t dot_int8(const int8_t*, const int8_t*, size_t);
}  // namespace sse41

namespace avx2 {
void add(float*, const float*, const float*, size_t);
void saxpy(float*, const float*, float, size_t);
void relu(float*, const float*, size_t);
float dot(const float*, const float*, size_t);
void gather_scale(float*, const float*, const int32_t*, float, size_t);
void normalize_aos(float*, size_t);
void normalize_soa(float*, float*, float*, size_t);
void add_f64(double*, const double*, const double*, size_t);
double dot_f64(const double*, const double*, size_t);
int32_t dot_int8(const int8_t*, const int8_t*, size_t);
}  // namespace avx2

namespace avx512 {
void add(float*, const float*, const float*, size_t);
void saxpy(float*, const float*, float, size_t);
void relu(float*, const float*, size_t);
float dot(const float*, const float*, size_t);
void gather_scale(float*, const float*, const int32_t*, float, size_t);
void normalize_aos(float*, size_t);
void normalize_soa(float*, float*, float*, size_t);
void add_f64(double*, const double*, const double*, size_t);
double dot_f64(const double*, const double*, size_t);
int32_t dot_int8(const int8_t*, const int8_t*, size_t);
}  // namespace avx512

#endif

namespace {

Kernels table_scalar = {scalar::add, scalar::saxpy, scalar::relu, scalar::dot,
                        scalar::gather_scale, scalar::normalize_aos, scalar::normalize_soa,
                        scalar::add_f64, scalar::dot_f64, scalar::dot_int8};
Kernels table_auto_sse2 = {auto_sse2::add, auto_sse2::saxpy, auto_sse2::relu, auto_sse2::dot,
                           auto_sse2::gather_scale, auto_sse2::normalize_aos, auto_sse2::normalize_soa,
                           auto_sse2::add_f64, auto_sse2::dot_f64, auto_sse2::dot_int8};
Kernels table_auto_native = {auto_native::add, auto_native::saxpy, auto_native::relu, auto_native::dot,
                             auto_native::gather_scale, auto_native::normalize_aos, auto_native::normalize_soa,
                             auto_native::add_f64, auto_native::dot_f64, auto_native::dot_int8};

#ifdef SIMD_HAVE_X86
Kernels table_sse41 = {sse41::add, sse41::saxpy, sse41::relu, sse41::dot,
                       sse41::gather_scale, sse41::normalize_aos, sse41::normalize_soa,
                       sse41::add_f64, sse41::dot_f64, sse41::dot_int8};
Kernels table_avx2 = {avx2::add, avx2::saxpy, avx2::relu, avx2::dot,
                      avx2::gather_scale, avx2::normalize_aos, avx2::normalize_soa,
                      avx2::add_f64, avx2::dot_f64, avx2::dot_int8};
Kernels table_avx512 = {avx512::add, avx512::saxpy, avx512::relu, avx512::dot,
                        avx512::gather_scale, avx512::normalize_aos, avx512::normalize_soa,
                        avx512::add_f64, avx512::dot_f64, avx512::dot_int8};
#endif

}  // namespace

const char* isa_name(Isa isa) {
    switch (isa) {
        case Isa::Scalar: return "scalar";
        case Isa::AutoSse2: return "auto-sse2";
        case Isa::AutoNative: return "auto-native";
        case Isa::Sse41: return "sse4.1";
        case Isa::Avx2: return "avx2";
        case Isa::Avx512: return "avx512";
        default: return "?";
    }
}

bool isa_available(Isa isa) {
    switch (isa) {
        case Isa::Scalar:
        case Isa::AutoSse2:
        case Isa::AutoNative:
            return true;
        case Isa::Sse41:
#ifdef SIMD_HAVE_X86
            return __builtin_cpu_supports("sse4.1") != 0;
#else
            return false;
#endif
        case Isa::Avx2:
#ifdef SIMD_HAVE_X86
            return __builtin_cpu_supports("avx2") != 0 && __builtin_cpu_supports("fma") != 0;
#else
            return false;
#endif
        case Isa::Avx512:
#ifdef SIMD_HAVE_X86
            return __builtin_cpu_supports("avx512f") != 0 &&
                   __builtin_cpu_supports("avx512bw") != 0 &&
                   __builtin_cpu_supports("avx512vl") != 0;
#else
            return false;
#endif
        default:
            return false;
    }
}

size_t isa_vector_width(Isa isa) {
    switch (isa) {
        case Isa::Scalar: return 1;
        case Isa::Sse41: return 4;
        case Isa::Avx2: return 8;
        case Isa::Avx512: return 16;
        default: return 0;  // 自动向量化：宽度由编译器决定
    }
}

const Kernels& kernels(Isa isa) {
    switch (isa) {
        case Isa::AutoSse2: return table_auto_sse2;
        case Isa::AutoNative: return table_auto_native;
#ifdef SIMD_HAVE_X86
        case Isa::Sse41: return isa_available(isa) ? table_sse41 : table_scalar;
        case Isa::Avx2: return isa_available(isa) ? table_avx2 : table_scalar;
        case Isa::Avx512: return isa_available(isa) ? table_avx512 : table_scalar;
#endif
        default: return table_scalar;
    }
}

}  // namespace simd
