// 程序级基准：整命令计时。用法: app [N]
#include <cstdio>
#include <cstdlib>
#include "matmul.h"

int main(int argc, char** argv) {
    int N = argc > 1 ? atoi(argv[1]) : 512;
    auto A = make_matrix(N, 0.1);
    auto B = make_matrix(N, 0.7);
    std::vector<double> C(N * N);

    matmul_naive(A, B, C, N);

    // 防止整个计算被优化掉
    double sum = 0;
    for (double v : C) sum += v;
    printf("checksum=%f\n", sum);
    return 0;
}
