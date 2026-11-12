cmake_minimum_required(VERSION 3.16)
project(skiplist LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SKIPLIST_SANITIZE "Build with AddressSanitizer + UBSan (tests)" OFF)
option(SKIPLIST_BENCH "Build bench_skiplist (needs Google Benchmark)" ON)

# Google Benchmark: use an installed copy if it is version 1.8.3 or newer
# (works offline), otherwise download v1.8.3 at configure time (needs
# internet). Declared before our own flags so the sanitizer/warning flags
# below apply only to this project's targets. Without Google Benchmark use
# -DSKIPLIST_BENCH=OFF: the tests and work_counts still build.
if(SKIPLIST_BENCH)
  find_package(benchmark 1.8.3 QUIET)
  if(benchmark_FOUND)
    set(SKIPLIST_GBENCH_VERSION "${benchmark_VERSION} (installed)")
  else()
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googlebenchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG v1.8.3)
    FetchContent_MakeAvailable(googlebenchmark)
    set(SKIPLIST_GBENCH_VERSION "v1.8.3 (FetchContent)")
  endif()
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(SKIPLIST_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=all -g)
  add_link_options(-fsanitize=address,undefined)
endif()

# The skip list is a single file (src/skiplist.cpp) that each executable
# #includes, so every target compiles its own copy.
include_directories(src)

add_executable(test_skiplist tests/test_skiplist.cpp)

# Instrumented build of the same source as bench_skiplist (no Google Benchmark).
# Counters are enabled here only, never in the timing benchmarks.
add_executable(work_counts bench/bench_skiplist.cpp)
target_compile_definitions(work_counts PRIVATE SKIPLIST_INSTRUMENT)

if(SKIPLIST_BENCH)
  add_executable(bench_skiplist bench/bench_skiplist.cpp)
  target_link_libraries(bench_skiplist PRIVATE benchmark::benchmark)
  target_compile_definitions(bench_skiplist PRIVATE
    SKIPLIST_GBENCH="${SKIPLIST_GBENCH_VERSION}"
    SKIPLIST_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    SKIPLIST_FLAGS="${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE} -std=c++17")
endif()
