#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

void matmul_naive(const std::vector<double>& A, const std::vector<double>& B,
                  std::vector<double>& C, int N);
void matmul_tiled(const std::vector<double>& A, const std::vector<double>& B,
                  std::vector<double>& C, int N);
std::vector<double> make_matrix(int N, double seed);
