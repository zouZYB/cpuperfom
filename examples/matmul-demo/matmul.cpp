// matmul-demo：演示缓存优化前后的性能差异。
// naive：按行×列直接访问，内层循环跨行跳跃，cache miss 严重。
// tiled：分块计算，子块驻留 L1/L2，cache 友好。
#include "matmul.h"

void matmul_naive(const std::vector<double>& A, const std::vector<double>& B,
                  std::vector<double>& C, int N) {
    std::fill(C.begin(), C.end(), 0.0);
    // i-j-k 循环序：内层按列访问 B（步长 N），每次迭代都跨 cache line，miss 严重
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < N; ++k)
                C[i * N + j] += A[i * N + k] * B[k * N + j];
}

void matmul_tiled(const std::vector<double>& A, const std::vector<double>& B,
                  std::vector<double>& C, int N) {
    constexpr int T = 32; // 32*32*8B = 8KB，驻留 L1
    std::fill(C.begin(), C.end(), 0.0);
    for (int ii = 0; ii < N; ii += T)
        for (int kk = 0; kk < N; kk += T)
            for (int jj = 0; jj < N; jj += T)
                for (int i = ii; i < std::min(ii + T, N); ++i)
                    for (int k = kk; k < std::min(kk + T, N); ++k) {
                        double a = A[i * N + k];
                        for (int j = jj; j < std::min(jj + T, N); ++j)
                            C[i * N + j] += a * B[k * N + j];
                    }
}

std::vector<double> make_matrix(int N, double seed) {
    std::vector<double> m(N * N);
    for (int i = 0; i < N * N; ++i)
        m[i] = std::sin(seed * i) * 0.5 + 1.0; // 确定性伪随机
    return m;
}
