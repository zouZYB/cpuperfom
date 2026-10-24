// 数据 / 分支模式生成器：所有随机都走固定 seed 的 xorshift，保证跨编译器可复现。
#include "branch_lab.h"

#include <algorithm>

namespace fz {
namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(0x9e3779b97f4a7c15ull ^ (uint64_t)seed) {
        if (s == 0) s = 0x9e3779b97f4a7c15ull;
        for (int i = 0; i < 4; ++i) next();
    }
    uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    uint32_t below(uint32_t m) {
        if (m == 0) return 0;
        return (uint32_t)(next() >> 33) % m;
    }
};

inline int32_t block_value(size_t i, size_t k) {
    return (int32_t)(((i / k) & 1u) ? 0 : 255);
}

// 周期模式：先随机生成 period 位"基序列"，再整段重复。
// 用"随机位基序列"而不是"前半 1 后半 0"是为了真正考察预测器能否记住
// 长周期序列：块状的半周期模式在任何周期下都可由很短的 history 预测。
void fill_periodic_random(size_t n, size_t period, uint32_t seed, std::vector<uint8_t>& out) {
    Rng rng(seed ^ 0x9d2c5680u);
    std::vector<uint8_t> base(period);
    for (size_t i = 0; i < period; ++i) base[i] = (uint8_t)rng.below(2);
    out.assign(n, 0);
    for (size_t i = 0; i < n; ++i) out[i] = base[i % period];
}

}  // namespace

void make_data(DataOrder order, size_t n, uint32_t seed, std::vector<int32_t>& out) {
    out.assign(n, 0);
    if (n == 0) return;
    Rng rng(seed ^ 0x51ed270bu);
    switch (order) {
        case DataOrder::Random:
            for (size_t i = 0; i < n; ++i) out[i] = (int32_t)rng.below(512) - 256;
            break;
        case DataOrder::SortedAsc:
        case DataOrder::SortedDesc: {
            for (size_t i = 0; i < n; ++i) out[i] = (int32_t)rng.below(512) - 256;
            std::sort(out.begin(), out.end());
            if (order == DataOrder::SortedDesc) std::reverse(out.begin(), out.end());
            break;
        }
        case DataOrder::AllTrue:
            for (size_t i = 0; i < n; ++i) out[i] = 128 + (int32_t)rng.below(128);
            break;
        case DataOrder::AllFalse:
            for (size_t i = 0; i < n; ++i) out[i] = (int32_t)rng.below(128);
            break;
        case DataOrder::AllPositive:
            for (size_t i = 0; i < n; ++i) out[i] = 1 + (int32_t)rng.below(255);
            break;
        case DataOrder::AllNegative:
            for (size_t i = 0; i < n; ++i) out[i] = -255 + (int32_t)rng.below(255);
            break;
        case DataOrder::Alternating:
            for (size_t i = 0; i < n; ++i) out[i] = (i & 1u) ? 0 : 255;
            break;
        case DataOrder::AlternatingSign:
            for (size_t i = 0; i < n; ++i) out[i] = (i & 1u) ? -255 : 255;
            break;
        case DataOrder::Block4:
            for (size_t i = 0; i < n; ++i) out[i] = block_value(i, 4);
            break;
        case DataOrder::Block8:
            for (size_t i = 0; i < n; ++i) out[i] = block_value(i, 8);
            break;
        case DataOrder::Block16:
            for (size_t i = 0; i < n; ++i) out[i] = block_value(i, 16);
            break;
        case DataOrder::Block32:
            for (size_t i = 0; i < n; ++i) out[i] = block_value(i, 32);
            break;
        case DataOrder::Block64:
            for (size_t i = 0; i < n; ++i) out[i] = block_value(i, 64);
            break;
        case DataOrder::Block256:
            for (size_t i = 0; i < n; ++i) out[i] = block_value(i, 256);
            break;
        case DataOrder::Skew90:
            for (size_t i = 0; i < n; ++i) out[i] = (rng.below(10) == 0) ? 0 : 255;
            break;
        case DataOrder::Skew99:
            for (size_t i = 0; i < n; ++i) out[i] = (rng.below(100) == 0) ? 0 : 255;
            break;
        case DataOrder::Bucket50:
            for (size_t i = 0; i < n; ++i) out[i] = (int32_t)rng.below(50);
            break;
        case DataOrder::Wide256:
            for (size_t i = 0; i < n; ++i) out[i] = (int32_t)rng.below(256);
            break;
        default:
            for (size_t i = 0; i < n; ++i) out[i] = 0;
            break;
    }
}

