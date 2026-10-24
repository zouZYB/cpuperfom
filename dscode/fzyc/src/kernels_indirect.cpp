// 间接分支：switch 跳转表 / if-else 链 / 函数指针 / 虚函数。
#include "branch_lab.h"

namespace fz {
namespace {

// 映射：目标 k → 贡献 k*3+1（switch / if 链 / 参考实现三者一致）
inline int64_t bucket_value(int32_t k) { return (int64_t)k * 3 + 1; }

int64_t sw2(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        switch (in.idx[i]) {
            case 0: sum += bucket_value(0); break;
            case 1: sum += bucket_value(1); break;
            default: sum += bucket_value(0); break;
        }
    }
    return sum;
}

int64_t sw4(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        switch (in.idx[i]) {
            case 0: sum += bucket_value(0); break;
            case 1: sum += bucket_value(1); break;
            case 2: sum += bucket_value(2); break;
            case 3: sum += bucket_value(3); break;
            default: sum += bucket_value(0); break;
        }
    }
    return sum;
}

int64_t sw8(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        switch (in.idx[i]) {
            case 0: sum += bucket_value(0); break;
            case 1: sum += bucket_value(1); break;
            case 2: sum += bucket_value(2); break;
            case 3: sum += bucket_value(3); break;
            case 4: sum += bucket_value(4); break;
            case 5: sum += bucket_value(5); break;
            case 6: sum += bucket_value(6); break;
            case 7: sum += bucket_value(7); break;
            default: sum += bucket_value(0); break;
        }
    }
    return sum;
}

int64_t sw16(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        switch (in.idx[i]) {
            case 0: sum += bucket_value(0); break;
            case 1: sum += bucket_value(1); break;
            case 2: sum += bucket_value(2); break;
            case 3: sum += bucket_value(3); break;
            case 4: sum += bucket_value(4); break;
            case 5: sum += bucket_value(5); break;
            case 6: sum += bucket_value(6); break;
            case 7: sum += bucket_value(7); break;
            case 8: sum += bucket_value(8); break;
            case 9: sum += bucket_value(9); break;
            case 10: sum += bucket_value(10); break;
            case 11: sum += bucket_value(11); break;
            case 12: sum += bucket_value(12); break;
            case 13: sum += bucket_value(13); break;
            case 14: sum += bucket_value(14); break;
            case 15: sum += bucket_value(15); break;
            default: sum += bucket_value(0); break;
        }
    }
    return sum;
}

int64_t sw64(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        switch (in.idx[i]) {
#define FZ_CASE(K) case K: sum += bucket_value(K); break;
            FZ_CASE(0) FZ_CASE(1) FZ_CASE(2) FZ_CASE(3) FZ_CASE(4) FZ_CASE(5) FZ_CASE(6) FZ_CASE(7)
            FZ_CASE(8) FZ_CASE(9) FZ_CASE(10) FZ_CASE(11) FZ_CASE(12) FZ_CASE(13) FZ_CASE(14) FZ_CASE(15)
            FZ_CASE(16) FZ_CASE(17) FZ_CASE(18) FZ_CASE(19) FZ_CASE(20) FZ_CASE(21) FZ_CASE(22) FZ_CASE(23)
            FZ_CASE(24) FZ_CASE(25) FZ_CASE(26) FZ_CASE(27) FZ_CASE(28) FZ_CASE(29) FZ_CASE(30) FZ_CASE(31)
            FZ_CASE(32) FZ_CASE(33) FZ_CASE(34) FZ_CASE(35) FZ_CASE(36) FZ_CASE(37) FZ_CASE(38) FZ_CASE(39)
            FZ_CASE(40) FZ_CASE(41) FZ_CASE(42) FZ_CASE(43) FZ_CASE(44) FZ_CASE(45) FZ_CASE(46) FZ_CASE(47)
            FZ_CASE(48) FZ_CASE(49) FZ_CASE(50) FZ_CASE(51) FZ_CASE(52) FZ_CASE(53) FZ_CASE(54) FZ_CASE(55)
            FZ_CASE(56) FZ_CASE(57) FZ_CASE(58) FZ_CASE(59) FZ_CASE(60) FZ_CASE(61) FZ_CASE(62) FZ_CASE(63)
#undef FZ_CASE
            default: sum += bucket_value(0); break;
        }
    }
    return sum;
}

// if-else 链（对照 switch 跳转表）
int64_t chain2(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t k = in.idx[i];
        if (k == 0) {
            FZ_BARRIER();
            sum += bucket_value(0);
        } else {
            FZ_BARRIER();
            sum += bucket_value(1);
        }
    }
    return sum;
}

int64_t chain4(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t k = in.idx[i];
        if (k == 0) {
            FZ_BARRIER();
            sum += bucket_value(0);
        } else if (k == 1) {
            FZ_BARRIER();
            sum += bucket_value(1);
        } else if (k == 2) {
            FZ_BARRIER();
            sum += bucket_value(2);
        } else {
            FZ_BARRIER();
            sum += bucket_value(3);
        }
    }
    return sum;
}

