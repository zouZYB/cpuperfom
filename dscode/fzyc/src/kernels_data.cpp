// 数据相关条件分支：同一份语义（阈值判断），分支版 vs CMOV/掩码版。
// 分支版在两条路径里都放编译期屏障，确保编译器不会把 if/else 折叠成 cmov，
// 否则内核里就没有分支可预测了。
#include "branch_lab.h"

namespace fz {

// ---------------- 求和：x >= a 时 sum += x，否则 sum -= x ----------------

int64_t sum_if_branch(const Input& in) {
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

int64_t sum_if_cmov(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        const int32_t mask = -(int32_t)(x >= in.a);  // 0 或 -1
        const int32_t pick = (x & mask) | ((-x) & ~mask);
        sum += pick;
    }
    return sum;
}

int64_t ref_sum_if(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x >= in.a) {
            sum += x;
        } else {
            sum -= x;
        }
    }
    return sum;
}

// ---------------- 计数：统计 x >= a 的个数 ----------------

int64_t count_if_branch(const Input& in) {
    int64_t cnt = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x >= in.a) {
            FZ_BARRIER();
            cnt += 1;
        } else {
            FZ_BARRIER();
            cnt += 0;
        }
    }
    return cnt;
}

int64_t count_if_cmov(const Input& in) {
    int64_t cnt = 0;
    for (size_t i = 0; i < in.n; ++i) cnt += (in.v[i] >= in.a) ? 1 : 0;
    return cnt;
}

int64_t ref_count_if(const Input& in) {
    int64_t cnt = 0;
    for (size_t i = 0; i < in.n; ++i) cnt += (in.v[i] >= in.a) ? 1 : 0;
    return cnt;
}

// ---------------- 最大值：初值 in.a ----------------

int64_t max_if_branch(const Input& in) {
    int32_t m = in.a;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x > m) {
            FZ_BARRIER();
            m = x;
        } else {
            FZ_BARRIER();
        }
    }
    return (int64_t)m;
}

int64_t max_cmov(const Input& in) {
    int32_t m = in.a;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        const int32_t d = x - m;       // 数据范围 [-256,255]，不会溢出
        const int32_t mask = d >> 31;  // d < 0 → -1
        m = (x & ~mask) | (m & mask);
    }
    return (int64_t)m;
}

int64_t ref_max(const Input& in) {
    int32_t m = in.a;
    for (size_t i = 0; i < in.n; ++i)
        if (in.v[i] > m) m = in.v[i];
    return (int64_t)m;
}

// ---------------- 绝对值之和 ----------------

int64_t abs_if_branch(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x < 0) {
            FZ_BARRIER();
            sum += -(int64_t)x;
        } else {
            FZ_BARRIER();
            sum += x;
        }
    }
    return sum;
}

int64_t abs_cmov(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        const int32_t mask = x >> 31;  // x < 0 → -1
        const int32_t v = (x ^ mask) - mask;
        sum += v;
    }
    return sum;
}

int64_t ref_abs(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        sum += (x < 0) ? -(int64_t)x : x;
    }
    return sum;
}

// ---------------- 截断到 [a, b] ----------------

int64_t clamp_if_branch(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x < in.a) {
            FZ_BARRIER();
            sum += in.a;
        } else if (x > in.b) {
            FZ_BARRIER();
            sum += in.b;
        } else {
            FZ_BARRIER();
            sum += x;
        }
    }
    return sum;
}

int64_t clamp_cmov(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        int32_t t = in.v[i];
        const int32_t lo_mask = -(int32_t)(t < in.a);
        t = (in.a & lo_mask) | (t & ~lo_mask);
        const int32_t hi_mask = -(int32_t)(t > in.b);
        t = (in.b & hi_mask) | (t & ~hi_mask);
        sum += t;
    }
    return sum;
}

int64_t ref_clamp(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        const int32_t t = (x < in.a) ? in.a : ((x > in.b) ? in.b : x);
        sum += t;
    }
    return sum;
}

// ---------------- 条件存储：dst[i] = x >= a ? x : b ----------------

int64_t cond_store_branch(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x >= in.a) {
            FZ_BARRIER();
            in.dst[i] = x;
        } else {
            FZ_BARRIER();
            in.dst[i] = in.b;
        }
        sum += in.dst[i];
    }
    return sum;
}

int64_t cond_store_cmov(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        const int32_t mask = -(int32_t)(x >= in.a);
        const int32_t value = (x & mask) | (in.b & ~mask);
        in.dst[i] = value;
        sum += value;
    }
    return sum;
}

int64_t ref_cond_store(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        in.dst[i] = (x >= in.a) ? x : in.b;
        sum += in.dst[i];
    }
    return sum;
}

}  // namespace fz
