// 手写 AVX2 + FMA（256-bit）：8 个 float / 4 个 double 每指令。
#include "simd_impl.h"

namespace simd {
namespace avx2 {
namespace {
using T = detail::FTraits<__m256>;
using D = detail::DTraits<__m256d>;
}  // namespace

void add(float* dst, const float* a, const float* b, size_t n) {
    detail::add_impl<__m256, T>(dst, a, b, n);
}
void saxpy(float* y, const float* x, float alpha, size_t n) {
    detail::saxpy_impl<__m256, T>(y, x, alpha, n);
}
void relu(float* dst, const float* src, size_t n) {
    detail::relu_impl<__m256, T>(dst, src, n);
}
float dot(const float* a, const float* b, size_t n) {
    return detail::dot_impl<__m256, T>(a, b, n);
}
void gather_scale(float* dst, const float* src, const int32_t* idx, float scale, size_t n) {
    detail::gather_scale_impl<__m256, T>(dst, src, idx, scale, n);
}
void normalize_aos(float* xyz, size_t npoints) {
    detail::normalize_aos_impl<__m256, T>(xyz, npoints);
}
void normalize_soa(float* x, float* y, float* z, size_t n) {
    detail::normalize_soa_impl<__m256, T>(x, y, z, n);
}
void add_f64(double* dst, const double* a, const double* b, size_t n) {
    detail::add_f64_impl<__m256d, D>(dst, a, b, n);
}
double dot_f64(const double* a, const double* b, size_t n) {
    return detail::dot_f64_impl<__m256d, D>(a, b, n);
}

int32_t dot_int8(const int8_t* a, const int8_t* b, size_t n) {
    __m256i acc = _mm256_setzero_si256();
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
        __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i));
        __m256i alo = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(va));
        __m256i blo = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(vb));
        __m256i ahi = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(va, 1));
        __m256i bhi = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(vb, 1));
        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(alo, blo));
        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(ahi, bhi));
    }
    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    __m128i s = _mm_add_epi32(lo, hi);
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
    int32_t total = _mm_cvtsi128_si32(s);
    for (; i < n; ++i) total += (int32_t)a[i] * (int32_t)b[i];
    return total;
}

}  // namespace avx2
}  // namespace simd
