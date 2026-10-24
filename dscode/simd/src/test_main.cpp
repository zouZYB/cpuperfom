// 正确性测试：把 SIMD 内核最容易踩的坑全部钉住。
//
// 覆盖维度：
//   N          : 0,1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,1000,1024,4095,4096,4097
//   对齐       : 指针偏移 0/1/2/3/4/7 个元素（未对齐 load/store）
//   数值       : 随机值 + NaN / ±Inf / ±0 / 次正规数 / ±FLT_MAX
//   原地       : dst==src、y==x、xyz 原地
//   归约       : dot 与 dot_f64 用 double 参考值比对（容差）
//   整数       : int8 点积，含 -128 边界与随机值，要求逐位一致
//   间接访问   : gather 的逆序 / 重复 / 步长 / 随机排列索引
//   布局       : AoS 与 SoA 两种 normalize 结果一致性
//
// 断言策略：与独立编写的标量参考实现比对；浮点用相对/绝对容差，NaN==NaN。
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "simd_kernels.h"

namespace {

// ---------------- 基础设施 ----------------

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    float uniform(float lo, float hi) {
        return lo + (hi - lo) * (float)((next() >> 40) / 16777216.0);
    }
    int32_t index(int32_t n) { return (int32_t)(next() % (uint64_t)n); }
};

bool close_f(float got, float want, double rtol, double atol) {
    if (std::isnan(got) || std::isnan(want)) return std::isnan(got) && std::isnan(want);
    if (std::isinf(got) || std::isinf(want)) return got == want;
    double d = std::fabs((double)got - (double)want);
    return d <= atol + rtol * std::fabs((double)want);
}

bool close_d(double got, double want, double rtol, double atol) {
    if (std::isnan(got) || std::isnan(want)) return std::isnan(got) && std::isnan(want);
    if (std::isinf(got) || std::isinf(want)) return got == want;
    double d = std::fabs(got - want);
    return d <= atol + rtol * std::fabs(want);
}

struct Test {
    std::string name;
    int checks = 0;
    int fails = 0;
    int shown = 0;

    explicit Test(std::string n) : name(std::move(n)) {}

    void fail(const char* fmt, ...) {
        ++fails;
        if (shown >= 8) return;  // 每个测试最多打 8 条，避免刷屏
        ++shown;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        std::printf("    FAIL %s: %s\n", name.c_str(), buf);
    }

    void check(bool ok, const char* fmt, ...) {
        ++checks;
        if (ok) return;
        ++fails;
        if (shown >= 8) return;
        ++shown;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        std::printf("    FAIL %s: %s\n", name.c_str(), buf);
    }

    void report() {
        std::printf("[%s] %-22s checks=%-6d fails=%d\n", fails ? "FAIL" : "PASS", name.c_str(), checks, fails);
    }
};

const size_t kN[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
                     127, 128, 129, 255, 256, 257, 1000, 1024, 4095, 4096, 4097,
                     65536, 1000003, 1048576, 1048577};
const size_t kOff[] = {0, 1, 2, 3, 4, 7};

std::vector<simd::Isa> all_isas() {
    std::vector<simd::Isa> v;
    for (int i = 0; i < (int)simd::Isa::Count; ++i) v.push_back((simd::Isa)i);
    return v;
}

// 带前后哨兵的缓冲区：越界写会被 has_canary() 抓到
template <class T>
struct Guarded {
    static const size_t kPad = 32;
    std::vector<T> mem;
    size_t n;
    size_t off;
    explicit Guarded(size_t count, size_t offset = 0)
        : mem(count + offset + 2 * kPad, (T)0), n(count), off(kPad + offset) {}
    T* p() { return mem.data() + off; }
    const T* p() const { return mem.data() + off; }
    bool canary_ok() const {
        for (size_t i = 0; i < kPad; ++i) {
            if (mem[i] != (T)0) return false;
            if (mem[off + n + i] != (T)0) return false;
        }
        return true;
    }
};

