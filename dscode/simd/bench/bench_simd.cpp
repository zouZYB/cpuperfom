// Google Benchmark 性能用例：同一内核 × 6 种实现 × 多档规模。
//
// 规模含义（本机 7800X3D：L1 32KB/核、L2 1MB/核、L3 96MB 共享）：
//   1024  元素  → L1 常驻，纯计算受限
//   65536 元素  → L2/L3 边界
//   4M    元素  → 16MB/数组，L3 常驻
//   32M   元素  → 128MB/数组，强制打穿到 DRAM，暴露带宽上限
//
// 所有缓冲区用 64 字节对齐分配：否则 malloc 的随机对齐会让"未对齐"用例
// 混入 4K aliasing 噪声，测出来的不是对齐效应而是分配器抖动。
#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "simd_kernels.h"

namespace {

using simd::Isa;

// 所有缓冲区按 4KB 页对齐分配。原因（实测出来的坑）：
// 若三个数组的页内偏移不同，Zen4 的 512 位路径会掉到 ~1/3 吞吐（256 位不受影响），
// 而 malloc/64B 对齐分配给出的页内偏移随分配器状态漂移，会把这种布局噪声
// 误读成 ISA 差异。固定成同一页内偏移后，数据可复现。
inline void* aligned_alloc_page(size_t bytes) {
#if defined(_WIN32)
    return _aligned_malloc(bytes, 4096);
#else
    void* p = nullptr;
    if (posix_memalign(&p, 4096, bytes) != 0) return nullptr;
    return p;
#endif
}

inline void aligned_free_page(void* p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

bool skip_if_unsupported(benchmark::State& st, Isa isa) {
    if (simd::isa_available(isa)) return false;
    st.SkipWithError((std::string("ISA 不可用: ") + simd::isa_name(isa)).c_str());
    return true;
}

// 64B 对齐、构造即填充的缓冲区
template <class T>
class AlignedBuf {
public:
    explicit AlignedBuf(size_t count, T v = T()) : n_(count) {
        size_t bytes = ((count * sizeof(T) + 63) / 64) * 64;
        if (bytes == 0) bytes = 64;
        ptr_ = static_cast<T*>(aligned_alloc_page(bytes));
        if (!ptr_) throw std::bad_alloc();
        std::fill(ptr_, ptr_ + count, v);
    }
    ~AlignedBuf() { aligned_free_page(ptr_); }
    AlignedBuf(const AlignedBuf&) = delete;
    AlignedBuf& operator=(const AlignedBuf&) = delete;
    T* data() { return ptr_; }
    const T* data() const { return ptr_; }
    size_t size() const { return n_; }

private:
    T* ptr_;
    size_t n_;
};

// ---------------- float 逐元素 ----------------

void BM_Add(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<float> a(n, 1.0f), b(n, 2.0f);
    AlignedBuf<float> d(n);
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.add(d.data(), a.data(), b.data(), n);
        benchmark::DoNotOptimize(d.data());
        benchmark::ClobberMemory();
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 3 * 4);
}

void BM_Saxpy(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<float> x(n, 3.0f), y(n, 4.0f);
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.saxpy(y.data(), x.data(), 2.0f, n);
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 3 * 4);
}

void BM_Relu(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<float> src(n), dst(n);
    for (size_t i = 0; i < n; ++i) src.data()[i] = (float)((int)(i % 17) - 8);
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.relu(dst.data(), src.data(), n);
        benchmark::DoNotOptimize(dst.data());
        benchmark::ClobberMemory();
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 2 * 4);
}

void BM_Dot(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<float> a(n, 1.5f), b(n, 2.5f);
    const simd::Kernels& k = simd::kernels(isa);
    float sink = 0;
    for (auto _ : st) {
        sink += k.dot(a.data(), b.data(), n);
        benchmark::DoNotOptimize(sink);
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 2 * 4);
}

// ---------------- 间接访问 / 布局 ----------------

void BM_Gather(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    const size_t src_n = 1u << 20;
    AlignedBuf<float> src(src_n), dst(n);
    AlignedBuf<int32_t> idx(n);
    for (size_t i = 0; i < src_n; ++i) src.data()[i] = (float)i;
    uint32_t s = 12345;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        idx.data()[i] = (int32_t)(s % src_n);
    }
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.gather_scale(dst.data(), src.data(), idx.data(), 2.0f, n);
        benchmark::DoNotOptimize(dst.data());
        benchmark::ClobberMemory();
    }
    st.SetItemsProcessed((int64_t)st.iterations() * (int64_t)n);
}

void BM_NormalizeSoa(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<float> x(n, 1.0f), y(n, 2.0f), z(n, 3.0f);
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.normalize_soa(x.data(), y.data(), z.data(), n);
        benchmark::DoNotOptimize(x.data());
        benchmark::ClobberMemory();
    }
    st.SetItemsProcessed((int64_t)st.iterations() * (int64_t)n);
}

void BM_NormalizeAos(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<float> xyz(3 * n);
    for (size_t i = 0; i < n; ++i) {
        xyz.data()[3 * i + 0] = 1.0f;
        xyz.data()[3 * i + 1] = 2.0f;
        xyz.data()[3 * i + 2] = 3.0f;
    }
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.normalize_aos(xyz.data(), n);
        benchmark::DoNotOptimize(xyz.data());
        benchmark::ClobberMemory();
    }
    st.SetItemsProcessed((int64_t)st.iterations() * (int64_t)n);
}

