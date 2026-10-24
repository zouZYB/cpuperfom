// 正确性测试：注册表自检 + 生成器自检 + "被测内核 vs 独立参考实现"逐场景比对。
//
// 覆盖面（"情况要全"）：
//   1) 每个内核 × n 边界（0/1/2/3/4/5/7/8/15/16/17/31/32/33/63/64/65/127/128/129/
//      255/256/257/1000/4096/4097）
//   2) 每个内核 × 全部 19 种数据分布
//   3) 每个内核 × 全部 19 种分支模式
//   4) 每个内核 × 全部 4 种相关性
//   5) 每个内核 × 2 个 seed（可复现性）
//   6) 结构参数：p1 ∈ {1,2,4,8}、p2 ∈ {0,1,2}、a/b 多组（含非法区间与 0）
//   7) 数据/分支生成器本身的不变量（排序、偏斜、周期、游程、范围、确定性）
//   8) 条件存储类内核额外逐元素比对 dst
#include "branch_lab.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

size_t g_checks = 0;
size_t g_fails = 0;
const size_t kMaxReport = 20;
std::vector<std::string> g_fail_msgs;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_fails;
        if (g_fail_msgs.size() < kMaxReport) g_fail_msgs.push_back(what);
    }
}

void section(const char* name) { std::printf("\n-- %s --\n", name); }

std::string cfg_brief(const fz::Config& c) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "n=%zu,order=%s,pattern=%s,corr=%s,seed=%u,a=%d,b=%d,p1=%d,p2=%d",
                  c.n, fz::order_name(c.order), fz::pattern_name(c.pattern), fz::corr_name(c.corr),
                  c.seed, c.a, c.b, c.p1, c.p2);
    return std::string(buf);
}

