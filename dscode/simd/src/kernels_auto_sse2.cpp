// 自动向量化（x86-64 默认目标）：-O3 会产生 SSE2 向量代码，但由于没有 -march，
// 只能用 128 位、且没有 FMA——常被误认为“编译器已经优化好了”。
#include <cstddef>
#include <cstdint>

namespace simd {
namespace auto_sse2 {
#include "kernels_generic.inc"
}  // namespace auto_sse2
}  // namespace simd