float special_at(size_t i) {
    static const float v[] = {0.0f,      -0.0f,     1.0f,       -1.0f,      INFINITY, -INFINITY,
                              NAN,       FLT_MIN,   -FLT_MIN,   FLT_MAX,    -FLT_MAX, 1e-40f,
                              -1e-40f,   3.5f,      -3.5f,      12345.678f};
    return v[i % (sizeof(v) / sizeof(v[0]))];
}

// ---------------- 独立参考实现（不复用被测代码） ----------------

void ref_add(float* d, const float* a, const float* b, size_t n) {
    for (size_t i = 0; i < n; ++i) d[i] = a[i] + b[i];
}
void ref_saxpy(float* y, const float* x, float alpha, size_t n) {
    for (size_t i = 0; i < n; ++i) y[i] = alpha * x[i] + y[i];
}
void ref_relu(float* d, const float* s, size_t n) {
    for (size_t i = 0; i < n; ++i) d[i] = s[i] > 0.0f ? s[i] : 0.0f;
}
float ref_dot(const float* a, const float* b, size_t n) {
    double acc = 0;
    for (size_t i = 0; i < n; ++i) acc += (double)a[i] * (double)b[i];
    return (float)acc;
}
void ref_normalize_soa(float* x, float* y, float* z, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        float len = __builtin_sqrtf(x[i] * x[i] + y[i] * y[i] + z[i] * z[i]);
        x[i] *= 1.0f / len;
        y[i] *= 1.0f / len;
        z[i] *= 1.0f / len;
    }
}
void ref_normalize_aos(float* xyz, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        float x = xyz[3 * i], y = xyz[3 * i + 1], z = xyz[3 * i + 2];
        float len = __builtin_sqrtf(x * x + y * y + z * z);
        xyz[3 * i] = x / len;
        xyz[3 * i + 1] = y / len;
        xyz[3 * i + 2] = z / len;
    }
}

// ---------------- 各内核测试 ----------------

void test_dispatch(Test& t) {
    t.check(simd::isa_name(simd::Isa::Scalar) != nullptr, "scalar 名字为空");
    t.check(simd::isa_vector_width(simd::Isa::Scalar) == 1, "scalar 宽度应为 1");
    t.check(simd::isa_vector_width(simd::Isa::Sse41) == 4, "sse 宽度应为 4");
    t.check(simd::isa_vector_width(simd::Isa::Avx2) == 8, "avx2 宽度应为 8");
    t.check(simd::isa_vector_width(simd::Isa::Avx512) == 16, "avx512 宽度应为 16");
    for (simd::Isa isa : all_isas()) {
        const simd::Kernels& k = simd::kernels(isa);
        t.check(k.add && k.saxpy && k.relu && k.dot && k.gather_scale && k.normalize_aos &&
                    k.normalize_soa && k.add_f64 && k.dot_f64 && k.dot_int8,
                "%s 分发表存在空指针", simd::isa_name(isa));
        // 可用性必须与编译期能力自洽：不可用时 kernels() 必须回落到标量表
        if (!simd::isa_available(isa)) {
            t.check(k.add == simd::kernels(simd::Isa::Scalar).add, "%s 不可用却未回落", simd::isa_name(isa));
        }
    }
}

