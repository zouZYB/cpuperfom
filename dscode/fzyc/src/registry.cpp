// 场景矩阵：把 (内核 × 数据分布 × 分支模式 × 结构参数) 组合成注册表。
#include "branch_lab.h"

#include <deque>
#include <string>
#include <vector>

namespace fz {

// ---------------------------------------------------------------------------
// 名称表
// ---------------------------------------------------------------------------

const char* group_name(Group g) {
    switch (g) {
        case Group::Data: return "data";
        case Group::Pattern: return "pattern";
        case Group::Correlated: return "corr";
        case Group::Density: return "density";
        case Group::Indirect: return "indirect";
        case Group::Loop: return "loop";
        case Group::Branchless: return "branchless";
        case Group::Hint: return "hint";
        case Group::Call: return "call";
        case Group::Store: return "store";
        case Group::Random: return "random";
        case Group::Align: return "align";
        default: return "?";
    }
}

const char* order_name(DataOrder o) {
    switch (o) {
        case DataOrder::Random: return "random";
        case DataOrder::SortedAsc: return "sorted_asc";
        case DataOrder::SortedDesc: return "sorted_desc";
        case DataOrder::AllTrue: return "all_true";
        case DataOrder::AllFalse: return "all_false";
        case DataOrder::AllPositive: return "all_positive";
        case DataOrder::AllNegative: return "all_negative";
        case DataOrder::Alternating: return "alternating";
        case DataOrder::AlternatingSign: return "alternating_sign";
        case DataOrder::Block4: return "block4";
        case DataOrder::Block8: return "block8";
        case DataOrder::Block16: return "block16";
        case DataOrder::Block32: return "block32";
        case DataOrder::Block64: return "block64";
        case DataOrder::Block256: return "block256";
        case DataOrder::Skew90: return "skew90";
        case DataOrder::Skew99: return "skew99";
        case DataOrder::Bucket50: return "bucket50";
        case DataOrder::Wide256: return "wide256";
        default: return "?";
    }
}

const char* pattern_name(BitPattern p) {
    switch (p) {
        case BitPattern::AllTrue: return "all_true";
        case BitPattern::AllFalse: return "all_false";
        case BitPattern::Alternate: return "alternate";
        case BitPattern::Period3: return "period3";
        case BitPattern::Period4: return "period4";
        case BitPattern::Period5: return "period5";
        case BitPattern::Period7: return "period7";
        case BitPattern::Period8: return "period8";
        case BitPattern::Period16: return "period16";
        case BitPattern::Period32: return "period32";
        case BitPattern::Period64: return "period64";
        case BitPattern::Period128: return "period128";
        case BitPattern::Period256: return "period256";
        case BitPattern::Period512: return "period512";
        case BitPattern::Period1024: return "period1024";
        case BitPattern::Period2048: return "period2048";
        case BitPattern::Period4096: return "period4096";
        case BitPattern::Period8192: return "period8192";
        case BitPattern::Period16384: return "period16384";
        case BitPattern::Period32768: return "period32768";
        case BitPattern::Random: return "random";
        case BitPattern::Run3: return "run3";
        case BitPattern::Run8: return "run8";
        case BitPattern::Run32: return "run32";
        case BitPattern::PhaseSwitch: return "phase_switch";
        default: return "?";
    }
}

const char* corr_name(Corr c) {
    switch (c) {
        case Corr::Same: return "same";
        case Corr::Not: return "not";
        case Corr::Independent: return "independent";
        case Corr::XorPrev: return "xor_prev";
        default: return "?";
    }
}

namespace {

struct Builder {
    std::deque<std::string> owned;
    std::vector<Case> list;

    const char* keep(std::string s) {
        owned.push_back(std::move(s));
        return owned.back().c_str();
    }

