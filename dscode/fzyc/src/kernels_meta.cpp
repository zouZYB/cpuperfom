// 提示与布局 / 调用与返回 / 长随机序列 / 代码对齐。
#include "branch_lab.h"

namespace fz {
namespace {

// ---------------- 调用目标（返回栈 / 直接调用） ----------------

FZ_NOINLINE int64_t site0(int32_t x) { return (int64_t)x * 2 + 0; }
FZ_NOINLINE int64_t site1(int32_t x) { return (int64_t)x * 2 + 1; }
FZ_NOINLINE int64_t site2(int32_t x) { return (int64_t)x * 2 + 2; }
FZ_NOINLINE int64_t site3(int32_t x) { return (int64_t)x * 2 + 3; }
FZ_NOINLINE int64_t site4(int32_t x) { return (int64_t)x * 2 + 4; }
FZ_NOINLINE int64_t site5(int32_t x) { return (int64_t)x * 2 + 5; }
FZ_NOINLINE int64_t site6(int32_t x) { return (int64_t)x * 2 + 6; }
FZ_NOINLINE int64_t site7(int32_t x) { return (int64_t)x * 2 + 7; }

FZ_NOINLINE int64_t direct_target(int32_t x) { return (int64_t)x * 2 + 1; }

FZ_NOINLINE int64_t rec_sum(int32_t depth, int32_t x) {
    if (depth <= 0) {
        FZ_BARRIER();
        return x;
    }
    const int64_t r = rec_sum(depth - 1, x);
    FZ_BARRIER();  // 放在调用之后：阻止编译器把递归改写成尾递归/循环
    return r + 1;
}

}  // namespace

// ---------------- 提示：none / likely / unlikely / unpredictable ----------------

int64_t hint_none(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x >= in.a) {
            FZ_BARRIER();
            sum += x;
        } else {
            FZ_BARRIER();
            sum -= x;
        }
    }
    return sum;
}

int64_t hint_likely(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (FZ_LIKELY(x >= in.a)) {
            FZ_BARRIER();
            sum += x;
        } else {
            FZ_BARRIER();
            sum -= x;
        }
    }
    return sum;
}

int64_t hint_unlikely(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (FZ_UNLIKELY(x >= in.a)) {
            FZ_BARRIER();
            sum += x;
        } else {
            FZ_BARRIER();
            sum -= x;
        }
    }
    return sum;
}

// 故意不加屏障：让 Clang-17+ 有机会把它替换成 CMOV
int64_t hint_unpredictable(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (FZ_UNPREDICTABLE(x >= in.a)) {
            sum += x;
        } else {
            sum -= x;
        }
    }
    return sum;
}

int64_t ref_hint(const Input& in) { return ref_sum_if(in); }

// ---------------- 分支顺序（热路径在前 / 在后） ----------------

int64_t hot_first(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x >= in.a) {
            FZ_BARRIER();
            sum += x;
        } else {
            FZ_BARRIER();
            sum -= x;
        }
    }
    return sum;
}

int64_t cold_first(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x < in.a) {
            FZ_BARRIER();
            sum -= x;
        } else {
            FZ_BARRIER();
            sum += x;
        }
    }
    return sum;
}

int64_t ref_hot_first(const Input& in) { return ref_sum_if(in); }
int64_t ref_cold_first(const Input& in) { return ref_sum_if(in); }

// ---------------- 调用 / 返回 ----------------

int64_t call_direct(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += direct_target(in.v[i]);
    return sum;
}

int64_t ref_call_direct(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += (int64_t)in.v[i] * 2 + 1;
    return sum;
}

int64_t call_sites8(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        switch (in.idx[i] & 7u) {
            case 0: sum += site0(x); break;
            case 1: sum += site1(x); break;
            case 2: sum += site2(x); break;
            case 3: sum += site3(x); break;
            case 4: sum += site4(x); break;
            case 5: sum += site5(x); break;
            case 6: sum += site6(x); break;
            default: sum += site7(x); break;
        }
    }
    return sum;
}

int64_t ref_call_sites8(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += (int64_t)in.v[i] * 2 + (int64_t)(in.idx[i] & 7u);
    return sum;
}

int64_t recursion_depth(const Input& in) {
    int32_t depth = in.p1;
    if (depth < 0) depth = 0;
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += rec_sum(depth, in.v[i]);
    return sum;
}

int64_t ref_recursion_depth(const Input& in) {
    int32_t depth = in.p1;
    if (depth < 0) depth = 0;
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += (int64_t)in.v[i] + depth;
    return sum;
}

// ---------------- 长随机序列 ----------------

int64_t random_mix(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b1[i]) {
            FZ_BARRIER();
            sum += x * 3 + 1;
        } else {
            FZ_BARRIER();
            sum -= x * 2 - 1;
        }
    }
    return sum;
}

int64_t ref_random_mix(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        sum += in.b1[i] ? (x * 3 + 1) : (-x * 2 + 1);
    }
    return sum;
}

// ---------------- 代码对齐：同一循环，函数入口对齐不同 ----------------

#ifdef FZ_ALIGN_BODY
#undef FZ_ALIGN_BODY
#endif
#define FZ_ALIGN_BODY()                                                         \
    int64_t sum = 0;                                                            \
    for (size_t i = 0; i < in.n; ++i) {                                         \
        const int32_t x = in.v[i];                                              \
        if (x >= in.a) {                                                        \
            FZ_BARRIER();                                                       \
            sum += x;                                                           \
        } else {                                                                \
            FZ_BARRIER();                                                       \
            sum -= x;                                                           \
        }                                                                       \
    }                                                                           \
    return sum;

FZ_FN_ALIGN(16) int64_t align_16(const Input& in) { FZ_ALIGN_BODY() }
FZ_FN_ALIGN(32) int64_t align_32(const Input& in) { FZ_ALIGN_BODY() }
FZ_FN_ALIGN(64) int64_t align_64(const Input& in) { FZ_ALIGN_BODY() }

#undef FZ_ALIGN_BODY

int64_t ref_align(const Input& in) { return ref_sum_if(in); }

}  // namespace fz
