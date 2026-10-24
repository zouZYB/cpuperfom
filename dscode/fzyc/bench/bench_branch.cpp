// 性能用例：注册表里的每个场景都变成一个 Google Benchmark 用例。
//
// 测量方法上的两个关键点（见 README「测量方法」）：
//   1) 每次调用内核扫描 n 个元素，n 默认 64K：n 太小时"同一段分支序列"会在
//      反复调用中被预测器部分记住，随机分支会被人为测快；
//   2) 每个用例准备 8 组不同 seed 的输入轮转使用，避免跨调用的序列重复；
//      n 很大（>=256K）时序列本身已无法被记住，只需 2 组输入以省内存。
#include <benchmark/benchmark.h>

#include <cstdio>
#include <vector>

#include "branch_lab.h"

namespace {
constexpr int kPoolSmall = 8;           // n <= 128K：8 组不同输入轮转
constexpr size_t kPoolMaxN = 1u << 17;  // 131072
constexpr int kPoolLarge = 2;           // 大数组：2 组即可
constexpr uint32_t kSeedStride = 7919;  // 每组输入的 seed 间隔（质数）
}  // namespace

int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);

    size_t registered = 0;
    for (const fz::Case& c : fz::cases()) {
        auto* bm = benchmark::RegisterBenchmark(
            c.name, [c](benchmark::State& st) {
                const int pool_size = (c.cfg.n <= kPoolMaxN) ? kPoolSmall : kPoolLarge;
                std::vector<fz::Prepared> pool;
                pool.reserve(static_cast<size_t>(pool_size));
                for (int i = 0; i < pool_size; ++i) {
                    fz::Config cfg = c.cfg;
                    cfg.seed = c.cfg.seed + static_cast<uint32_t>(i) * kSeedStride;
                    pool.push_back(fz::prepare(cfg));
                }
                const size_t mask = static_cast<size_t>(pool_size - 1);
                size_t turn = 0;
                for (auto _ : st) {
                    const fz::Input& in = pool[turn & mask].in;
                    ++turn;
                    int64_t r = c.fn(in);
                    benchmark::DoNotOptimize(r);
                }
                st.SetItemsProcessed(static_cast<int64_t>(st.iterations()) *
                                     static_cast<int64_t>(c.cfg.n));
                st.SetLabel("n=" + std::to_string(c.cfg.n) +
                            " pool=" + std::to_string(pool_size));
            });
        bm->Unit(benchmark::kNanosecond);
        ++registered;
    }

    std::printf("注册 %zu 个分支预测场景\n", registered);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
