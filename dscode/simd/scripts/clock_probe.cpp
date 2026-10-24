// 时钟标定：用“延迟已知的依赖链”把 ns 换算成 cycles。
//   add 链：每级延迟 1 cycle
//   imul 链：每级延迟 3 cycles
// 两条链互相印证，偏差 <1% 即认为可靠。
//
// 编译：g++ -std=c++17 -O2 -o build/clock_probe scripts/clock_probe.cpp
// 运行：build/clock_probe.exe
#include <chrono>
#include <cstdint>
#include <cstdio>

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

int main() {
    std::uint64_t x = 1;
    const std::uint64_t n = 400'000'000ULL;  // 8 条 add / 迭代
    double t0 = now_s();
    for (std::uint64_t i = 0; i < n / 8; ++i) {
        asm volatile("add $1, %0" : "+r"(x));
        asm volatile("add $1, %0" : "+r"(x));
        asm volatile("add $1, %0" : "+r"(x));
        asm volatile("add $1, %0" : "+r"(x));
        asm volatile("add $1, %0" : "+r"(x));
        asm volatile("add $1, %0" : "+r"(x));
        asm volatile("add $1, %0" : "+r"(x));
        asm volatile("add $1, %0" : "+r"(x));
    }
    double t1 = now_s();
    double ghz_add = (double)n / (t1 - t0) / 1e9;

    std::uint64_t y = 3;
    const std::uint64_t m = 60'000'000ULL;
    t0 = now_s();
    for (std::uint64_t i = 0; i < m; ++i) asm volatile("imul $3, %0" : "+r"(y));
    t1 = now_s();
    double ghz_mul = (double)m * 3.0 / (t1 - t0) / 1e9;

    std::printf("add-chain  : %.3f GHz  (sink=%llu)\n", ghz_add, (unsigned long long)x);
    std::printf("imul-chain : %.3f GHz  (sink=%llu)\n", ghz_mul, (unsigned long long)y);
    std::printf("=> 采用 ~%.2f GHz（两法偏差 %.1f%%）\n", (ghz_add + ghz_mul) / 2,
                100.0 * (ghz_add - ghz_mul) / ghz_add);
    return 0;
}