void test_add(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : kN) {
            for (size_t off : kOff) {
                Guarded<float> a(n, off), b(n, off), got(n, off);
                Guarded<float> want(n, off);
                Rng r(0x1234 + n * 131 + off);
                for (size_t i = 0; i < n; ++i) {
                    a.p()[i] = r.uniform(-100.f, 100.f);
                    b.p()[i] = r.uniform(-100.f, 100.f);
                }
                ref_add(want.p(), a.p(), b.p(), n);
                k.add(got.p(), a.p(), b.p(), n);
                bool ok = true;
                for (size_t i = 0; i < n; ++i)
                    if (!close_f(got.p()[i], want.p()[i], 0.0, 0.0)) ok = false;
                t.check(ok, "%s n=%zu off=%zu 结果不一致", simd::isa_name(isa), n, off);
                t.check(got.canary_ok(), "%s n=%zu off=%zu 越界写", simd::isa_name(isa), n, off);
            }
        }
        // 原地：dst == a
        for (size_t n : {1u, 7u, 8u, 9u, 33u, 1000u}) {
            Guarded<float> a(n), b(n), want(n);
            Rng r(99 + n);
            for (size_t i = 0; i < n; ++i) {
                a.p()[i] = r.uniform(-10.f, 10.f);
                b.p()[i] = r.uniform(-10.f, 10.f);
            }
            ref_add(want.p(), a.p(), b.p(), n);
            k.add(a.p(), a.p(), b.p(), n);
            bool ok = true;
            for (size_t i = 0; i < n; ++i) ok &= close_f(a.p()[i], want.p()[i], 0, 0);
            t.check(ok, "%s 原地 add(dst==a) n=%zu 结果不一致", simd::isa_name(isa), n);
        }
    }
}

void test_saxpy(Test& t) {
    const float alphas[] = {0.0f, 1.0f, -1.0f, 0.5f, 3.14159f, -2.5e-3f, NAN};
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : kN) {
            for (size_t off : {0u, 1u, 3u}) {
                for (float alpha : alphas) {
                    Guarded<float> x(n, off), y(n, off), got(n, off), want(n, off);
                    Rng r(0x5150 + n * 17 + off);
                    for (size_t i = 0; i < n; ++i) {
                        x.p()[i] = r.uniform(-10.f, 10.f);
                        y.p()[i] = r.uniform(-10.f, 10.f);
                        got.p()[i] = y.p()[i];
                        want.p()[i] = y.p()[i];
                    }
                    ref_saxpy(want.p(), x.p(), alpha, n);
                    k.saxpy(got.p(), x.p(), alpha, n);
                    bool ok = true;
                    for (size_t i = 0; i < n; ++i)
                        if (!close_f(got.p()[i], want.p()[i], 1e-6, 1e-6)) ok = false;
                    t.check(ok, "%s n=%zu off=%zu alpha=%g 结果不一致", simd::isa_name(isa), n, off, alpha);
                }
            }
        }
    }
}

void test_relu(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : kN) {
            for (size_t off : {0u, 1u, 2u, 5u}) {
                Guarded<float> x(n, off), got(n, off), want(n, off);
                for (size_t i = 0; i < n; ++i) x.p()[i] = special_at(i);
                ref_relu(want.p(), x.p(), n);
                k.relu(got.p(), x.p(), n);
                bool ok = true;
                for (size_t i = 0; i < n; ++i) {
                    // relu 的 NaN 归一化为 +0，用位比较更严格
                    if (std::isnan(want.p()[i])) ok &= got.p()[i] == 0.0f;
                    else ok &= (std::memcmp(&got.p()[i], &want.p()[i], sizeof(float)) == 0);
                }
                t.check(ok, "%s n=%zu off=%zu 特殊值结果不一致", simd::isa_name(isa), n, off);
            }
        }
    }
}

