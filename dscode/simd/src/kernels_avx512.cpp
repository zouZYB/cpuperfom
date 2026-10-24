// 手写 AVX-512F/BW/VL（512-bit）：16 个 float / 8 个 double 每指令。
// 注意：Zen4 的 512 位指令由 256 位数据通路"双泵"执行，理论宽度翻倍、实际吞吐接近 256 位，
// 本机测出来通常只比 AVX2 快 0~30%，这本身就是很好的教学案例。
#include "simd_impl.h"

namespace simd {
namespace avx512 {
namespace {
using T = detail::FTraits<__m512>;
using D = detail::DTraits<__m512d>;
}  // namespace

void add(float* dst, const float* a, const float* b, size_t n) {
    detail::add_impl<__m512, T>(dst, a, b, n);
}
void saxpy(float* y, const float* x, float alpha, size_t n) {
    detail::saxpy_impl<__m512, T>(y, x, alpha, n);
}
void relu(float* dst, const float* src, size_t n) {
    detail::relu_impl<__m512, T>(dst, src, n);
}
float dot(const float* a, const float* b, size_t n) {
    return detail::dot_impl<__m512, T>(a, b, n);
}
void gather_scale(float* dst, const float* src, const int32_t* idx, float scale, size_t n) {
    detail::gather_scale_impl<__m512, T>(dst, src, idx, scale, n);
}
void normalize_aos(float* xyz, size_t npoints) {
    detail::normalize_aos_impl<__m512, T>(xyz, npoints);
}
void normalize_soa(float* x, float* y, float* z, size_t n) {
    detail::normalize_soa_impl<__m512, T>(x, y, z, n);
}
void add_f64(double* dst, const double* a, const double* b, size_t n) {
    detail::add_f64_impl<__m512d, D>(dst, a, b, n);
}
double dot_f64(const double* a, const double* b, size_t n) {
    return detail::dot_f64_impl<__m512d, D>(a, b, n);
}

int32_t dot_int8(const int8_t* a, const int8_t* b, size_t n) {
    __m512i acc = _mm512_setzero_si512();
    size_t i = 0;
    for (; i + 64 <= n; i += 64) {
        __m512i va = _mm512_loadu_si512(reinterpret_cast<const void*>(a + i));
        __m512i vb = _mm512_loadu_si512(reinterpret_cast<const void*>(b + i));
        __m512i alo = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(va));
        __m512i blo = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(vb));
        __m512i ahi = _mm512_cvtepi8_epi16(_mm512_extracti64x4_epi64(va, 1));
        __m512i bhi = _mm512_cvtepi8_epi16(_mm512_extracti64x4_epi64(vb, 1));
        acc = _mm512_add_epi32(acc, _mm512_madd_epi16(alo, blo));
        acc = _mm512_add_epi32(acc, _mm512_madd_epi16(ahi, bhi));
    }
    int32_t total = _mm512_reduce_add_epi32(acc);
    for (; i < n; ++i) total += (int32_t)a[i] * (int32_t)b[i];
    return total;
}

}  // namespace avx512
}  // namespace simd
