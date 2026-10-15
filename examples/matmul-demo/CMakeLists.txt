cmake_minimum_required(VERSION 3.20)
project(matmul_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.4
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# MinGW：静态链接运行时，避免与系统其他 libstdc++ DLL（如 Git for Windows 附带的）ABI 冲突
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

add_executable(bench_matmul bench_matmul.cpp matmul.cpp)
target_link_libraries(bench_matmul PRIVATE benchmark::benchmark)

add_executable(app app.cpp matmul.cpp)