void test_dot(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : kN) {
            for (size_t off : {0u, 1u, 3u}) {
                Guarded<float> a(n, off), b(n, off);
                Rng r(7 + n * 3 + off);
                for (size_t i = 0; i < n; ++i) {
                    a.p()[i] = r.uniform(-1.f, 1.f);
                    b.p()[i] = r.uniform(-1.f, 1.f);
                }
                float want = ref_dot(a.p(), b.p(), n);
                float got = k.dot(a.p(), b.p(), n);
                // 误差界：float 求和误差随 sqrt(n) 增长；参考值用 double 累加
                const double rtol = 1e-5 + 1e-7 * std::sqrt((double)n);
                t.check(close_f(got, want, rtol, 1e-4), "%s n=%zu off=%zu dot got=%.9g want=%.9g",
                        simd::isa_name(isa), n, off, got, want);
            }
        }
        // 特殊值：NaN / Inf 传播
        {
            float a[16], b[16];
            for (int i = 0; i < 16; ++i) {
                a[i] = 1.0f;
                b[i] = 2.0f;
            }
            a[7] = NAN;
            float got = k.dot(a, b, 16);
            t.check(std::isnan(got), "%s dot 未传播 NaN", simd::isa_name(isa));
            a[7] = INFINITY;
            b[7] = 0.0f;
            got = k.dot(a, b, 16);
            t.check(std::isnan(got), "%s dot Inf*0 未产生 NaN", simd::isa_name(isa));
        }
    }
}

void test_gather(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : {0u, 1u, 2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 63u, 64u, 65u, 1000u}) {
            for (size_t off : {0u, 1u, 2u}) {
                Guarded<float> src(1024, 0), dst(n, off);
                for (size_t i = 0; i < 1024; ++i) src.p()[i] = (float)i * 0.5f - 100.0f;
                std::vector<int32_t> idx(n);
                Rng r(0xabc + n);
                for (size_t i = 0; i < n; ++i) {
                    // 逆序、重复、步长、随机交替
                    switch (i % 4) {
                        case 0: idx[i] = (int32_t)(n - 1 - i); break;
                        case 1: idx[i] = (int32_t)(i % 7); break;
                        case 2: idx[i] = (int32_t)((i * 2) % 1024); break;
                        default: idx[i] = r.index(1024); break;
                    }
                }
                k.gather_scale(dst.p(), src.p(), idx.data(), 2.5f, n);
                bool ok = true;
                for (size_t i = 0; i < n; ++i)
                    ok &= dst.p()[i] == src.p()[idx[i]] * 2.5f;
                t.check(ok, "%s n=%zu off=%zu gather 结果不一致", simd::isa_name(isa), n, off);
                t.check(dst.canary_ok(), "%s n=%zu off=%zu gather 越界写", simd::isa_name(isa), n, off);

                // 特殊缩放系数：0 / 负数 / NaN / 极大值
                for (float scale : {0.0f, -2.5f, NAN, 1e30f}) {
                    Guarded<float> d2(n, off);
                    k.gather_scale(d2.p(), src.p(), idx.data(), scale, n);
                    bool ok2 = true;
                    for (size_t i = 0; i < n; ++i) {
                        float want2 = src.p()[idx[i]] * scale;
                        ok2 &= (std::isnan(want2) ? std::isnan(d2.p()[i]) : d2.p()[i] == want2);
                    }
                    t.check(ok2, "%s n=%zu off=%zu gather scale=%g 不一致", simd::isa_name(isa), n, off, scale);
                }
            }
        }
    }
}

