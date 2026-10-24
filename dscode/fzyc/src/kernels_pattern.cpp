// 分支方向模式：周期模式、双分支、全局历史链、相关分支、分支密度。
#include "branch_lab.h"

namespace fz {

// ---------------- pattern_sum：b1[i] 决定加/减 ----------------

int64_t pattern_sum(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b1[i]) {
            FZ_BARRIER();
            sum += x;
        } else {
            FZ_BARRIER();
            sum -= x;
        }
    }
    return sum;
}

int64_t ref_pattern_sum(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b1[i]) {
            sum += x;
        } else {
            sum -= x;
        }
    }
    return sum;
}

// ---------------- pattern_two：每个元素两个独立分支 ----------------

int64_t pattern_two(const Input& in) {
    int64_t s1 = 0;
    int64_t s2 = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b1[i]) {
            FZ_BARRIER();
            s1 += x;
        } else {
            FZ_BARRIER();
            s1 -= x;
        }
        if (in.b2[i]) {
            FZ_BARRIER();
            s2 += x;
        } else {
            FZ_BARRIER();
            s2 -= x;
        }
    }
    return s1 * 3 + s2 * 5;
}

int64_t ref_pattern_two(const Input& in) {
    int64_t s1 = 0;
    int64_t s2 = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        s1 += in.b1[i] ? x : -x;
        s2 += in.b2[i] ? x : -x;
    }
    return s1 * 3 + s2 * 5;
}

// ---------------- history_chain：连续 p1 个 1 才累加 ----------------

namespace {
inline uint32_t history_mask(int32_t depth) {
    if (depth < 1) depth = 1;
    if (depth > 31) depth = 31;
    return (uint32_t)((1u << depth) - 1u);
}
}  // namespace

int64_t history_chain(const Input& in) {
    int64_t sum = 0;
    const uint32_t mask = history_mask(in.p1);
    uint32_t hist = 0;
    for (size_t i = 0; i < in.n; ++i) {
        hist = ((hist << 1) | (in.b1[i] ? 1u : 0u)) & mask;
        if (hist == mask) {
            FZ_BARRIER();
            sum += in.v[i];
        } else {
            FZ_BARRIER();
            sum -= 1;
        }
    }
    return sum;
}

int64_t ref_history_chain(const Input& in) {
    int64_t sum = 0;
    const uint32_t mask = history_mask(in.p1);
    uint32_t hist = 0;
    for (size_t i = 0; i < in.n; ++i) {
        hist = ((hist << 1) | (in.b1[i] ? 1u : 0u)) & mask;
        sum += (hist == mask) ? in.v[i] : -1;
    }
    return sum;
}

// ---------------- nested_corr：if (b1) { if (b2) ... } ----------------

int64_t nested_corr(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b1[i]) {
            FZ_BARRIER();
            if (in.b2[i]) {
                FZ_BARRIER();
                sum += x;
            } else {
                FZ_BARRIER();
                sum -= x;
            }
        } else {
            FZ_BARRIER();
            sum += 1;
        }
    }
    return sum;
}

int64_t ref_nested_corr(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b1[i]) {
            sum += in.b2[i] ? x : -x;
        } else {
            sum += 1;
        }
    }
    return sum;
}

// ---------------- xor_prev：分支条件是 b2（默认 b1[i]^b1[i-1]） ----------------

int64_t xor_prev(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b2[i]) {
            FZ_BARRIER();
            sum += x;
        } else {
            FZ_BARRIER();
            sum -= x;
        }
    }
    return sum;
}

int64_t ref_xor_prev(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        if (in.b2[i]) {
            sum += x;
        } else {
            sum -= x;
        }
    }
    return sum;
}

// ---------------- density：每个元素 p1 个分支 ----------------

int64_t density(const Input& in) {
    int64_t sum = 0;
    const int32_t reps = (in.p1 < 1) ? 1 : in.p1;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        for (int32_t k = 0; k < reps; ++k) {
            const bool take = (in.b1[i] ^ (uint8_t)(k & 1)) != 0;
            if (take) {
                FZ_BARRIER();
                sum += x + k;
            } else {
                FZ_BARRIER();
                sum -= x - k;
            }
        }
    }
    return sum;
}

int64_t ref_density(const Input& in) {
    int64_t sum = 0;
    const int32_t reps = (in.p1 < 1) ? 1 : in.p1;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = in.v[i];
        for (int32_t k = 0; k < reps; ++k) {
            sum += ((in.b1[i] ^ (uint8_t)(k & 1)) != 0) ? (x + k) : (-x + k);
        }
    }
    return sum;
}

}  // namespace fz