    void add(Group g, const char* kernel, const std::string& variant, Fn fn, Fn ref,
             const Config& cfg, const char* note) {
        Case c;
        c.name = keep(std::string(group_name(g)) + "/" + kernel + "/" + variant);
        c.group = g;
        c.variant = keep(variant);
        c.fn = fn;
        c.ref = ref;
        c.cfg = cfg;
        c.note = note;
        list.push_back(c);
    }
};

std::string with_n(const Config& c) {
    if (c.n == kDefaultN) return std::string();
    return ",n=" + std::to_string(c.n);
}

void build_into(Builder& b) {
    const char* kModes[] = {"seq", "random", "same"};
    const DataOrder kAllOrders[] = {
        DataOrder::Random,      DataOrder::SortedAsc,  DataOrder::SortedDesc,
        DataOrder::AllTrue,     DataOrder::AllFalse,   DataOrder::Alternating,
        DataOrder::Block4,      DataOrder::Block8,     DataOrder::Block16,
        DataOrder::Block32,     DataOrder::Block64,    DataOrder::Block256,
    };

    // ---------------- data：数据相关条件分支 ----------------
    {
        const Group g = Group::Data;
        const char* kThreshNote = "阈值 a=128：排序后方向可预测，随机构成 ~50% 误预测";
        for (DataOrder o : kAllOrders) {
            Config c;
            c.order = o;
            const std::string v = std::string("order=") + order_name(o);
            b.add(g, "sum_if_branch", v, sum_if_branch, ref_sum_if, c, kThreshNote);
            b.add(g, "sum_if_cmov", v, sum_if_cmov, ref_sum_if, c, "掩码版无分支");
        }
        // 大数组：分支预测与缓存效应的叠加
        for (DataOrder o : {DataOrder::Random, DataOrder::SortedAsc}) {
            Config c;
            c.order = o;
            c.n = 1u << 20;
            const std::string v = std::string("order=") + order_name(o) + with_n(c);
            b.add(g, "sum_if_branch", v, sum_if_branch, ref_sum_if, c, "1M 元素（DRAM）");
        }
        for (DataOrder o : {DataOrder::Random, DataOrder::SortedAsc, DataOrder::AllTrue,
                            DataOrder::Alternating, DataOrder::Block8}) {
            Config c;
            c.order = o;
            const std::string v = std::string("order=") + order_name(o);
            b.add(g, "count_if_branch", v, count_if_branch, ref_count_if, c, "");
            b.add(g, "count_if_cmov", v, count_if_cmov, ref_count_if, c, "");
        }
        for (DataOrder o : {DataOrder::Random, DataOrder::SortedAsc, DataOrder::AllTrue,
                            DataOrder::Alternating}) {
            Config c;
            c.order = o;
            const std::string v = std::string("order=") + order_name(o);
            b.add(g, "max_if_branch", v, max_if_branch, ref_max, c, "初值 a=128");
            b.add(g, "max_cmov", v, max_cmov, ref_max, c, "算术掩码求最大");
        }
        for (DataOrder o : {DataOrder::Random, DataOrder::AlternatingSign, DataOrder::AllPositive,
                            DataOrder::AllNegative}) {
            Config c;
            c.order = o;
            const std::string v = std::string("order=") + order_name(o);
            b.add(g, "abs_if_branch", v, abs_if_branch, ref_abs, c, "");
            b.add(g, "abs_cmov", v, abs_cmov, ref_abs, c, "");
        }
        for (DataOrder o : {DataOrder::Random, DataOrder::SortedAsc, DataOrder::Alternating,
                            DataOrder::Block8}) {
            Config c;
            c.order = o;
            c.a = -100;
            c.b = 100;
            const std::string v = std::string("order=") + order_name(o);
            b.add(g, "clamp_if_branch", v, clamp_if_branch, ref_clamp, c, "区间 [-100,100]");
            b.add(g, "clamp_cmov", v, clamp_cmov, ref_clamp, c, "");
        }
    }

    // ---------------- store：条件存储 ----------------
    for (DataOrder o : {DataOrder::Random, DataOrder::SortedAsc, DataOrder::Alternating}) {
        Config c;
        c.order = o;
        c.a = 128;
        c.b = 0;
        const std::string v = std::string("order=") + order_name(o);
        b.add(Group::Store, "cond_store_branch", v, cond_store_branch, ref_cond_store, c,
              "dst[i] = v[i] >= 128 ? v[i] : 0");
        b.add(Group::Store, "cond_store_cmov", v, cond_store_cmov, ref_cond_store, c, "");
    }

    // ---------------- pattern：周期 / 随机模式 ----------------
    {
        const BitPattern kAllPatterns[] = {
            BitPattern::AllTrue,    BitPattern::AllFalse,   BitPattern::Alternate,
            BitPattern::Period3,    BitPattern::Period4,    BitPattern::Period5,
            BitPattern::Period7,    BitPattern::Period8,    BitPattern::Period16,
            BitPattern::Period32,   BitPattern::Period64,   BitPattern::Period128,
            BitPattern::Period256,  BitPattern::Period512,  BitPattern::Period1024,
            BitPattern::Period2048, BitPattern::Period4096, BitPattern::Period8192,
            BitPattern::Period16384, BitPattern::Period32768,
            BitPattern::Random,
            BitPattern::Run3,       BitPattern::Run8,       BitPattern::Run32,
            BitPattern::PhaseSwitch,
        };
        for (BitPattern p : kAllPatterns) {
            Config c;
            c.pattern = p;
            b.add(Group::Pattern, "pattern_sum", std::string("pattern=") + pattern_name(p),
                  pattern_sum, ref_pattern_sum, c, "局部历史预测器：小周期可学、大周期/随机不可学");
        }
        for (BitPattern p : {BitPattern::AllTrue, BitPattern::Alternate, BitPattern::Period4,
                             BitPattern::Random, BitPattern::Run8}) {
            Config c;
            c.pattern = p;
            c.corr = Corr::Independent;
            b.add(Group::Pattern, "pattern_two", std::string("pattern=") + pattern_name(p) +
                                                      ",corr=independent",
                  pattern_two, ref_pattern_two, c, "每元素两个独立分支");
        }
        for (int32_t depth : {2, 3, 4, 8}) {
            Config c;
            c.pattern = BitPattern::Random;
            c.p1 = depth;
            b.add(Group::Pattern, "history_chain",
                  "pattern=random,depth=" + std::to_string(depth), history_chain,
                  ref_history_chain, c, "连续 depth 个 1 才跳转：依赖全局历史");
        }
        for (BitPattern p : {BitPattern::AllTrue, BitPattern::Run8}) {
            Config c;
            c.pattern = p;
            c.p1 = 4;
            b.add(Group::Pattern, "history_chain",
                  std::string("pattern=") + pattern_name(p) + ",depth=4", history_chain,
                  ref_history_chain, c, "");
        }
    }

    // ---------------- corr：相关分支 ----------------
    {
        for (Corr corr : {Corr::Same, Corr::Not, Corr::Independent, Corr::XorPrev}) {
            Config c;
            c.pattern = BitPattern::Random;
            c.corr = corr;
            b.add(Group::Correlated, "nested_corr",
                  std::string("pattern=random,corr=") + corr_name(corr), nested_corr,
                  ref_nested_corr, c, "内外层相关：same/not 可被 TAGE 学习，independent 不行");
        }
        for (BitPattern p : {BitPattern::Alternate, BitPattern::Period4, BitPattern::Run8}) {
            Config c;
            c.pattern = p;
            c.corr = Corr::Independent;
            b.add(Group::Correlated, "nested_corr",
                  std::string("pattern=") + pattern_name(p) + ",corr=independent", nested_corr,
                  ref_nested_corr, c, "");
        }
        for (BitPattern p : {BitPattern::Random, BitPattern::Run8}) {
            Config c;
            c.pattern = p;
            c.corr = Corr::XorPrev;
            b.add(Group::Correlated, "xor_prev", std::string("pattern=") + pattern_name(p),
                  xor_prev, ref_xor_prev, c, "分支条件 = 相邻两位异或");
        }
    }

    // ---------------- density：分支密度 ----------------
    for (int32_t reps : {1, 2, 3, 4, 8}) {
        for (BitPattern p : {BitPattern::Alternate, BitPattern::Random, BitPattern::Period4}) {
            Config c;
            c.p1 = reps;
            c.pattern = p;
            b.add(Group::Density, "density",
                  "reps=" + std::to_string(reps) + ",pattern=" + pattern_name(p), density,
                  ref_density, c, "每元素 reps 个分支");
        }
    }

    // ---------------- indirect：间接分支 / 目标预测 ----------------
    {
        for (int32_t ways : {2, 4, 8, 16, 64}) {
            for (int32_t mode : {0, 1, 2}) {
                Config c;
                c.p1 = ways;
                c.p2 = mode;
                b.add(Group::Indirect, "switch_ways",
                      "ways=" + std::to_string(ways) + ",mode=" + kModes[mode], switch_ways,
                      ref_switch_ways, c, "跳转表目标预测（BTB）");
            }
        }
        for (int32_t ways : {2, 4, 8}) {
            for (int32_t mode : {0, 1}) {
                Config c;
                c.p1 = ways;
                c.p2 = mode;
                b.add(Group::Indirect, "ifchain_ways",
                      "ways=" + std::to_string(ways) + ",mode=" + kModes[mode], ifchain_ways,
                      ref_ifchain_ways, c, "if-else 链：多次条件分支 vs 跳转表");
            }
        }
        for (int32_t ways : {2, 4, 8}) {
            for (int32_t mode : {0, 1}) {
                Config c;
                c.p1 = ways;
                c.p2 = mode;
                b.add(Group::Indirect, "callptr",
                      "ways=" + std::to_string(ways) + ",mode=" + kModes[mode], callptr,
                      ref_callptr, c, "函数指针间接调用");
            }
        }
        for (int32_t ways : {1, 2, 4}) {
            for (int32_t mode : {0, 1}) {
                Config c;
                c.p1 = ways;
                c.p2 = mode;
                b.add(Group::Indirect, "virtual_call",
                      "ways=" + std::to_string(ways) + ",mode=" + kModes[mode], virtual_call,
                      ref_virtual_call, c, "虚函数间接调用");
            }
        }
    }

    // ---------------- loop：循环边界 ----------------
    {
        const char* kModes[] = {"const", "alternate", "random"};
        for (int32_t trip : {1, 2, 4, 8, 16}) {
            for (int32_t mode : {0, 1, 2}) {
                Config c;
                c.a = trip;
                c.p2 = mode;
                b.add(Group::Loop, "inner_trip",
                      "trip=" + std::to_string(trip) + ",mode=" + kModes[mode], inner_trip,
                      ref_inner_trip, c, "内层循环次数：循环预测器");
            }
        }
        for (auto pr : {std::pair<int32_t, int32_t>{2, 4}, {4, 4}, {8, 8}, {4, 16}}) {
            Config c;
            c.n = 4096;
            c.a = pr.first;
            c.b = pr.second;
            b.add(Group::Loop, "nested_loop",
                  "outer=" + std::to_string(pr.first) + ",inner=" + std::to_string(pr.second),
                  nested_loop, ref_nested_loop, c, "");
        }
        for (int32_t k : {1, 2, 4, 8}) {
            for (BitPattern p : {BitPattern::Alternate, BitPattern::Random}) {
                Config c;
                c.p1 = k;
                c.pattern = p;
                b.add(Group::Loop, "unroll",
                      "unroll=" + std::to_string(k) + ",pattern=" + pattern_name(p), unroll,
                      ref_unroll, c, "展开后每轮的分支数减少");
            }
        }
    }

    // ---------------- branchless：无分支改写 ----------------
    for (DataOrder o : {DataOrder::Bucket50, DataOrder::Wide256}) {
        Config c;
        c.order = o;
        for (int which = 0; which < 3; ++which) {
            const char* kName = (which == 0) ? "map_ifchain" : (which == 1) ? "map_lookup" : "map_arith";
            Fn fn = (which == 0) ? map_ifchain : (which == 1) ? map_lookup : map_arith;
            b.add(Group::Branchless, kName, std::string("order=") + order_name(o), fn,
                  ref_map_ifchain, c, "mapToBucket：6 分支 vs 查表 vs 除法");
        }
    }
    for (BitPattern p : {BitPattern::Random, BitPattern::Alternate, BitPattern::Period4}) {
        Config c;
        c.pattern = p;
        b.add(Group::Branchless, "predication_branch", std::string("pattern=") + pattern_name(p),
              predication_branch, ref_predication, c, "if-else 调用 computeX/computeY");
        b.add(Group::Branchless, "predication_select", std::string("pattern=") + pattern_name(p),
              predication_select, ref_predication, c, "两条都算再选择（cmov）");
    }

    // ---------------- hint：提示与布局 ----------------
    for (DataOrder o : {DataOrder::Skew90, DataOrder::Skew99}) {
        Config c;
        c.order = o;
        const std::string v = std::string("order=") + order_name(o);
        b.add(Group::Hint, "hint_none", v, hint_none, ref_hint, c, "90%/99% 偏斜，无提示");
        b.add(Group::Hint, "hint_likely", v, hint_likely, ref_hint, c, "__builtin_expect(...,1)");
        b.add(Group::Hint, "hint_unlikely", v, hint_unlikely, ref_hint, c, "__builtin_expect(...,0)");
        b.add(Group::Hint, "hint_unpredictable", v, hint_unpredictable, ref_hint, c,
              "Clang-17+ __builtin_unpredictable → CMOV");
    }
    {
        Config c;
        c.order = DataOrder::Skew90;
        b.add(Group::Hint, "hot_first", "order=skew90", hot_first, ref_hot_first, c,
              "90% 的路径写在前面");
        b.add(Group::Hint, "cold_first", "order=skew90", cold_first, ref_cold_first, c,
              "90% 的路径写在后面（布局相反）");
    }

    // ---------------- call：调用 / 返回 ----------------
    {
        Config c;
        b.add(Group::Call, "call_direct", "sites=1", call_direct, ref_call_direct, c,
              "直接调用：返回栈可预测");
        for (int32_t mode : {0, 1}) {
            Config cc;
            cc.p2 = mode;
            b.add(Group::Call, "call_sites8", std::string("mode=") + kModes[mode], call_sites8,
                  ref_call_sites8, cc, "8 个不同调用点轮转（返回栈压力）");
        }
        for (int32_t depth : {1, 4, 8, 16}) {
            Config cc;
            cc.p1 = depth;
            b.add(Group::Call, "recursion_depth", "depth=" + std::to_string(depth),
                  recursion_depth, ref_recursion_depth, cc, "固定深度递归");
        }
    }

    // ---------------- random：长随机序列（≈50% 误预测） ----------------
    for (uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
        Config c;
        c.n = 1u << 20;
        c.pattern = BitPattern::Random;
        c.seed = seed;
        b.add(Group::Random, "random_mix",
              "seed=" + std::to_string(seed) + with_n(c), random_mix, ref_random_mix, c,
              "1M 元素的随机方向序列");
    }

    // ---------------- align：代码对齐 ----------------
    for (int which = 0; which < 3; ++which) {
        Config c;
        c.order = DataOrder::AllTrue;  // 分支方向可预测，避免误预测噪声盖过对齐效应
        const char* kName = (which == 0) ? "align_16" : (which == 1) ? "align_32" : "align_64";
        Fn fn = (which == 0) ? align_16 : (which == 1) ? align_32 : align_64;
        const std::string v = "align=" + std::to_string(which == 0 ? 16 : (which == 1 ? 32 : 64));
        b.add(Group::Align, kName, v, fn, ref_align, c, "函数入口对齐不同（PERF 书 11.4）");
    }
}

const Builder& registry() {
    static const Builder kReg = [] {
        Builder r;
        build_into(r);
        return r;
    }();
    return kReg;
}

}  // namespace

const std::vector<Case>& cases() { return registry().list; }

const std::vector<const Case*>& cases_of(Group g) {
    static const std::vector<std::vector<const Case*>> kIndex = [] {
        std::vector<std::vector<const Case*>> v(group_count());
        for (const Case& c : cases()) v[(size_t)c.group].push_back(&c);
        return v;
    }();
    const size_t idx = (size_t)g;
    return (idx < kIndex.size()) ? kIndex[idx] : kIndex[0];
}

const Case* find_case(const char* name) {
    for (const Case& c : cases())
        if (std::string(c.name) == name) return &c;
    return nullptr;
}

}  // namespace fz