void test_normalize(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : {0u, 1u, 3u, 4u, 5u, 7u, 8u, 9u, 15u, 16u, 17u, 100u, 1000u}) {
            // SoA
            Guarded<float> x(n), y(n), z(n), wx(n), wy(n), wz(n), gx(n), gy(n), gz(n);
            Rng r(31 + n);
            for (size_t i = 0; i < n; ++i) {
                x.p()[i] = wx.p()[i] = gx.p()[i] = r.uniform(-5.f, 5.f);
                y.p()[i] = wy.p()[i] = gy.p()[i] = r.uniform(-5.f, 5.f);
                z.p()[i] = wz.p()[i] = gz.p()[i] = r.uniform(-5.f, 5.f);
            }
            ref_normalize_soa(wx.p(), wy.p(), wz.p(), n);
            k.normalize_soa(gx.p(), gy.p(), gz.p(), n);
            bool ok = true;
            for (size_t i = 0; i < n; ++i)
                ok &= close_f(gx.p()[i], wx.p()[i], 1e-5, 1e-6) &&
                      close_f(gy.p()[i], wy.p()[i], 1e-5, 1e-6) &&
                      close_f(gz.p()[i], wz.p()[i], 1e-5, 1e-6);
            t.check(ok, "%s normalize_soa n=%zu 结果不一致", simd::isa_name(isa), n);

            // AoS
            Guarded<float> aos(3 * n), waos(3 * n), gaos(3 * n);
            for (size_t i = 0; i < 3 * n; ++i) aos.p()[i] = waos.p()[i] = gaos.p()[i] = r.uniform(-5.f, 5.f);
            ref_normalize_aos(waos.p(), n);
            k.normalize_aos(gaos.p(), n);
            ok = true;
            for (size_t i = 0; i < 3 * n; ++i)
                ok &= close_f(gaos.p()[i], waos.p()[i], 1e-5, 1e-6);
            t.check(ok, "%s normalize_aos n=%zu 结果不一致", simd::isa_name(isa), n);
        }
        // 退化输入：零向量 / 含 NaN / 极大值
        {
            float v[4] = {0.f, NAN, 1e30f, 3.f};
            float x[4], y[4], z[4], wx[4], wy[4], wz[4];
            for (int i = 0; i < 4; ++i) x[i] = y[i] = z[i] = v[i];
            std::memcpy(wx, x, sizeof(x));
            std::memcpy(wy, y, sizeof(y));
            std::memcpy(wz, z, sizeof(z));
            ref_normalize_soa(wx, wy, wz, 4);
            k.normalize_soa(x, y, z, 4);
            bool ok = true;
            for (int i = 0; i < 4; ++i)
                ok &= close_f(x[i], wx[i], 1e-5, 1e-6) && close_f(y[i], wy[i], 1e-5, 1e-6) &&
                      close_f(z[i], wz[i], 1e-5, 1e-6);
            t.check(ok, "%s normalize_soa 退化输入不一致", simd::isa_name(isa));
        }
        // 退化输入（AoS）：零向量 / NaN / 极大值
        {
            Guarded<float> aos(12), waos(12), gaos(12);
            const float v[4] = {0.0f, NAN, 1e30f, -3.0f};
            for (size_t p = 0; p < 4; ++p)
                for (int c = 0; c < 3; ++c) aos.p()[3 * p + c] = waos.p()[3 * p + c] = gaos.p()[3 * p + c] = v[p];
            ref_normalize_aos(waos.p(), 4);
            k.normalize_aos(gaos.p(), 4);
            bool ok = true;
            for (size_t i = 0; i < 12; ++i) ok &= close_f(gaos.p()[i], waos.p()[i], 1e-5, 1e-6);
            t.check(ok, "%s normalize_aos 退化输入不一致", simd::isa_name(isa));
            t.check(gaos.canary_ok(), "%s normalize_aos 越界写", simd::isa_name(isa));
        }
    }
}

void test_int8(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : {0u, 1u, 2u, 3u, 7u, 8u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 127u, 128u, 129u, 1000u, 4096u, 4097u}) {
            std::vector<int8_t> a(n), b(n);
            Rng r(0x1b8 + n);
            for (size_t i = 0; i < n; ++i) {
                switch (i % 5) {
                    case 0: a[i] = 127; b[i] = 127; break;
                    case 1: a[i] = -128; b[i] = 127; break;
                    case 2: a[i] = (int8_t)(r.next() & 0xff); b[i] = (int8_t)(r.next() & 0xff); break;
                    default: a[i] = (int8_t)r.uniform(-100.f, 100.f); b[i] = (int8_t)r.uniform(-100.f, 100.f); break;
                }
            }
            int64_t want = 0;
            for (size_t i = 0; i < n; ++i) want += (int32_t)a[i] * (int32_t)b[i];
            int32_t got = k.dot_int8(a.data(), b.data(), n);
            t.check((int64_t)got == want, "%s n=%zu int8 dot got=%d want=%lld", simd::isa_name(isa), n,
                    got, (long long)want);
        }
    }
}

