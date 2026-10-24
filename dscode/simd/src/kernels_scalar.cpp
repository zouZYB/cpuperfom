// 标量基线：编译时关闭 GCC 的循环/SLP 向量化，保证真的是“一条指令一个元素”。
#include <cstddef>
#include <cstdint>

namespace simd {
namespace scalar {
#include "kernels_generic.inc"
}  // namespace scalar
}  // namespace simd
