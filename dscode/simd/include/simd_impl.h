// 向量内核模板：同一份内核代码，按 128/256/512 位实例化。
// 每个 ISA 的 .cpp 只负责用自己的编译开关实例化模板（-msse4.1 / -mavx2 -mfma / -mavx512f ...）。
#pragma once

#include <immintrin.h>

#include <cstddef>
#include <cstdint>

namespace simd {
namespace detail {

// ---------------- float 向量 traits ----------------

template <class V>
struct FTraits;

template <>
struct FTraits<__m128> {
    static constexpr size_t W = 4;
    static __m128 load(const float* p) { return _mm_loadu_ps(p); }
    static void store(float* p, __m128 v) { _mm_storeu_ps(p, v); }
    static __m128 set1(float x) { return _mm_set1_ps(x); }
    static __m128 zero() { return _mm_setzero_ps(); }
    static __m128 add(__m128 a, __m128 b) { return _mm_add_ps(a, b); }
    static __m128 mul(__m128 a, __m128 b) { return _mm_mul_ps(a, b); }
    static __m128 div(__m128 a, __m128 b) { return _mm_div_ps(a, b); }
    static __m128 max(__m128 a, __m128 b) { return _mm_max_ps(a, b); }
    static __m128 sqrt(__m128 a) { return _mm_sqrt_ps(a); }
    // SSE 没有 FMA：拆成 mul + add，这一点在性能对比里也要体现
    static __m128 fmadd(__m128 a, __m128 b, __m128 c) { return _mm_add_ps(_mm_mul_ps(a, b), c); }
    // SSE 没有 gather：用 4 次标量加载拼出来
    static __m128 gather(const float* base, const int32_t* idx) {
        return _mm_setr_ps(base[idx[0]], base[idx[1]], base[idx[2]], base[idx[3]]);
    }
    static float hsum(__m128 v) {
        __m128 shuf = _mm_movehdup_ps(v);
        __m128 sums = _mm_add_ps(v, shuf);
        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        return _mm_cvtss_f32(sums);
    }
};

#if defined(__AVX2__)
template <>
struct FTraits<__m256> {
    static constexpr size_t W = 8;
    static __m256 load(const float* p) { return _mm256_loadu_ps(p); }
    static void store(float* p, __m256 v) { _mm256_storeu_ps(p, v); }
    static __m256 set1(float x) { return _mm256_set1_ps(x); }
    static __m256 zero() { return _mm256_setzero_ps(); }
    static __m256 add(__m256 a, __m256 b) { return _mm256_add_ps(a, b); }
    static __m256 mul(__m256 a, __m256 b) { return _mm256_mul_ps(a, b); }
    static __m256 div(__m256 a, __m256 b) { return _mm256_div_ps(a, b); }
    static __m256 max(__m256 a, __m256 b) { return _mm256_max_ps(a, b); }
    static __m256 sqrt(__m256 a) { return _mm256_sqrt_ps(a); }
    static __m256 fmadd(__m256 a, __m256 b, __m256 c) { return _mm256_fmadd_ps(a, b, c); }
    static __m256 gather(const float* base, const int32_t* idx) {
        return _mm256_i32gather_ps(base, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(idx)), 4);
    }
    static float hsum(__m256 v) {
        __m128 lo = _mm256_castps256_ps128(v);
        __m128 hi = _mm256_extractf128_ps(v, 1);
        return FTraits<__m128>::hsum(_mm_add_ps(lo, hi));
    }
};
#endif  // __AVX2__

#if defined(__AVX512F__)
template <>
struct FTraits<__m512> {
    static constexpr size_t W = 16;
    static __m512 load(const float* p) { return _mm512_loadu_ps(p); }
    static void store(float* p, __m512 v) { _mm512_storeu_ps(p, v); }
    static __m512 set1(float x) { return _mm512_set1_ps(x); }
    static __m512 zero() { return _mm512_setzero_ps(); }
    static __m512 add(__m512 a, __m512 b) { return _mm512_add_ps(a, b); }
    static __m512 mul(__m512 a, __m512 b) { return _mm512_mul_ps(a, b); }
    static __m512 div(__m512 a, __m512 b) { return _mm512_div_ps(a, b); }
    static __m512 max(__m512 a, __m512 b) { return _mm512_max_ps(a, b); }
    static __m512 sqrt(__m512 a) { return _mm512_sqrt_ps(a); }
    static __m512 fmadd(__m512 a, __m512 b, __m512 c) { return _mm512_fmadd_ps(a, b, c); }
    static __m512 gather(const float* base, const int32_t* idx) {
        return _mm512_i32gather_ps(_mm512_loadu_si512(reinterpret_cast<const void*>(idx)), base, 4);
    }
    static float hsum(__m512 v) { return _mm512_reduce_add_ps(v); }
};
#endif  // __AVX512F__

// ---------------- double 向量 traits ----------------

template <class V>
struct DTraits;

template <>
struct DTraits<__m128d> {
    static constexpr size_t W = 2;
    static __m128d load(const double* p) { return _mm_loadu_pd(p); }
    static void store(double* p, __m128d v) { _mm_storeu_pd(p, v); }
    static __m128d zero() { return _mm_setzero_pd(); }
    static __m128d add(__m128d a, __m128d b) { return _mm_add_pd(a, b); }
    static __m128d mul(__m128d a, __m128d b) { return _mm_mul_pd(a, b); }
    static __m128d fmadd(__m128d a, __m128d b, __m128d c) { return _mm_add_pd(_mm_mul_pd(a, b), c); }
    static double hsum(__m128d v) {
        return _mm_cvtsd_f64(_mm_add_sd(v, _mm_unpackhi_pd(v, v)));
    }
};

#if defined(__AVX2__)
template <>
struct DTraits<__m256d> {
    static constexpr size_t W = 4;
    static __m256d load(const double* p) { return _mm256_loadu_pd(p); }
    static void store(double* p, __m256d v) { _mm256_storeu_pd(p, v); }
    static __m256d zero() { return _mm256_setzero_pd(); }
    static __m256d add(__m256d a, __m256d b) { return _mm256_add_pd(a, b); }
    static __m256d mul(__m256d a, __m256d b) { return _mm256_mul_pd(a, b); }
    static __m256d fmadd(__m256d a, __m256d b, __m256d c) { return _mm256_fmadd_pd(a, b, c); }
    static double hsum(__m256d v) {
        __m128d lo = _mm256_castpd256_pd128(v);
        __m128d hi = _mm256_extractf128_pd(v, 1);
        return DTraits<__m128d>::hsum(_mm_add_pd(lo, hi));
    }
};
#endif  // __AVX2__

#if defined(__AVX512F__)
template <>
struct DTraits<__m512d> {
    static constexpr size_t W = 8;
    static __m512d load(const double* p) { return _mm512_loadu_pd(p); }
    static void store(double* p, __m512d v) { _mm512_storeu_pd(p, v); }
    static __m512d zero() { return _mm512_setzero_pd(); }
    static __m512d add(__m512d a, __m512d b) { return _mm512_add_pd(a, b); }
    static __m512d mul(__m512d a, __m512d b) { return _mm512_mul_pd(a, b); }
    static __m512d fmadd(__m512d a, __m512d b, __m512d c) { return _mm512_fmadd_pd(a, b, c); }
    static double hsum(__m512d v) { return _mm512_reduce_add_pd(v); }
};
#endif  // __AVX512F__

// ---------------- 内核模板 ----------------

template <class V, class T>
inline void add_impl(float* dst, const float* a, const float* b, size_t n) {
    size_t i = 0;
    for (; i + T::W <= n; i += T::W)
        T::store(dst + i, T::add(T::load(a + i), T::load(b + i)));
    for (; i < n; ++i) dst[i] = a[i] + b[i];
}

template <class V, class T>
inline void saxpy_impl(float* y, const float* x, float alpha, size_t n) {
    const V va = T::set1(alpha);
    size_t i = 0;
    for (; i + T::W <= n; i += T::W)
        T::store(y + i, T::fmadd(va, T::load(x + i), T::load(y + i)));
    for (; i < n; ++i) y[i] = alpha * x[i] + y[i];
}

template <class V, class T>
inline void relu_impl(float* dst, const float* src, size_t n) {
    const V z = T::zero();
    size_t i = 0;
    for (; i + T::W <= n; i += T::W)
        T::store(dst + i, T::max(T::load(src + i), z));
    for (; i < n; ++i) dst[i] = src[i] > 0.0f ? src[i] : 0.0f;
}

// 4 路累加器：避免单一依赖链把向量 FMA 的吞吐串行化
template <class V, class T>
inline float dot_impl(const float* a, const float* b, size_t n) {
    V a0 = T::zero(), a1 = T::zero(), a2 = T::zero(), a3 = T::zero();
    size_t i = 0;
    for (; i + 4 * T::W <= n; i += 4 * T::W) {
        a0 = T::fmadd(T::load(a + i + 0 * T::W), T::load(b + i + 0 * T::W), a0);
        a1 = T::fmadd(T::load(a + i + 1 * T::W), T::load(b + i + 1 * T::W), a1);
        a2 = T::fmadd(T::load(a + i + 2 * T::W), T::load(b + i + 2 * T::W), a2);
        a3 = T::fmadd(T::load(a + i + 3 * T::W), T::load(b + i + 3 * T::W), a3);
    }
    for (; i + T::W <= n; i += T::W)
        a0 = T::fmadd(T::load(a + i), T::load(b + i), a0);
    float s = T::hsum(T::add(T::add(a0, a1), T::add(a2, a3)));
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}

template <class V, class T>
inline void gather_scale_impl(float* dst, const float* src, const int32_t* idx, float scale, size_t n) {
    const V vs = T::set1(scale);
    size_t i = 0;
    for (; i + T::W <= n; i += T::W)
        T::store(dst + i, T::mul(T::gather(src, idx + i), vs));
    for (; i < n; ++i) dst[i] = src[(size_t)idx[i]] * scale;
}

template <class V, class T>
inline void normalize_soa_impl(float* x, float* y, float* z, size_t n) {
    const V one = T::set1(1.0f);
    size_t i = 0;
    for (; i + T::W <= n; i += T::W) {
        V xv = T::load(x + i), yv = T::load(y + i), zv = T::load(z + i);
        V len2 = T::fmadd(zv, zv, T::fmadd(yv, yv, T::mul(xv, xv)));
        V inv = T::div(one, T::sqrt(len2));
        T::store(x + i, T::mul(xv, inv));
        T::store(y + i, T::mul(yv, inv));
        T::store(z + i, T::mul(zv, inv));
    }
    for (; i < n; ++i) {
        float len = __builtin_sqrtf(x[i] * x[i] + y[i] * y[i] + z[i] * z[i]);
        float inv = 1.0f / len;
        x[i] *= inv;
        y[i] *= inv;
        z[i] *= inv;
    }
}

// AoS：向量化只能靠 gather（3 次/块），写回没有 scater 指令时还要标量散写——
// 这正是"布局不友好时 SIMD 收益被吃掉"的现场演示。
template <class V, class T>
inline void normalize_aos_impl(float* xyz, size_t npoints) {
    const V one = T::set1(1.0f);
    int32_t idxbuf[T::W];
    float tx[T::W], ty[T::W], tz[T::W];
    size_t i = 0;
    for (; i + T::W <= npoints; i += T::W) {
        for (size_t k = 0; k < T::W; ++k) idxbuf[k] = (int32_t)(3 * (i + k));
        V xv = T::gather(xyz + 0, idxbuf);
        V yv = T::gather(xyz + 1, idxbuf);
        V zv = T::gather(xyz + 2, idxbuf);
        V len2 = T::fmadd(zv, zv, T::fmadd(yv, yv, T::mul(xv, xv)));
        V inv = T::div(one, T::sqrt(len2));
        T::store(tx, T::mul(xv, inv));
        T::store(ty, T::mul(yv, inv));
        T::store(tz, T::mul(zv, inv));
        for (size_t k = 0; k < T::W; ++k) {
            size_t o = 3 * (i + k);
            xyz[o + 0] = tx[k];
            xyz[o + 1] = ty[k];
            xyz[o + 2] = tz[k];
        }
    }
    for (; i < npoints; ++i) {
        float x = xyz[3 * i + 0], y = xyz[3 * i + 1], z = xyz[3 * i + 2];
        float len = __builtin_sqrtf(x * x + y * y + z * z);
        float inv = 1.0f / len;
        xyz[3 * i + 0] = x * inv;
        xyz[3 * i + 1] = y * inv;
        xyz[3 * i + 2] = z * inv;
    }
}

template <class V, class T>
inline void add_f64_impl(double* dst, const double* a, const double* b, size_t n) {
    size_t i = 0;
    for (; i + T::W <= n; i += T::W)
        T::store(dst + i, T::add(T::load(a + i), T::load(b + i)));
    for (; i < n; ++i) dst[i] = a[i] + b[i];
}

template <class V, class T>
inline double dot_f64_impl(const double* a, const double* b, size_t n) {
    V a0 = T::zero(), a1 = T::zero();
    size_t i = 0;
    for (; i + 2 * T::W <= n; i += 2 * T::W) {
        a0 = T::fmadd(T::load(a + i + 0 * T::W), T::load(b + i + 0 * T::W), a0);
        a1 = T::fmadd(T::load(a + i + 1 * T::W), T::load(b + i + 1 * T::W), a1);
    }
    for (; i + T::W <= n; i += T::W)
        a0 = T::fmadd(T::load(a + i), T::load(b + i), a0);
    double s = T::hsum(T::add(a0, a1));
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}

}  // namespace detail
}  // namespace simd