bool same_vector(const std::vector<int32_t>& a, const std::vector<int32_t>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}
bool same_bits(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// ---------------------------------------------------------------------------
// 生成器自检
// ---------------------------------------------------------------------------
void test_generators() {
    section("数据生成器");
    const size_t kN = 1024;
    std::vector<int32_t> v;
    for (int oi = 0; oi < (int)fz::DataOrder::COUNT; ++oi) {
        const auto o = (fz::DataOrder)oi;
        fz::make_data(o, kN, 7, v);
        check(v.size() == kN, std::string("make_data size ") + fz::order_name(o));
        bool ok = true;
        switch (o) {
            case fz::DataOrder::SortedAsc:
                ok = std::is_sorted(v.begin(), v.end());
                break;
            case fz::DataOrder::SortedDesc:
                ok = std::is_sorted(v.begin(), v.end(), std::greater<int32_t>());
                break;
            case fz::DataOrder::AllTrue:
                for (int32_t x : v) ok = ok && (x >= 128 && x <= 255);
                break;
            case fz::DataOrder::AllFalse:
                for (int32_t x : v) ok = ok && (x >= 0 && x < 128);
                break;
            case fz::DataOrder::AllPositive:
                for (int32_t x : v) ok = ok && (x > 0);
                break;
            case fz::DataOrder::AllNegative:
                for (int32_t x : v) ok = ok && (x < 0);
                break;
            case fz::DataOrder::Alternating:
                for (size_t i = 0; i < kN; ++i) ok = ok && (v[i] == ((i & 1u) ? 0 : 255));
                break;
            case fz::DataOrder::AlternatingSign:
                for (size_t i = 0; i < kN; ++i) ok = ok && (v[i] == ((i & 1u) ? -255 : 255));
                break;
            case fz::DataOrder::Block4:
            case fz::DataOrder::Block8:
            case fz::DataOrder::Block16:
            case fz::DataOrder::Block32:
            case fz::DataOrder::Block64:
            case fz::DataOrder::Block256: {
                const size_t k = (o == fz::DataOrder::Block4)     ? 4
                                 : (o == fz::DataOrder::Block8)   ? 8
                                 : (o == fz::DataOrder::Block16)  ? 16
                                 : (o == fz::DataOrder::Block32)  ? 32
                                 : (o == fz::DataOrder::Block64)  ? 64
                                                                  : 256;
                for (size_t i = 0; i < kN; ++i)
                    ok = ok && (v[i] == (((i / k) & 1u) ? 0 : 255));
                break;
            }
            case fz::DataOrder::Skew90:
            case fz::DataOrder::Skew99: {
                size_t ones = 0;
                for (int32_t x : v) ones += (x == 255) ? 1 : 0;
                const double ratio = (double)ones / (double)kN;
                ok = (o == fz::DataOrder::Skew90) ? (ratio > 0.85 && ratio < 0.95)
                                                  : (ratio > 0.97);
                break;
            }
            case fz::DataOrder::Bucket50:
                for (int32_t x : v) ok = ok && (x >= 0 && x < 50);
                break;
            case fz::DataOrder::Wide256:
                for (int32_t x : v) ok = ok && (x >= 0 && x < 256);
                break;
            case fz::DataOrder::Random:
                for (int32_t x : v) ok = ok && (x >= -256 && x <= 255);
                break;
            default:
                break;
        }
        check(ok, std::string("make_data 值域/形态 ") + fz::order_name(o));
        std::vector<int32_t> again;
        fz::make_data(o, kN, 7, again);
        check(same_vector(v, again), std::string("make_data 确定性 ") + fz::order_name(o));
    }
    // 不同 seed 变化（随机类）
    {
        std::vector<int32_t> a, b;
        fz::make_data(fz::DataOrder::Random, kN, 1, a);
        fz::make_data(fz::DataOrder::Random, kN, 2, b);
        check(!same_vector(a, b), "不同 seed 的数据应不同");
    }
    // n=0 / n=1
    for (int oi = 0; oi < (int)fz::DataOrder::COUNT; ++oi) {
        fz::make_data((fz::DataOrder)oi, 0, 1, v);
        check(v.empty(), "make_data n=0");
    }

    section("分支模式生成器");
    std::vector<uint8_t> bits;
    const size_t kB = 65536;  // 足够覆盖最长周期（32768）
    struct PeriodSpec {
        fz::BitPattern p;
        size_t period;
    };
    const PeriodSpec kPeriods[] = {
        {fz::BitPattern::Period3, 3},       {fz::BitPattern::Period4, 4},
        {fz::BitPattern::Period5, 5},       {fz::BitPattern::Period7, 7},
        {fz::BitPattern::Period8, 8},       {fz::BitPattern::Period16, 16},
        {fz::BitPattern::Period32, 32},     {fz::BitPattern::Period64, 64},
        {fz::BitPattern::Period128, 128},   {fz::BitPattern::Period256, 256},
        {fz::BitPattern::Period512, 512},   {fz::BitPattern::Period1024, 1024},
        {fz::BitPattern::Period2048, 2048}, {fz::BitPattern::Period4096, 4096},
        {fz::BitPattern::Period8192, 8192}, {fz::BitPattern::Period16384, 16384},
        {fz::BitPattern::Period32768, 32768},
    };
    for (int pi = 0; pi < (int)fz::BitPattern::COUNT; ++pi) {
        const auto p = (fz::BitPattern)pi;
        fz::make_bits(p, kB, 7, bits);
        check(bits.size() == kB, std::string("make_bits size ") + fz::pattern_name(p));
        bool in_range = true;
        for (uint8_t x : bits) in_range = in_range && (x <= 1);
        check(in_range, std::string("make_bits 取值 ") + fz::pattern_name(p));
        bool handled = false;
        for (const auto& ps : kPeriods) {
            if (ps.p == p) {
                // 基序列是随机的，最小周期可能是 period 的因子，这里只要求"该周期成立"
                check(fz::is_periodic(bits, ps.period),
                      std::string("周期检测 ") + fz::pattern_name(p));
                handled = true;
                break;
            }
        }
        if (!handled) {
            switch (p) {
                case fz::BitPattern::AllTrue:
                    check(fz::count_ones(bits) == kB, "AllTrue");
                    break;
                case fz::BitPattern::AllFalse:
                    check(fz::count_ones(bits) == 0, "AllFalse");
                    break;
                case fz::BitPattern::Alternate:
                    check(fz::is_periodic(bits, 2) && !fz::is_periodic(bits, 1), "Alternate 周期 2");
                    break;
                case fz::BitPattern::Run3:
                    check(fz::longest_run(bits) == 3, "Run3 游程");
                    break;
                case fz::BitPattern::Run8:
                    check(fz::longest_run(bits) == 8, "Run8 游程");
                    break;
                case fz::BitPattern::Run32:
                    check(fz::longest_run(bits) == 32, "Run32 游程");
                    break;
                case fz::BitPattern::Random: {
                    const double ratio = (double)fz::count_ones(bits) / (double)kB;
                    check(ratio > 0.42 && ratio < 0.58, "Random 比例 ~50%");
                    break;
                }
                case fz::BitPattern::PhaseSwitch: {
                    bool ok = true;
                    for (size_t i = 0; i < kB / 2; ++i) ok = ok && (bits[i] == 1);
                    check(ok, "PhaseSwitch 前半为 1");
                    break;
                }
                default:
                    break;
            }
        }
        std::vector<uint8_t> again;
        fz::make_bits(p, kB, 7, again);
        check(same_bits(bits, again), std::string("make_bits 确定性 ") + fz::pattern_name(p));
    }

    section("相关位生成器");
    std::vector<uint8_t> b1, b2;
    fz::make_bits(fz::BitPattern::Random, 512, 3, b1);
    for (int ci = 0; ci < (int)fz::Corr::COUNT; ++ci) {
        const auto c = (fz::Corr)ci;
        fz::make_corr_bits(c, b1, 512, 3, b2);
        check(b2.size() == 512, std::string("make_corr_bits size ") + fz::corr_name(c));
        bool ok = true;
        for (size_t i = 0; i < 512; ++i) {
            switch (c) {
                case fz::Corr::Same: ok = ok && (b2[i] == b1[i]); break;
                case fz::Corr::Not: ok = ok && (b2[i] == (b1[i] ? 0 : 1)); break;
                case fz::Corr::XorPrev:
                    ok = ok && (b2[i] == (uint8_t)((b1[i] ? 1 : 0) ^
                                                   ((i == 0) ? 0 : (b1[i - 1] ? 1 : 0))));
                    break;
                default: break;
            }
        }
        if (c != fz::Corr::Independent) check(ok, std::string("相关位语义 ") + fz::corr_name(c));
        if (c == fz::Corr::Independent) {
            const size_t ones = fz::count_ones(b2);
            check(ones > 140 && ones < 372, "Independent 比例 ~50%");
        }
    }

    section("跳转目标 / 循环次数生成器");
    std::vector<uint8_t> idx;
    for (int mode = 0; mode < 3; ++mode) {
        fz::make_idx(1000, 5, 8, mode, idx);
        bool ok = true;
        for (size_t i = 0; i < idx.size(); ++i) {
            ok = ok && (idx[i] < 8);
            if (mode == 0) ok = ok && (idx[i] == (uint8_t)(i % 8));
            if (mode == 2) ok = ok && (idx[i] == 0);
        }
        check(ok, "make_idx mode=" + std::to_string(mode));
    }
    std::vector<int32_t> trips;
    fz::make_trips(1000, 5, 4, 0, trips);
    bool ok = true;
    for (int32_t t : trips) ok = ok && (t == 4);
    check(ok, "make_trips 恒定");
    fz::make_trips(1000, 5, 4, 1, trips);
    ok = true;
    for (size_t i = 0; i < trips.size(); ++i) ok = ok && (trips[i] == ((i & 1u) ? 12 : 4));
    check(ok, "make_trips 交替");
    fz::make_trips(1000, 5, 4, 2, trips);
    ok = true;
    for (int32_t t : trips) ok = ok && (t >= 1 && t <= 4);
    check(ok, "make_trips 随机范围");

    section("prepare");
    fz::Config c;
    c.n = 100;
    c.a = 7;
    c.b = 9;
    c.p1 = 3;
    c.p2 = 1;
    const fz::Prepared p1 = fz::prepare(c);
    const fz::Prepared p2 = fz::prepare(c);
    check(p1.v.size() == 100 && p1.b1.size() == 100 && p1.b2.size() == 100 && p1.idx.size() == 100 &&
              p1.trips.size() == 100 && p1.dst.size() == 100,
          "prepare 数组长度");
    check(p1.in.n == 100 && p1.in.a == 7 && p1.in.b == 9 && p1.in.p1 == 3 && p1.in.p2 == 1,
          "prepare Input 字段");
    check(same_vector(p1.v, p2.v) && same_bits(p1.b1, p2.b1) && same_bits(p1.b2, p2.b2),
          "prepare 确定性");
    const fz::Prepared p0 = fz::prepare(fz::Config{});
    check(p0.in.n == fz::kDefaultN, "prepare 默认 n");
}

// ---------------------------------------------------------------------------
// 注册表自检
// ---------------------------------------------------------------------------
void test_registry() {
    section("注册表");
    const auto& list = fz::cases();
    check(list.size() >= 200, "场景数 >= 200（实际 " + std::to_string(list.size()) + "）");
    std::set<std::string> names;
    bool all_ok = true;
    for (const auto& c : list) {
        all_ok = all_ok && (c.name != nullptr) && (c.fn != nullptr) && (c.ref != nullptr);
        all_ok = all_ok && (c.cfg.n > 0);
        names.insert(c.name);
    }
    check(all_ok, "每个 case 的 name/fn/ref/cfg 非空");
    check(names.size() == list.size(), "case 名称唯一");
    for (size_t g = 0; g < fz::group_count(); ++g) {
        check(!fz::cases_of((fz::Group)g).empty(),
              std::string("组非空: ") + fz::group_name((fz::Group)g));
    }
    check(fz::find_case(list.front().name) == &list.front(), "find_case 首元素");
    check(fz::find_case(list.back().name) == &list.back(), "find_case 末元素");
    check(fz::find_case("no/such/case") == nullptr, "find_case 未命中");
}

// ---------------------------------------------------------------------------
// 逐场景语义比对
// ---------------------------------------------------------------------------
bool kernel_is(const fz::Case& c, fz::Fn fn) { return c.fn == fn; }

size_t run_case(const fz::Case& c, const fz::Config& cfg, bool check_dst) {
    const fz::Prepared p = fz::prepare(cfg);
    size_t fails_before = g_fails;
    const int64_t got = c.fn(p.in);
    const int64_t want = c.ref(p.in);
    check(got == want, std::string(c.name) + " | " + cfg_brief(cfg) + " | got=" +
                           std::to_string((long long)got) + " want=" +
                           std::to_string((long long)want));
    if (check_dst) {
        // 参考实现写出的 dst 作为期望值，被测实现重算后再逐元素比对
        fz::Prepared q = fz::prepare(cfg);
        (void)c.ref(q.in);
        const std::vector<int32_t> expected = q.dst;
        fz::Prepared r = fz::prepare(cfg);
        (void)c.fn(r.in);
        bool ok = (expected.size() == r.dst.size());
        for (size_t i = 0; ok && i < expected.size(); ++i) ok = (expected[i] == r.dst[i]);
        check(ok, std::string(c.name) + " dst 逐元素 | " + cfg_brief(cfg));
    }
    return g_fails - fails_before;
}

void test_case_semantics() {
    section("逐场景语义（被测内核 vs 参考实现）");
    const size_t kEdges[] = {0,  1,  2,  3,  4,   5,   7,   8,   15,  16,  17,   31,   32,
                             33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 1000, 4096, 4097};
    size_t total_checks_before = g_checks;

    for (size_t g = 0; g < fz::group_count(); ++g) {
        const auto& group = fz::cases_of((fz::Group)g);
        size_t group_fails_before = g_fails;
        for (const fz::Case* cp : group) {
            const fz::Case& c = *cp;
            const bool is_store = kernel_is(c, fz::cond_store_branch) || kernel_is(c, fz::cond_store_cmov);
            const bool is_nested_loop = kernel_is(c, fz::nested_loop);
            const bool is_clamp = kernel_is(c, fz::clamp_if_branch) || kernel_is(c, fz::clamp_cmov);

            // 1) n 边界
            for (size_t n : kEdges) {
                fz::Config cfg = c.cfg;
                cfg.n = n;
                run_case(c, cfg, is_store && n > 0);
            }
            // 2) 全部数据分布
            for (int oi = 0; oi < (int)fz::DataOrder::COUNT; ++oi) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                cfg.order = (fz::DataOrder)oi;
                run_case(c, cfg, is_store);
            }
            // 3) 全部分支模式
            for (int pi = 0; pi < (int)fz::BitPattern::COUNT; ++pi) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                cfg.pattern = (fz::BitPattern)pi;
                run_case(c, cfg, is_store);
            }
            // 4) 相关性
            for (int ci = 0; ci < (int)fz::Corr::COUNT; ++ci) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                cfg.corr = (fz::Corr)ci;
                run_case(c, cfg, is_store);
            }
            // 5) seed
            for (uint32_t seed : {1u, 7u}) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                cfg.seed = seed;
                run_case(c, cfg, is_store);
            }
            // 6) 结构参数
            for (int32_t p1 : {1, 2, 4, 8}) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                cfg.p1 = p1;
                run_case(c, cfg, is_store);
            }
            for (int32_t p2 : {0, 1, 2}) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                cfg.p2 = p2;
                run_case(c, cfg, is_store);
            }
            for (int32_t a : {0, 1, 50, 128, 255}) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                if (is_nested_loop) {
                    cfg.a = (a > 4) ? 4 : a;  // 嵌套循环用 a 作外层次数，避免爆炸
                } else if (is_clamp) {
                    cfg.a = a - 200;  // clamp 要求 a <= b
                    cfg.b = 200;
                } else {
                    cfg.a = a;
                }
                run_case(c, cfg, is_store);
            }
            for (int32_t b : {0, 50, 255}) {
                fz::Config cfg = c.cfg;
                cfg.n = 257;
                if (is_nested_loop) {
                    cfg.b = (b > 4) ? 4 : b;
                } else if (is_clamp) {
                    cfg.a = -255;
                    cfg.b = b - 100;
                } else {
                    cfg.b = b;
                }
                run_case(c, cfg, is_store);
            }
        }
        char line[160];
        std::snprintf(line, sizeof(line), "%-12s cases=%3zu checks=%5zu", fz::group_name((fz::Group)g),
                      group.size(), g_checks - total_checks_before);
        const bool ok = (g_fails == group_fails_before);
        std::printf("[%s] %s%s\n", ok ? "PASS" : "FAIL", line, ok ? "" : "  <-- 有失败");
        total_checks_before = g_checks;
    }
}

}  // namespace

int main() {
    std::printf("== 分支预测实验场 正确性测试 ==\n");
    std::printf("场景数: %zu\n", fz::cases().size());

    test_generators();
    test_registry();
    test_case_semantics();

    std::printf("\n");
    if (g_fails != 0) {
        std::printf("失败样例（最多 %zu 条）:\n", kMaxReport);
        for (const auto& m : g_fail_msgs) std::printf("  - %s\n", m.c_str());
        std::printf("\n存在失败断言: %zu / %zu\n", g_fails, g_checks);
        return 1;
    }
    std::printf("全部通过: 共 %zu 条断言，0 个失败\n", g_checks);
    return 0;
}
