// 无分支改写：查找表 / 算术 / 谓词（cmov）替换分支。
#include "branch_lab.h"

namespace fz {
namespace {

// mapToBucket：[0,50) → 0..4，其余 → -1（PERF 书 10.1 / 10.2 的例子）
inline int32_t bucket_of(int32_t x) { return (x >= 0 && x < 50) ? (x / 10) : -1; }

const int8_t kBuckets[50] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

FZ_NOINLINE int64_t compute_x(int32_t x) { return (int64_t)x * 3 + 1; }
FZ_NOINLINE int64_t compute_y(int32_t x) { return (int64_t)x * 5 + 2; }

}  // namespace

// ---------------- 分支链 vs 查找表 vs 算术 ----------------

int64_t map_ifchain(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if (x < 0) {
            FZ_BARRIER();
            sum += -1;
        } else if (x < 10) {
            FZ_BARRIER();
            sum += 0;
        } else if (x < 20) {
            FZ_BARRIER();
            sum += 1;
        } else if (x < 30) {
            FZ_BARRIER();
            sum += 2;
        } else if (x < 40) {
            FZ_BARRIER();
            sum += 3;
        } else if (x < 50) {
            FZ_BARRIER();
            sum += 4;
        } else {
            FZ_BARRIER();
            sum += -1;
        }
    }
    return sum;
}

int64_t map_lookup(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if ((uint32_t)x < 50u) {
            FZ_BARRIER();
            sum += kBuckets[x];
        } else {
            FZ_BARRIER();
            sum += -1;
        }
    }
    return sum;
}

int64_t map_arith(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t x = in.v[i];
        if ((uint32_t)x < 50u) {
            FZ_BARRIER();
            sum += x / 10;
        } else {
            FZ_BARRIER();
            sum += -1;
        }
    }
    return sum;
}

int64_t ref_map_ifchain(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += bucket_of(in.v[i]);
    return sum;
}

// ---------------- 谓词：if-else 调用 vs 两条都算再选 ----------------

int64_t predication_branch(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        if (in.b1[i]) {
            FZ_BARRIER();
            sum += compute_x(in.v[i]);
        } else {
            FZ_BARRIER();
            sum += compute_y(in.v[i]);
        }
    }
    return sum;
}

int64_t predication_select(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int64_t x = compute_x(in.v[i]);
        const int64_t y = compute_y(in.v[i]);
        sum += in.b1[i] ? x : y;
    }
    return sum;
}

int64_t ref_predication(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        sum += in.b1[i] ? ((int64_t)in.v[i] * 3 + 1) : ((int64_t)in.v[i] * 5 + 2);
    }
    return sum;
}

}  // namespace fz