void test_f64(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : {0u, 1u, 2u, 3u, 4u, 7u, 8u, 9u, 17u, 33u, 1000u, 4096u}) {
            for (size_t off : {0u, 1u}) {
                Guarded<double> a(n, off), b(n, off), got(n, off), want(n, off);
                Rng r(0xf64 + n);
                for (size_t i = 0; i < n; ++i) {
                    a.p()[i] = (double)r.uniform(-1e6f, 1e6f);
                    b.p()[i] = (double)r.uniform(-1e6f, 1e6f);
                }
                for (size_t i = 0; i < n; ++i) want.p()[i] = a.p()[i] + b.p()[i];
                k.add_f64(got.p(), a.p(), b.p(), n);
                bool ok = true;
                for (size_t i = 0; i < n; ++i) ok &= close_d(got.p()[i], want.p()[i], 0.0, 0.0);
                t.check(ok, "%s add_f64 n=%zu 结果不一致", simd::isa_name(isa), n);

                double dwant = 0;
                for (size_t i = 0; i < n; ++i) dwant += a.p()[i] * b.p()[i];
                double dgot = k.dot_f64(a.p(), b.p(), n);
                t.check(close_d(dgot, dwant, 1e-12, 1e-9), "%s n=%zu dot_f64 got=%.17g want=%.17g",
                        simd::isa_name(isa), n, dgot, dwant);
            }
        }
    }
}

void test_int8_large(Test& t) {
    const size_t n = 1u << 20;  // 100 万字节，验证长循环 + 尾部
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        std::vector<int8_t> a(n), b(n);
        for (size_t i = 0; i < n; ++i) {
            a[i] = (int8_t)((int)(i % 11) - 5);
            b[i] = (int8_t)((int)((i * 7) % 13) - 6);
        }
        int64_t want = 0;
        for (size_t i = 0; i < n; ++i) want += (int32_t)a[i] * (int32_t)b[i];
        int32_t got = simd::kernels(isa).dot_int8(a.data(), b.data(), n);
        t.check((int64_t)got == want, "%s n=%zu 大数组 int8 dot got=%d want=%lld", simd::isa_name(isa), n,
                got, (long long)want);
    }
}

// dst 与输入重叠：add(dst==b)、relu 原地、saxpy(y==x)
void test_aliasing(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        for (size_t n : {1u, 3u, 8u, 9u, 17u, 1000u, 4096u, 4097u}) {
            Rng r(0xa11a5 + n);
            // add：dst == b
            Guarded<float> a(n), b(n), want(n);
            for (size_t i = 0; i < n; ++i) {
                a.p()[i] = r.uniform(-10.f, 10.f);
                b.p()[i] = r.uniform(-10.f, 10.f);
            }
            ref_add(want.p(), a.p(), b.p(), n);
            k.add(b.p(), a.p(), b.p(), n);
            bool ok = true;
            for (size_t i = 0; i < n; ++i) ok &= b.p()[i] == want.p()[i];
            t.check(ok, "%s 原地 add(dst==b) n=%zu 结果不一致", simd::isa_name(isa), n);

            // relu：dst == src
            Guarded<float> s(n), rs(n);
            for (size_t i = 0; i < n; ++i) s.p()[i] = rs.p()[i] = special_at(i + n);
            ref_relu(rs.p(), rs.p(), n);
            k.relu(s.p(), s.p(), n);
            ok = true;
            for (size_t i = 0; i < n; ++i) {
                if (std::isnan(rs.p()[i])) ok &= s.p()[i] == 0.0f;
                else ok &= std::memcmp(&s.p()[i], &rs.p()[i], sizeof(float)) == 0;
            }
            t.check(ok, "%s 原地 relu(dst==src) n=%zu 结果不一致", simd::isa_name(isa), n);

            // saxpy：y == x
            Guarded<float> y(n), wy(n);
            for (size_t i = 0; i < n; ++i) {
                y.p()[i] = r.uniform(-5.f, 5.f);
                wy.p()[i] = y.p()[i];
            }
            ref_saxpy(wy.p(), wy.p(), 1.5f, n);
            k.saxpy(y.p(), y.p(), 1.5f, n);
            ok = true;
            for (size_t i = 0; i < n; ++i) ok &= close_f(y.p()[i], wy.p()[i], 1e-6, 1e-6);
            t.check(ok, "%s saxpy(y==x) n=%zu 结果不一致", simd::isa_name(isa), n);
        }
    }
}

