#!/usr/bin/env bash
# Reproduces every result in the archive: build, tests, sanitizer run,
# work counts, timing benchmarks, figure/tables and report.
# Usage: scripts/run_all.sh            (from the project root)
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== release build"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

echo "== correctness tests"
./build/test_skiplist | tee results/test_output.txt

echo "== tests under AddressSanitizer + UBSan"
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSKIPLIST_SANITIZE=ON
cmake --build build-asan -j --target test_skiplist
ASAN_OPTIONS=detect_leaks=1 ./build-asan/test_skiplist | tee results/test_asan_output.txt

echo "== logical-work counts (instrumented build)"
./build/work_counts > results/work_counts.csv

# Single-threaded benchmarks. If taskset exists, pin to one core: BENCH_CPU
# (default 2, or 0 on machines with fewer than 3 cores).
PIN=""
if command -v taskset >/dev/null 2>&1; then
  DEFAULT_CPU=$(( $(nproc) > 2 ? 2 : 0 ))
  PIN="taskset -c ${BENCH_CPU:-$DEFAULT_CPU}"
fi
echo "== timing benchmarks (single thread${PIN:+, $PIN})"
$PIN ./build/bench_skiplist \
  --benchmark_out=results/bench.json --benchmark_out_format=json \
  --benchmark_display_aggregates_only=true | tee results/bench_console.txt

echo "== figure and tables"
python3 scripts/analyze.py results
