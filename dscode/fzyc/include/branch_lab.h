// 分支预测实验场（fzyc = fen zhi yu ce）公共接口。
//
// 设计目标：用"同一份语义、可控的可预测性"的数据和代码形态，测量
//   1) 条件分支方向预测（局部历史 / 全局历史 / 循环预测器 / 偏斜）
//   2) 间接分支目标预测（BTB：switch / 函数指针 / 虚函数 / 返回栈）
//   3) 无分支改写（cmov、查找表、算术、谓词）与分支的收益对比
//
// 所有内核都是"整数 → 整数 checksum"，便于用独立参考实现做逐位比对。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// 编译器适配
// ---------------------------------------------------------------------------

// 编译期屏障：不产生任何指令，但阻止编译器把 if/else 折叠成 cmov/select，
// 从而保证"分支版"内核里真的有分支。这是本实验有效性的关键。
//
// 为什么要给 asm 传一个"唯一立即数"：LLVM 会把两个分支里完全相同的空 asm
// 提到分支外，从而仍然 if-convert；每条屏障带一个不同的立即数（__COUNTER__）
// 之后两条 asm 不再等价，分支就被保住了。GCC 与 clang/clang-cl 都验证过。
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define FZ_BARRIER() _ReadWriteBarrier()
#else
#define FZ_BARRIER() __asm__ __volatile__("" ::"i"(__COUNTER__) : "memory")
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define FZ_NOINLINE __declspec(noinline)
#define FZ_LIKELY(x) (x)
#define FZ_UNLIKELY(x) (x)
#define FZ_UNPREDICTABLE(x) (x)
#define FZ_FN_ALIGN(n)
#else
#define FZ_NOINLINE __attribute__((noinline))
#define FZ_LIKELY(x) __builtin_expect(!!(x), 1)
#define FZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FZ_FN_ALIGN(n) __attribute__((aligned(n)))
#if defined(__clang__) && __clang_major__ >= 17
#define FZ_UNPREDICTABLE(x) __builtin_unpredictable((x))
#else
#define FZ_UNPREDICTABLE(x) (x)
#endif
#endif