void test_f64_special(Test& t) {
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        const simd::Kernels& k = simd::kernels(isa);
        double a[8], b[8];
        for (int i = 0; i < 8; ++i) {
            a[i] = 1.0;
            b[i] = 2.0;
        }
        a[3] = INFINITY;
        b[3] = -INFINITY;
        double out[8];
        k.add_f64(out, a, b, 8);
        t.check(std::isnan(out[3]), "%s add_f64 inf + -inf 未产生 NaN", simd::isa_name(isa));

        a[3] = INFINITY;
        b[3] = 0.0;
        double d = k.dot_f64(a, b, 8);
        t.check(std::isnan(d), "%s dot_f64 inf*0 未产生 NaN", simd::isa_name(isa));
    }
}


// 同一输入下，所有可用 ISA 的结果必须彼此一致（防止某条路径悄悄算错）
void test_cross_isa(Test& t) {
    const size_t n = 1024;
    Guarded<float> a(n), b(n);
    Rng r(0x5eed);
    for (size_t i = 0; i < n; ++i) {
        a.p()[i] = r.uniform(-1.f, 1.f);
        b.p()[i] = r.uniform(-1.f, 1.f);
    }
    Guarded<float> base(n);
    simd::kernels(simd::Isa::Scalar).add(base.p(), a.p(), b.p(), n);
    for (simd::Isa isa : all_isas()) {
        if (!simd::isa_available(isa)) continue;
        Guarded<float> out(n);
        simd::kernels(isa).add(out.p(), a.p(), b.p(), n);
        bool ok = true;
        for (size_t i = 0; i < n; ++i) ok &= std::memcmp(&out.p()[i], &base.p()[i], sizeof(float)) == 0;
        t.check(ok, "%s 与 scalar 的 add 结果不逐位一致", simd::isa_name(isa));
    }
}

}  // namespace

int main() {
    std::printf("== SIMD 内核正确性测试 ==\n");
    std::printf("ISA 可用性: ");
    for (simd::Isa isa : all_isas()) {
        std::printf("%s=%s ", simd::isa_name(isa), simd::isa_available(isa) ? "yes" : "no");
    }
    std::printf("\n\n");

    struct Case {
        const char* name;
        void (*fn)(Test&);
    } cases[] = {
        {"dispatch", test_dispatch},       {"add", test_add},
        {"saxpy", test_saxpy},             {"relu", test_relu},
        {"dot", test_dot},                 {"gather", test_gather},
        {"normalize", test_normalize},     {"int8", test_int8},
        {"int8-large", test_int8_large},   {"f64", test_f64},
        {"f64-special", test_f64_special}, {"aliasing", test_aliasing},
        {"cross-isa", test_cross_isa},
    };

    int total_fails = 0;
    for (const auto& c : cases) {
        Test t(c.name);
        c.fn(t);
        t.report();
        total_fails += t.fails;
    }
    std::printf("\n%s: 共 %d 个失败断言\n", total_fails == 0 ? "全部通过" : "存在失败", total_fails);
    return total_fails == 0 ? 0 : 1;
}
