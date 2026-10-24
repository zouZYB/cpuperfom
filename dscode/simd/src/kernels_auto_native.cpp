// 自动向量化（-O3 -march=native）：编译器可用的最宽指令集 + FMA。
#include <cstddef>
#include <cstdint>

namespace simd {
namespace auto_native {
#include "kernels_generic.inc"
}  // namespace auto_native
}  // namespace simd
