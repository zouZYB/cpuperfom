cmake_minimum_required(VERSION 3.20)
project(simd_lab CXX)

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
set(SIMD_LAB_CLANG_CL OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(SIMD_LAB_CLANG_CL ON)
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "本实验依赖 GCC/Clang 的 -mavx2/-mavx512f 与内建 CPUID，不支持纯 MSVC 工具集(v143)。\n"
        "可选：MinGW-GCC、Clang，或 VS2022 + ClangCL（-G \"Visual Studio 17 2022\" -A x64 -T ClangCL）")
endif()

# clang-cl 的 __builtin_cpu_supports 在链接期依赖 compiler-rt 的 __cpu_model，
# MSBuild 默认不带这个库，这里从编译器自己的 resource dir 里找出来显式链接。
if(SIMD_LAB_CLANG_CL)
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE SIMD_LAB_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    set(SIMD_LAB_CLANG_RT
        "${SIMD_LAB_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.builtins-x86_64.lib")
    if(EXISTS "${SIMD_LAB_CLANG_RT}")
        add_library(simd_lab_clang_rt INTERFACE)
        target_link_libraries(simd_lab_clang_rt INTERFACE "${SIMD_LAB_CLANG_RT}")
        set(SIMD_LAB_EXTRA_LIBS simd_lab_clang_rt)
    else()
        message(WARNING "未找到 clang_rt.builtins-x86_64.lib，链接可能因 __cpu_model 未定义而失败")
        set(SIMD_LAB_EXTRA_LIBS "")
    endif()
else()
    set(SIMD_LAB_EXTRA_LIBS "")
endif()

option(SIMD_LAB_BUILD_BENCH "构建 Google Benchmark 性能用例" ON)

# MinGW：静态链接运行时，避免与系统里其他 libstdc++ DLL 的 ABI 冲突
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

set(SIMD_LAB_SOURCES
    src/dispatch.cpp
    src/kernels_scalar.cpp
    src/kernels_auto_sse2.cpp
    src/kernels_auto_native.cpp
)

set(SIMD_LAB_X86 OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(SIMD_LAB_X86 ON)
    list(APPEND SIMD_LAB_SOURCES
        src/kernels_sse41.cpp
        src/kernels_avx2.cpp
        src/kernels_avx512.cpp
    )
endif()

add_library(simd_kernels STATIC ${SIMD_LAB_SOURCES})
target_include_directories(simd_kernels PUBLIC include)
# 每个 TU 用自己的指令集开关编译：同一份模板，三种位宽。
# GCC / Clang（GNU 驱动）用 -m* 开关；clang-cl（MSVC 驱动）用 /arch: 或 /clang:-m* 直通。
if(SIMD_LAB_CLANG_CL)
    set(SIMD_LAB_WARN_FLAGS /clang:-Wall /clang:-Wextra)
    set(SIMD_LAB_FLAGS_SCALAR /clang:-fno-vectorize /clang:-fno-slp-vectorize)
    set(SIMD_LAB_FLAGS_NATIVE /clang:-march=native)
    set(SIMD_LAB_FLAGS_SSE41 /clang:-msse4.1)
    set(SIMD_LAB_FLAGS_AVX2 /arch:AVX2)
    set(SIMD_LAB_FLAGS_AVX512 /arch:AVX512)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SIMD_LAB_WARN_FLAGS -Wall -Wextra -Wno-ignored-attributes)
    set(SIMD_LAB_FLAGS_SCALAR -fno-vectorize -fno-slp-vectorize)
    set(SIMD_LAB_FLAGS_NATIVE -march=native)
    set(SIMD_LAB_FLAGS_SSE41 -msse4.1)
    set(SIMD_LAB_FLAGS_AVX2 -mavx2 -mfma)
    set(SIMD_LAB_FLAGS_AVX512 -mavx512f -mavx512bw -mavx512vl -mfma)
else()  # GCC
    set(SIMD_LAB_WARN_FLAGS -Wall -Wextra -Wno-ignored-attributes -Wno-psabi)
    set(SIMD_LAB_FLAGS_SCALAR -fno-tree-vectorize -fno-tree-slp-vectorize)
    set(SIMD_LAB_FLAGS_NATIVE -march=native)
    set(SIMD_LAB_FLAGS_SSE41 -msse4.1)
    set(SIMD_LAB_FLAGS_AVX2 -mavx2 -mfma)
    set(SIMD_LAB_FLAGS_AVX512 -mavx512f -mavx512bw -mavx512vl -mfma)
endif()

target_compile_options(simd_kernels PRIVATE ${SIMD_LAB_WARN_FLAGS})

set_source_files_properties(src/kernels_scalar.cpp PROPERTIES
    COMPILE_OPTIONS "${SIMD_LAB_FLAGS_SCALAR}")
set_source_files_properties(src/kernels_auto_native.cpp PROPERTIES
    COMPILE_OPTIONS "${SIMD_LAB_FLAGS_NATIVE}")
if(SIMD_LAB_X86)
    set_source_files_properties(src/kernels_sse41.cpp PROPERTIES
        COMPILE_OPTIONS "${SIMD_LAB_FLAGS_SSE41}")
    set_source_files_properties(src/kernels_avx2.cpp PROPERTIES
        COMPILE_OPTIONS "${SIMD_LAB_FLAGS_AVX2}")
    set_source_files_properties(src/kernels_avx512.cpp PROPERTIES
        COMPILE_OPTIONS "${SIMD_LAB_FLAGS_AVX512}")
endif()

# ---------------- 正确性测试 ----------------

add_executable(test_simd src/test_main.cpp)
target_link_libraries(test_simd PRIVATE simd_kernels ${SIMD_LAB_EXTRA_LIBS})
target_compile_options(test_simd PRIVATE ${SIMD_LAB_WARN_FLAGS})

enable_testing()
add_test(NAME simd_correctness COMMAND test_simd)

# ---------------- 性能用例 ----------------

if(SIMD_LAB_BUILD_BENCH)
    include(FetchContent)

    # 优先复用 examples/matmul-demo 已经拉取好的 Google Benchmark 源码，离线也能构建
    if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_BENCHMARK)
        set(_simd_local_bench "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/matmul-demo/build/_deps/benchmark-src")
        if(EXISTS "${_simd_local_bench}/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_BENCHMARK "${_simd_local_bench}" CACHE PATH
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

    add_executable(bench_simd bench/bench_simd.cpp)
    target_link_libraries(bench_simd PRIVATE simd_kernels benchmark::benchmark ${SIMD_LAB_EXTRA_LIBS})
endif()
