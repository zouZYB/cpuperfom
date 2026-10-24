// 循环相关：内层循环次数（循环预测器）、嵌套循环、展开降低循环分支频率。
#include "branch_lab.h"

namespace fz {
namespace {

// 不内联的加法：保证内层循环真实存在（否则编译器会把循环折叠成乘法）
FZ_NOINLINE void bump(int64_t& s) { s += 1; }

inline void step(const Input& in, size_t i, int64_t& sum) {
    const int64_t x = in.v[i];
    if (in.b1[i]) {
        FZ_BARRIER();
        sum += x;
    } else {
        FZ_BARRIER();
        sum -= x;
    }
}

}  // namespace

int64_t inner_trip(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t trips = in.trips[i];
        for (int32_t k = 0; k < trips; ++k) bump(sum);
    }
    return sum;
}

int64_t ref_inner_trip(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += in.trips[i];
    return sum;
}

int64_t nested_loop(const Input& in) {
    int64_t sum = 0;
    const int32_t outer = (in.a < 0) ? 0 : in.a;
    const int32_t inner = (in.b < 0) ? 0 : in.b;
    for (size_t i = 0; i < in.n; ++i) {
        for (int32_t o = 0; o < outer; ++o) {
            for (int32_t k = 0; k < inner; ++k) bump(sum);
        }
    }
    return sum;
}

int64_t ref_nested_loop(const Input& in) {
    const int64_t outer = (in.a < 0) ? 0 : in.a;
    const int64_t inner = (in.b < 0) ? 0 : in.b;
    return (int64_t)in.n * outer * inner;
}

// ---------------- 展开：每轮处理 p1 个元素 ----------------

int64_t unroll(const Input& in) {
    int64_t sum = 0;
    size_t i = 0;
    switch (in.p1) {
        case 1:
            for (; i < in.n; ++i) step(in, i, sum);
            break;
        case 2:
            for (; i + 2 <= in.n; i += 2) {
                step(in, i, sum);
                step(in, i + 1, sum);
            }
            for (; i < in.n; ++i) step(in, i, sum);
            break;
        case 4:
            for (; i + 4 <= in.n; i += 4) {
                step(in, i, sum);
                step(in, i + 1, sum);
                step(in, i + 2, sum);
                step(in, i + 3, sum);
            }
            for (; i < in.n; ++i) step(in, i, sum);
            break;
        default:
            for (; i + 8 <= in.n; i += 8) {
                step(in, i, sum);
                step(in, i + 1, sum);
                step(in, i + 2, sum);
                step(in, i + 3, sum);
                step(in, i + 4, sum);
                step(in, i + 5, sum);
                step(in, i + 6, sum);
                step(in, i + 7, sum);
            }
            for (; i < in.n; ++i) step(in, i, sum);
            break;
    }
    return sum;
}

int64_t ref_unroll(const Input& in) { return ref_pattern_sum(in); }

}  // namespace fz
