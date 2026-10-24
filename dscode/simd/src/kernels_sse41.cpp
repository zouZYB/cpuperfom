// 手写 SSE4.1（128-bit，无 FMA）：4 个 float / 2 个 double 每指令。
#include "simd_impl.h"

namespace simd {
namespace sse41 {
namespace {
using T = detail::FTraits<__m128>;
using D = detail::DTraits<__m128d>;
}  // namespace

void add(float* dst, const float* a, const float* b, size_t n) {
    detail::add_impl<__m128, T>(dst, a, b, n);
}
void saxpy(float* y, const float* x, float alpha, size_t n) {
    detail::saxpy_impl<__m128, T>(y, x, alpha, n);
}
void relu(float* dst, const float* src, size_t n) {
    detail::relu_impl<__m128, T>(dst, src, n);
}
float dot(const float* a, const float* b, size_t n) {
    return detail::dot_impl<__m128, T>(a, b, n);
}
void gather_scale(float* dst, const float* src, const int32_t* idx, float scale, size_t n) {
    detail::gather_scale_impl<__m128, T>(dst, src, idx, scale, n);
}
void normalize_aos(float* xyz, size_t npoints) {
    detail::normalize_aos_impl<__m128, T>(xyz, npoints);
}
void normalize_soa(float* x, float* y, float* z, size_t n) {
    detail::normalize_soa_impl<__m128, T>(x, y, z, n);
}
void add_f64(double* dst, const double* a, const double* b, size_t n) {
    detail::add_f64_impl<__m128d, D>(dst, a, b, n);
}
double dot_f64(const double* a, const double* b, size_t n) {
    return detail::dot_f64_impl<__m128d, D>(a, b, n);
}

int32_t dot_int8(const int8_t* a, const int8_t* b, size_t n) {
    __m128i acc = _mm_setzero_si128();
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
        __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i));
        __m128i alo = _mm_cvtepi8_epi16(va);
        __m128i blo = _mm_cvtepi8_epi16(vb);
        __m128i ahi = _mm_cvtepi8_epi16(_mm_srli_si128(va, 8));
        __m128i bhi = _mm_cvtepi8_epi16(_mm_srli_si128(vb, 8));
        acc = _mm_add_epi32(acc, _mm_madd_epi16(alo, blo));
        acc = _mm_add_epi32(acc, _mm_madd_epi16(ahi, bhi));
    }
    __m128i s = _mm_add_epi32(acc, _mm_shuffle_epi32(acc, _MM_SHUFFLE(1, 0, 3, 2)));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
    int32_t total = _mm_cvtsi128_si32(s);
    for (; i < n; ++i) total += (int32_t)a[i] * (int32_t)b[i];
    return total;
}

}  // namespace sse41
}  // namespace simd