// ---------------- 整数 / 双精度 ----------------

void BM_Int8Dot(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<int8_t> a(n), b(n);
    for (size_t i = 0; i < n; ++i) {
        a.data()[i] = (int8_t)(i % 100);
        b.data()[i] = (int8_t)((i * 3) % 100);
    }
    const simd::Kernels& k = simd::kernels(isa);
    int32_t sink = 0;
    for (auto _ : st) {
        sink += k.dot_int8(a.data(), b.data(), n);
        benchmark::DoNotOptimize(sink);
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 2);
}

void BM_AddF64(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<double> a(n, 1.0), b(n, 2.0), d(n);
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.add_f64(d.data(), a.data(), b.data(), n);
        benchmark::DoNotOptimize(d.data());
        benchmark::ClobberMemory();
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 3 * 8);
}

void BM_DotF64(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<double> a(n, 1.5), b(n, 2.5);
    const simd::Kernels& k = simd::kernels(isa);
    double sink = 0;
    for (auto _ : st) {
        sink += k.dot_f64(a.data(), b.data(), n);
        benchmark::DoNotOptimize(sink);
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 2 * 8);
}

// ---------------- 对齐 / 小规模 ----------------

void BM_MisalignedAdd(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t off = (size_t)st.range(0);
    const size_t n = (size_t)st.range(1);
    AlignedBuf<float> a(n + 8, 1.0f), b(n + 8, 2.0f), d(n + 8);
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.add(d.data() + off, a.data() + off, b.data() + off, n);
        benchmark::DoNotOptimize(d.data());
        benchmark::ClobberMemory();
    }
    st.SetBytesProcessed((int64_t)st.iterations() * (int64_t)n * 3 * 4);
}

void BM_TinyAdd(benchmark::State& st, Isa isa) {
    if (skip_if_unsupported(st, isa)) return;
    const size_t n = (size_t)st.range(0);
    AlignedBuf<float> a(n, 1.0f), b(n, 2.0f), d(n);
    const simd::Kernels& k = simd::kernels(isa);
    for (auto _ : st) {
        k.add(d.data(), a.data(), b.data(), n);
        benchmark::DoNotOptimize(d.data());
        benchmark::ClobberMemory();
    }
    st.SetItemsProcessed((int64_t)st.iterations() * (int64_t)n);
}

// ---------------- 注册 ----------------

#define ALL_ISAS(KERNEL, ...)                                                              \
    BENCHMARK_CAPTURE(KERNEL, scalar, Isa::Scalar)->ArgsProduct({{__VA_ARGS__}})->ArgNames({"n"});          \
    BENCHMARK_CAPTURE(KERNEL, auto_sse2, Isa::AutoSse2)->ArgsProduct({{__VA_ARGS__}})->ArgNames({"n"});     \
    BENCHMARK_CAPTURE(KERNEL, auto_native, Isa::AutoNative)->ArgsProduct({{__VA_ARGS__}})->ArgNames({"n"}); \
    BENCHMARK_CAPTURE(KERNEL, sse41, Isa::Sse41)->ArgsProduct({{__VA_ARGS__}})->ArgNames({"n"});            \
    BENCHMARK_CAPTURE(KERNEL, avx2, Isa::Avx2)->ArgsProduct({{__VA_ARGS__}})->ArgNames({"n"});              \
    BENCHMARK_CAPTURE(KERNEL, avx512, Isa::Avx512)->ArgsProduct({{__VA_ARGS__}})->ArgNames({"n"});

ALL_ISAS(BM_Add, 1024, 65536, 1 << 22, 1 << 25)
ALL_ISAS(BM_Saxpy, 1024, 65536, 1 << 22, 1 << 25)
ALL_ISAS(BM_Relu, 1024, 65536, 1 << 22, 1 << 25)
ALL_ISAS(BM_Dot, 1024, 65536, 1 << 22, 1 << 25)
ALL_ISAS(BM_Gather, 4096, 65536, 1 << 20)
ALL_ISAS(BM_NormalizeSoa, 1024, 65536, 1 << 22)
ALL_ISAS(BM_NormalizeAos, 1024, 65536, 1 << 22)
ALL_ISAS(BM_Int8Dot, 4096, 1 << 16, 1 << 22)
ALL_ISAS(BM_AddF64, 1024, 1 << 16, 1 << 22)
ALL_ISAS(BM_DotF64, 1024, 1 << 16, 1 << 22)
ALL_ISAS(BM_TinyAdd, 1, 2, 3, 7, 8, 9, 16, 17, 33)

#define ALL_ISAS_MISALIGNED(KERNEL)                                                        \
    BENCHMARK_CAPTURE(KERNEL, scalar, Isa::Scalar)                                          \
        ->ArgsProduct({{0, 1, 2, 3}, {4096, 65536}})                                        \
        ->ArgNames({"off", "n"});                                                           \
    BENCHMARK_CAPTURE(KERNEL, avx2, Isa::Avx2)                                              \
        ->ArgsProduct({{0, 1, 2, 3}, {4096, 65536}})                                        \
        ->ArgNames({"off", "n"});                                                           \
    BENCHMARK_CAPTURE(KERNEL, avx512, Isa::Avx512)                                          \
        ->ArgsProduct({{0, 1, 2, 3}, {4096, 65536}})                                        \
        ->ArgNames({"off", "n"});

ALL_ISAS_MISALIGNED(BM_MisalignedAdd)

}  // namespace

BENCHMARK_MAIN();