void make_bits(BitPattern pattern, size_t n, uint32_t seed, std::vector<uint8_t>& out) {
    out.assign(n, 0);
    if (n == 0) return;
    Rng rng(seed ^ 0x2545f491u);
    switch (pattern) {
        case BitPattern::AllTrue:
            for (size_t i = 0; i < n; ++i) out[i] = 1;
            break;
        case BitPattern::AllFalse:
            for (size_t i = 0; i < n; ++i) out[i] = 0;
            break;
        case BitPattern::Alternate:
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)(i & 1u);
            break;
        case BitPattern::Period3: fill_periodic_random(n, 3, seed, out); break;
        case BitPattern::Period4: fill_periodic_random(n, 4, seed, out); break;
        case BitPattern::Period5: fill_periodic_random(n, 5, seed, out); break;
        case BitPattern::Period7: fill_periodic_random(n, 7, seed, out); break;
        case BitPattern::Period8: fill_periodic_random(n, 8, seed, out); break;
        case BitPattern::Period16: fill_periodic_random(n, 16, seed, out); break;
        case BitPattern::Period32: fill_periodic_random(n, 32, seed, out); break;
        case BitPattern::Period64: fill_periodic_random(n, 64, seed, out); break;
        case BitPattern::Period128: fill_periodic_random(n, 128, seed, out); break;
        case BitPattern::Period256: fill_periodic_random(n, 256, seed, out); break;
        case BitPattern::Period512: fill_periodic_random(n, 512, seed, out); break;
        case BitPattern::Period1024: fill_periodic_random(n, 1024, seed, out); break;
        case BitPattern::Period2048: fill_periodic_random(n, 2048, seed, out); break;
        case BitPattern::Period4096: fill_periodic_random(n, 4096, seed, out); break;
        case BitPattern::Period8192: fill_periodic_random(n, 8192, seed, out); break;
        case BitPattern::Period16384: fill_periodic_random(n, 16384, seed, out); break;
        case BitPattern::Period32768: fill_periodic_random(n, 32768, seed, out); break;
        case BitPattern::Random:
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)rng.below(2);
            break;
        case BitPattern::Run3:
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)(((i / 3) & 1u) != 0);
            break;
        case BitPattern::Run8:
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)(((i / 8) & 1u) != 0);
            break;
        case BitPattern::Run32:
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)(((i / 32) & 1u) != 0);
            break;
        case BitPattern::PhaseSwitch: {
            const size_t half = n / 2;
            for (size_t i = 0; i < n; ++i) out[i] = (i < half) ? 1 : (uint8_t)rng.below(2);
            break;
        }
        default:
            break;
    }
}

void make_corr_bits(Corr corr, const std::vector<uint8_t>& b1, size_t n, uint32_t seed,
                    std::vector<uint8_t>& out) {
    out.assign(n, 0);
    if (n == 0) return;
    Rng rng(seed ^ 0x6a09e667u);
    switch (corr) {
        case Corr::Same:
            for (size_t i = 0; i < n; ++i) out[i] = b1[i] ? 1 : 0;
            break;
        case Corr::Not:
            for (size_t i = 0; i < n; ++i) out[i] = b1[i] ? 0 : 1;
            break;
        case Corr::Independent:
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)rng.below(2);
            break;
        case Corr::XorPrev:
            for (size_t i = 0; i < n; ++i) {
                const uint8_t prev = (i == 0) ? 0 : (b1[i - 1] ? 1 : 0);
                out[i] = (uint8_t)((b1[i] ? 1 : 0) ^ prev);
            }
            break;
        default:
            break;
    }
}

void make_idx(size_t n, uint32_t seed, int32_t ways, int32_t mode, std::vector<uint8_t>& out) {
    out.assign(n, 0);
    if (n == 0) return;
    if (ways < 1) ways = 1;
    if (ways > 256) ways = 256;
    Rng rng(seed ^ 0xbb67ae85u);
    switch (mode) {
        case 0:  // 顺序
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)(i % (size_t)ways);
            break;
        case 1:  // 随机
            for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)(rng.below((uint32_t)ways));
            break;
        default:  // 固定同一目标
            for (size_t i = 0; i < n; ++i) out[i] = 0;
            break;
    }
}

void make_trips(size_t n, uint32_t seed, int32_t trip, int32_t mode, std::vector<int32_t>& out) {
    out.assign(n, 0);
    if (n == 0) return;
    if (trip < 0) trip = 0;
    Rng rng(seed ^ 0x3c6ef372u);
    switch (mode) {
        case 0:  // 恒定
            for (size_t i = 0; i < n; ++i) out[i] = trip;
            break;
        case 1:  // 交替 trip / trip+8
            for (size_t i = 0; i < n; ++i) out[i] = (i & 1u) ? (trip + 8) : trip;
            break;
        default:  // 1..trip 随机
            for (size_t i = 0; i < n; ++i)
                out[i] = (trip <= 1) ? trip : (int32_t)(1 + rng.below((uint32_t)trip));
            break;
    }
}

size_t count_ones(const std::vector<uint8_t>& bits) {
    size_t c = 0;
    for (uint8_t x : bits) c += (x ? 1u : 0u);
    return c;
}

size_t longest_run(const std::vector<uint8_t>& bits) {
    size_t best = 0, cur = 0;
    for (uint8_t x : bits) {
        if (x) {
            ++cur;
            if (cur > best) best = cur;
        } else {
            cur = 0;
        }
    }
    return best;
}

bool is_periodic(const std::vector<uint8_t>& bits, size_t period) {
    if (period == 0 || period >= bits.size()) return false;
    for (size_t i = period; i < bits.size(); ++i)
        if (bits[i] != bits[i - period]) return false;
    return true;
}

size_t detect_period(const std::vector<uint8_t>& bits, size_t max_period) {
    for (size_t p = 1; p <= max_period && p < bits.size(); ++p)
        if (is_periodic(bits, p)) return p;
    return 0;
}

}  // namespace fz