int64_t chain8(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) {
        const int32_t k = in.idx[i];
        if (k == 0) {
            FZ_BARRIER();
            sum += bucket_value(0);
        } else if (k == 1) {
            FZ_BARRIER();
            sum += bucket_value(1);
        } else if (k == 2) {
            FZ_BARRIER();
            sum += bucket_value(2);
        } else if (k == 3) {
            FZ_BARRIER();
            sum += bucket_value(3);
        } else if (k == 4) {
            FZ_BARRIER();
            sum += bucket_value(4);
        } else if (k == 5) {
            FZ_BARRIER();
            sum += bucket_value(5);
        } else if (k == 6) {
            FZ_BARRIER();
            sum += bucket_value(6);
        } else {
            FZ_BARRIER();
            sum += bucket_value(7);
        }
    }
    return sum;
}

// ---------------- 函数指针目标 ----------------

FZ_NOINLINE int64_t tgt0(int32_t x) { return (int64_t)x * 2 + 0; }
FZ_NOINLINE int64_t tgt1(int32_t x) { return (int64_t)x * 2 + 1; }
FZ_NOINLINE int64_t tgt2(int32_t x) { return (int64_t)x * 2 + 2; }
FZ_NOINLINE int64_t tgt3(int32_t x) { return (int64_t)x * 2 + 3; }
FZ_NOINLINE int64_t tgt4(int32_t x) { return (int64_t)x * 2 + 4; }
FZ_NOINLINE int64_t tgt5(int32_t x) { return (int64_t)x * 2 + 5; }
FZ_NOINLINE int64_t tgt6(int32_t x) { return (int64_t)x * 2 + 6; }
FZ_NOINLINE int64_t tgt7(int32_t x) { return (int64_t)x * 2 + 7; }

using Call = int64_t (*)(int32_t);
const Call kTargets[8] = {tgt0, tgt1, tgt2, tgt3, tgt4, tgt5, tgt6, tgt7};

// ---------------- 虚函数目标 ----------------

struct VBase {
    virtual ~VBase() = default;
    virtual int64_t value(int32_t x) const = 0;
};

struct V0 : VBase {
    int64_t value(int32_t x) const override { return (int64_t)x + 1; }
};
struct V1 : VBase {
    int64_t value(int32_t x) const override { return (int64_t)x + 4; }
};
struct V2 : VBase {
    int64_t value(int32_t x) const override { return (int64_t)x + 7; }
};
struct V3 : VBase {
    int64_t value(int32_t x) const override { return (int64_t)x + 10; }
};

const V0 g_v0;
const V1 g_v1;
const V2 g_v2;
const V3 g_v3;
const VBase* const kObjs[4] = {&g_v0, &g_v1, &g_v2, &g_v3};

}  // namespace

// ---------------- 对外入口 ----------------

int64_t switch_ways(const Input& in) {
    switch (in.p1) {
        case 2: return sw2(in);
        case 4: return sw4(in);
        case 8: return sw8(in);
        case 16: return sw16(in);
        default: return sw64(in);
    }
}

int64_t ifchain_ways(const Input& in) {
    switch (in.p1) {
        case 2: return chain2(in);
        case 4: return chain4(in);
        default: return chain8(in);
    }
}

int64_t callptr(const Input& in) {
    int32_t ways = in.p1;
    if (ways < 1) ways = 1;
    if (ways > 8) ways = 8;
    const uint32_t mask = (uint32_t)(ways - 1);
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += kTargets[in.idx[i] & mask](in.v[i]);
    return sum;
}

int64_t virtual_call(const Input& in) {
    int32_t ways = in.p1;
    if (ways < 1) ways = 1;
    if (ways > 4) ways = 4;
    const uint32_t mask = (uint32_t)(ways - 1);
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += kObjs[in.idx[i] & mask]->value(in.v[i]);
    return sum;
}

// ---------------- 参考实现 ----------------

int64_t ref_switch_ways(const Input& in) {
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += bucket_value(in.idx[i]);
    return sum;
}

int64_t ref_ifchain_ways(const Input& in) { return ref_switch_ways(in); }

int64_t ref_callptr(const Input& in) {
    int32_t ways = in.p1;
    if (ways < 1) ways = 1;
    if (ways > 8) ways = 8;
    const uint32_t mask = (uint32_t)(ways - 1);
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i)
        sum += (int64_t)in.v[i] * 2 + (int64_t)(in.idx[i] & mask);
    return sum;
}

int64_t ref_virtual_call(const Input& in) {
    int32_t ways = in.p1;
    if (ways < 1) ways = 1;
    if (ways > 4) ways = 4;
    const uint32_t mask = (uint32_t)(ways - 1);
    int64_t sum = 0;
    for (size_t i = 0; i < in.n; ++i) sum += (int64_t)in.v[i] + 1 + 3 * (int64_t)(in.idx[i] & mask);
    return sum;
}

}  // namespace fz
