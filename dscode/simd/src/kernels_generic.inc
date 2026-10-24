// 纯 C++ 参考实现，被三个 TU 各包含一次（由调用方提供 namespace 与 include）：
//   kernels_scalar.cpp      → -fno-tree-vectorize  （标量基线）
//   kernels_auto_sse2.cpp   → -O3                   （x86-64 默认 SSE2 的自动向量化）
//   kernels_auto_native.cpp → -O3 -march=native     （完整自动向量化）
//
// 刻意不用 __restrict：保留“编译器插入运行时别名检查”的真实行为，
// 同时让 dst==src 的原地调用保持正确（每个向量块先读后写）。

void add(float* dst, const float* a, const float* b, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = a[i] + b[i];
}

void saxpy(float* y, const float* x, float alpha, size_t n) {
    for (size_t i = 0; i < n; ++i) y[i] = alpha * x[i] + y[i];
}

// relu 语义：v > 0 ? v : 0（NaN → 0），与 _mm_max_ps(x, 0) 的行为一致
void relu(float* dst, const float* src, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[i] > 0.0f ? src[i] : 0.0f;
}

float dot(const float* a, const float* b, size_t n) {
    float acc = 0.0f;
    for (size_t i = 0; i < n; ++i) acc += a[i] * b[i];
    return acc;
}

void gather_scale(float* dst, const float* src, const int32_t* idx, float scale, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[(size_t)idx[i]] * scale;
}

void normalize_aos(float* xyz, size_t npoints) {
    for (size_t i = 0; i < npoints; ++i) {
        float x = xyz[3 * i + 0], y = xyz[3 * i + 1], z = xyz[3 * i + 2];
        float len = __builtin_sqrtf(x * x + y * y + z * z);
        float inv = 1.0f / len;
        xyz[3 * i + 0] = x * inv;
        xyz[3 * i + 1] = y * inv;
        xyz[3 * i + 2] = z * inv;
    }
}

void normalize_soa(float* x, float* y, float* z, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        float len = __builtin_sqrtf(x[i] * x[i] + y[i] * y[i] + z[i] * z[i]);
        float inv = 1.0f / len;
        x[i] *= inv;
        y[i] *= inv;
        z[i] *= inv;
    }
}

void add_f64(double* dst, const double* a, const double* b, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = a[i] + b[i];
}

double dot_f64(const double* a, const double* b, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += a[i] * b[i];
    return acc;
}

int32_t dot_int8(const int8_t* a, const int8_t* b, size_t n) {
    int64_t acc = 0;  // 参考实现用 64 位，避免中间溢出；接口按 int32 返回
    for (size_t i = 0; i < n; ++i) acc += (int32_t)a[i] * (int32_t)b[i];
    return (int32_t)acc;
}
