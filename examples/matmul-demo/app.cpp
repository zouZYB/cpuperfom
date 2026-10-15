// 程序级基准：整命令计时。用法: app <naive|tiled> [N]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "matmul.h"

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "naive";
    int N = argc > 2 ? atoi(argv[2]) : 512;
    auto A = make_matrix(N, 0.1);
    auto B = make_matrix(N, 0.7);
    std::vector<double> C(N * N);

    if (strcmp(mode, "tiled") == 0)
        matmul_tiled(A, B, C, N);
    else
        matmul_naive(A, B, C, N);

    // 防止整个计算被优化掉
    double sum = 0;
    for (double v : C) sum += v;
    printf("checksum=%f\n", sum);
    return 0;
}
