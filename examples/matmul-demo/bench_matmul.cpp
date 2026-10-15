// 函数级微基准：输出 Google Benchmark JSON 供 bench-runner 解析。
#include <benchmark/benchmark.h>
#include "matmul.h"

static void BM_MatmulNaive(benchmark::State& state) {
    const int N = state.range(0);
    auto A = make_matrix(N, 0.1);
    auto B = make_matrix(N, 0.7);
    std::vector<double> C(N * N);
    for (auto _ : state) {
        matmul_naive(A, B, C, N);
        benchmark::DoNotOptimize(C.data());
    }
}
BENCHMARK(BM_MatmulNaive)->Arg(512);

BENCHMARK_MAIN();