namespace fz {

// ---------------------------------------------------------------------------
// 场景描述
// ---------------------------------------------------------------------------

enum class Group {
    Data,        // 数据相关条件分支（排序 vs 随机）
    Pattern,     // 周期/随机模式 → 局部历史预测器
    Correlated,  // 相关分支（全局历史 / TAGE 领域）
    Density,     // 分支密度（每元素 N 个分支）
    Indirect,    // 间接分支 / 目标地址预测
    Loop,        // 循环边界与循环预测器
    Branchless,  // 无分支改写（cmov / 查表 / 算术 / 谓词）
    Hint,        // likely/unlikely/unpredictable 提示
    Call,        // 调用/返回（返回地址栈）
    Store,       // 条件存储
    Random,      // 长随机序列（≈50% 误预测实验）
    Align,       // 函数/代码对齐（基础块对齐，参照 PERF 书 11.4）
    COUNT
};

// 数据分布（值域按内核语义解释；阈值类的默认阈值 a=128）
enum class DataOrder {
    Random,          // [-256, 255] 均匀
    SortedAsc,       // 升序 → 阈值分支"前不取后取"
    SortedDesc,      // 降序
    AllTrue,         // [128, 255]：阈值分支恒取
    AllFalse,        // [0, 127]：恒不取
    AllPositive,     // [1, 255]
    AllNegative,     // [-255, -1]
    Alternating,     // i 偶 → 255，i 奇 → 0
    AlternatingSign, // i 偶 → +255，i 奇 → -255
    Block4,          // 每 4 个翻转 true/false
    Block8,
    Block16,
    Block32,
    Block64,
    Block256,
    Skew90,          // 90% 的值为 255，10% 为 0
    Skew99,          // 99% 的值为 255，1% 为 0
    Bucket50,        // [0, 49] 均匀：mapToBucket 的热路径
    Wide256,         // [0, 255] 均匀：含越界
    COUNT
};

// 分支方向模式（b1 数组）
enum class BitPattern {
    AllTrue,     // 恒取
    AllFalse,    // 恒不取
    Alternate,   // 交替（周期 2，可被学习）
    Period3,
    Period4,
    Period5,
    Period7,
    Period8,
    Period16,
    Period32,
    Period64,
    Period128,
    Period256,
    Period512,
    Period1024,
    Period2048,
    Period4096,
    Period8192,
    Period16384,
    Period32768,
    Random,      // 均匀随机（≈50% 误预测）
    Run3,        // 3 个取 / 3 个不取
    Run8,
    Run32,
    PhaseSwitch, // 前半恒取、后半随机（预测器状态切换）
    COUNT
};

// 第二个分支与第一个分支的相关性
enum class Corr {
    Same,        // b2 == b1（完全相关：TAGE 能学）
    Not,         // b2 == !b1（反相关：同样能学）
    Independent, // b2 独立随机（不可学）
    XorPrev,     // b2[i] = b1[i] ^ b1[i-1]（跨迭代相关）
    COUNT
};

// 默认元素数。之所以取 64K 而不是 4K：微基准会反复调用同一个内核，n 太小时
// "同一段分支结果序列"会被预测器随调用重复而部分记住（实测 n=4K 时随机分支
// 只跑出 ~0.8ns/元素，而 n>=64K 时是 ~2.5ns/元素），64K 以上才有稳定的
// "50% 误预测"表现；bench 另外用 8 组不同 seed 的输入轮转，进一步消除重复效应。
constexpr size_t kDefaultN = 65536;

struct Config {
    DataOrder order = DataOrder::Random;
    BitPattern pattern = BitPattern::AllTrue;
    Corr corr = Corr::Independent;
    uint32_t seed = 42;
    int32_t a = 128;   // 主参数：阈值 / 下界 / 初值（语义由内核注释给出）
    int32_t b = 0;     // 次参数：上界 / 目标值
    int32_t p1 = 1;    // 结构参数：路数 / 密度 / 深度 / 展开 / 跳转规模
    int32_t p2 = 0;    // 结构参数 2：索引模式（0 顺序 / 1 随机 / 2 固定）
    size_t n = kDefaultN;
};

// 内核运行时输入（所有数组由 prepare() 一次性生成）
struct Input {
    const int32_t* v = nullptr;      // 主数据
    const uint8_t* b1 = nullptr;     // 分支模式 1
    const uint8_t* b2 = nullptr;     // 分支模式 2（与 b1 相关）
    const uint8_t* idx = nullptr;    // 跳转目标索引 0..p1-1
    const int32_t* trips = nullptr;  // 循环次数
    int32_t* dst = nullptr;          // 输出缓冲（条件存储类内核）
    size_t n = 0;
    int32_t a = 0;
    int32_t b = 0;
    int32_t p1 = 0;
    int32_t p2 = 0;
};

using Fn = int64_t (*)(const Input&);

struct Case {
    const char* name;  // 全局唯一：<组>/<内核>/<变体>
    Group group;
    const char* variant;
    Fn fn;   // 被测实现
    Fn ref;  // 独立参考实现（只用于正确性比对）
    Config cfg;
    const char* note;
};

// 注册表
const std::vector<Case>& cases();
const std::vector<const Case*>& cases_of(Group g);
const Case* find_case(const char* name);
const char* group_name(Group g);
const char* order_name(DataOrder o);
const char* pattern_name(BitPattern p);
const char* corr_name(Corr c);
constexpr size_t group_count() { return static_cast<size_t>(Group::COUNT); }

// 由 Config 生成全部输入数组
struct Prepared {
    std::vector<int32_t> v;
    std::vector<uint8_t> b1;
    std::vector<uint8_t> b2;
    std::vector<uint8_t> idx;
    std::vector<int32_t> trips;
    std::vector<int32_t> dst;
    Input in;
};
Prepared prepare(const Config& cfg);

// 生成器（也导出给测试单独检查）
void make_data(DataOrder order, size_t n, uint32_t seed, std::vector<int32_t>& out);
void make_bits(BitPattern pattern, size_t n, uint32_t seed, std::vector<uint8_t>& out);
void make_corr_bits(Corr corr, const std::vector<uint8_t>& b1, size_t n, uint32_t seed,
                    std::vector<uint8_t>& out);
void make_idx(size_t n, uint32_t seed, int32_t ways, int32_t mode, std::vector<uint8_t>& out);
void make_trips(size_t n, uint32_t seed, int32_t trip, int32_t mode, std::vector<int32_t>& out);

// 模式统计工具（测试用）
size_t count_ones(const std::vector<uint8_t>& bits);
size_t longest_run(const std::vector<uint8_t>& bits);
bool is_periodic(const std::vector<uint8_t>& bits, size_t period);
size_t detect_period(const std::vector<uint8_t>& bits, size_t max_period);

// ---------------------------------------------------------------------------
// 内核声明（实现散落在 src/kernels_*.cpp）
// 约定：输入只读，返回 int64 checksum；n==0 时必须安全返回 0。
// ---------------------------------------------------------------------------

// --- 数据相关条件分支 -------------------------------------------------------
// sum_if_*  : v[i] >= a 时 sum += v[i]，否则 sum -= v[i]（a 默认 128）
int64_t sum_if_branch(const Input& in);
int64_t sum_if_cmov(const Input& in);
// count_if_*: 统计 v[i] >= a 的个数
int64_t count_if_branch(const Input& in);
int64_t count_if_cmov(const Input& in);
// max_if_*  : 求最大值，初值 a
int64_t max_if_branch(const Input& in);
int64_t max_cmov(const Input& in);
// abs_if_*  : 绝对值之和
int64_t abs_if_branch(const Input& in);
int64_t abs_cmov(const Input& in);
// clamp_if_*: 截断到 [a, b]
int64_t clamp_if_branch(const Input& in);
int64_t clamp_cmov(const Input& in);

// --- 条件存储（Store 组）：dst[i] = v[i] >= a ? v[i] : b --------------------
int64_t cond_store_branch(const Input& in);
int64_t cond_store_cmov(const Input& in);

// --- 周期模式（Pattern 组）-------------------------------------------------
// pattern_sum: b1[i] 决定 sum += v[i] 还是 sum -= v[i]
int64_t pattern_sum(const Input& in);
// pattern_two: 每个元素两个独立分支（b1[i]、b2[i] 各加一个累加器）
int64_t pattern_two(const Input& in);
// history_chain: 仅当连续 p1 个 b1 全为 1 时才把 v[i] 累加（依赖全局历史）
int64_t history_chain(const Input& in);

// --- 相关分支（Correlated 组）---------------------------------------------
// nested_corr: if (b1) { if (b2) ... }，b2 与 b1 按 Corr 相关
int64_t nested_corr(const Input& in);
// xor_prev: 分支条件为 b2[i]（默认 b1[i] ^ b1[i-1]）
int64_t xor_prev(const Input& in);

// --- 分支密度（Density 组）------------------------------------------------
// density: 每个元素执行 p1 个互不相关位置的条件分支
int64_t density(const Input& in);

// --- 间接分支（Indirect 组）-----------------------------------------------
// switch_ways: 按 idx[i] 做 switch（p1 决定路数，参数化实现覆盖 2/4/8/16/64）
int64_t switch_ways(const Input& in);
// ifchain_ways: 同一映射的 if-else 链（对照：分支 vs 跳转表）
int64_t ifchain_ways(const Input& in);
// callptr: 通过函数指针表调用（p1 个目标）
int64_t callptr(const Input& in);
// virtual_call: 通过基类指针调用（p1 个派生类）
int64_t virtual_call(const Input& in);

// --- 循环（Loop 组）-------------------------------------------------------
// inner_trip: 每个元素一个 trips[i] 次的内层循环
int64_t inner_trip(const Input& in);
// nested_loop: p1 外层 × p2 内层嵌套循环
int64_t nested_loop(const Input& in);
// unroll: 每轮处理 p1 个元素（减少循环分支次数）
int64_t unroll(const Input& in);

// --- 无分支改写（Branchless 组）-------------------------------------------
// map_*: 把 [0,255] 的值映射到 5 个桶（<10,<20,<30,<40,<50，其余 -1）
int64_t map_ifchain(const Input& in);
int64_t map_lookup(const Input& in);
int64_t map_arith(const Input& in);
// predication_*: if (cond) sum += fx(x) else sum += fy(x)；select 版两条都算
int64_t predication_branch(const Input& in);
int64_t predication_select(const Input& in);

// --- 提示（Hint 组）-------------------------------------------------------
int64_t hint_none(const Input& in);
int64_t hint_likely(const Input& in);
int64_t hint_unlikely(const Input& in);
int64_t hint_unpredictable(const Input& in);
int64_t hot_first(const Input& in);
int64_t cold_first(const Input& in);

// --- 调用/返回（Call 组）--------------------------------------------------
int64_t call_direct(const Input& in);
int64_t call_sites8(const Input& in);
int64_t recursion_depth(const Input& in);

// --- 随机长序列（Random 组）-----------------------------------------------
int64_t random_mix(const Input& in);

// --- 对齐（Align 组）：同一内层循环，函数入口对齐不同 ----------------------
int64_t align_16(const Input& in);
int64_t align_32(const Input& in);
int64_t align_64(const Input& in);

// ---------------------------------------------------------------------------
// 参考实现（正确性比对用；语义与对应的被测内核一致，但不含屏障/无分支技巧）
// ---------------------------------------------------------------------------
int64_t ref_sum_if(const Input& in);
int64_t ref_count_if(const Input& in);
int64_t ref_max(const Input& in);
int64_t ref_abs(const Input& in);
int64_t ref_clamp(const Input& in);
int64_t ref_cond_store(const Input& in);
int64_t ref_pattern_sum(const Input& in);
int64_t ref_pattern_two(const Input& in);
int64_t ref_history_chain(const Input& in);
int64_t ref_nested_corr(const Input& in);
int64_t ref_xor_prev(const Input& in);
int64_t ref_density(const Input& in);
int64_t ref_switch_ways(const Input& in);
int64_t ref_ifchain_ways(const Input& in);
int64_t ref_callptr(const Input& in);
int64_t ref_virtual_call(const Input& in);
int64_t ref_inner_trip(const Input& in);
int64_t ref_nested_loop(const Input& in);
int64_t ref_unroll(const Input& in);
int64_t ref_map_ifchain(const Input& in);
int64_t ref_predication(const Input& in);
int64_t ref_hint(const Input& in);
int64_t ref_hot_first(const Input& in);
int64_t ref_cold_first(const Input& in);
int64_t ref_call_direct(const Input& in);
int64_t ref_call_sites8(const Input& in);
int64_t ref_recursion_depth(const Input& in);
int64_t ref_random_mix(const Input& in);
int64_t ref_align(const Input& in);

}  // namespace fz
