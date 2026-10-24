cmake_minimum_required(VERSION 3.20)
project(fzyc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
endif()

# 支持的工具链：
#   1) GCC / Clang（GNU 驱动），例如 MinGW-GCC；
#   2) clang-cl（MSVC 驱动），用于 VS2022：
#      cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64 -T ClangCL
set(FZYC_CLANG_CL OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(FZYC_CLANG_CL ON)
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "本实验需要 GCC / Clang（含 VS2022 的 ClangCL 工具集）；\n"
        "纯 MSVC 工具集(v143)不支持：可用 VS2022 + ClangCL（-G \"Visual Studio 17 2022\" -A x64 -T ClangCL）")
endif()

# 本实验只关心"控制流"，所有内核统一关掉自动向量化：
#   1) 分支版内核靠编译期屏障已经不会被向量化；
#   2) CMOV/掩码版如果被向量化，就会把"无分支"变成"SIMD"，对比失去意义。
if(FZYC_CLANG_CL)
    set(FZYC_NO_VEC /clang:-fno-vectorize /clang:-fno-slp-vectorize)
    set(FZYC_WARN /clang:-Wall /clang:-Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(FZYC_NO_VEC -fno-vectorize -fno-slp-vectorize)
    set(FZYC_WARN -Wall -Wextra -Wno-ignored-attributes)
else()  # GCC
    set(FZYC_NO_VEC -fno-tree-vectorize -fno-tree-slp-vectorize)
    set(FZYC_WARN -Wall -Wextra -Wno-ignored-attributes -Wno-psabi)
endif()

option(FZYC_BUILD_BENCH "构建 Google Benchmark 性能用例" ON)

set(FZYC_SOURCES
    src/pattern_gen.cpp
    src/prepare.cpp
    src/kernels_data.cpp
    src/kernels_pattern.cpp
    src/kernels_indirect.cpp
    src/kernels_loop.cpp
    src/kernels_branchless.cpp
    src/kernels_meta.cpp
    src/registry.cpp
)

add_library(branch_lab STATIC ${FZYC_SOURCES})
target_include_directories(branch_lab PUBLIC include)
target_compile_options(branch_lab PRIVATE ${FZYC_WARN} ${FZYC_NO_VEC})

# ---------------- 正确性测试 ----------------

add_executable(test_branch src/test_main.cpp)
target_link_libraries(test_branch PRIVATE branch_lab)
target_compile_options(test_branch PRIVATE ${FZYC_WARN} ${FZYC_NO_VEC})

enable_testing()
add_test(NAME branch_correctness COMMAND test_branch)

# ---------------- 性能用例 ----------------

if(FZYC_BUILD_BENCH)
    include(FetchContent)

    # 优先复用 examples/matmul-demo 已经拉取好的 Google Benchmark 源码，离线也能构建
    if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_BENCHMARK)
        set(_fzyc_local_bench "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/matmul-demo/build/_deps/benchmark-src")
        if(EXISTS "${_fzyc_local_bench}/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_BENCHMARK "${_fzyc_local_bench}" CACHE PATH
                "本地 Google Benchmark 源码（离线构建）" FORCE)
        endif()
    endif()

    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.4
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    add_executable(bench_branch bench/bench_branch.cpp)
    target_link_libraries(bench_branch PRIVATE branch_lab benchmark::benchmark)
    target_compile_options(bench_branch PRIVATE ${FZYC_WARN} ${FZYC_NO_VEC})
endif()
