# Skip list: structural union/split and experimental analysis

Randomized skip list over distinct signed 64-bit keys (C++17) with `search`,
`insert`, `ordered_union` and `split`. Union and split relink existing nodes
(no allocation, no new heights). Includes a rebuilding split baseline, tests,
Google Benchmark timing and instrumented work counts.

Files: `src/skiplist.cpp` (the skip list, single file), `tests/test_skiplist.cpp`,
`bench/bench_skiplist.cpp` (workloads, timing, and work counts when built with
`SKIPLIST_INSTRUMENT`), `scripts/` (run and analysis), `results/` (raw output),
`report.pdf`.

## Dependencies

- GCC 11.4.0, CMake 3.22.1, Ubuntu 22.04.5 under WSL2, AMD Ryzen AI 9 365
- Google Benchmark v1.8.3: CMake uses an installed copy if it finds one
  (works offline); otherwise it downloads v1.8.3 from GitHub (needs internet)
- Python 3.10.12 with matplotlib, pandas, numpy (figures only)

## Commands

All at once (about 5 minutes): `scripts/run_all.sh`. Without Google Benchmark
or internet, the tests and work counts still build: add `-DSKIPLIST_BENCH=OFF`
to the first `cmake` command below (or just run
`g++ -std=c++17 -O2 -Isrc tests/test_skiplist.cpp -o test_skiplist && ./test_skiplist`).
Step by step:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/test_skiplist                                   # correctness tests

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSKIPLIST_SANITIZE=ON
cmake --build build-asan -j --target test_skiplist
ASAN_OPTIONS=detect_leaks=1 ./build-asan/test_skiplist  # memory check

./build/work_counts > results/work_counts.csv           # logical-work counts

# pin to one core; omit "taskset -c 2" if taskset is unavailable
taskset -c 2 ./build/bench_skiplist \
  --benchmark_out=results/bench.json --benchmark_out_format=json

python3 scripts/analyze.py results                      # figure and tables
```

## Seeds

- Tests: workload seeds 11, 22, 33 (height seed = seed * 0x9e3779b97f4a7c15)
- Benchmark workload: `std::mt19937_64(20261024 ^ (N << 8) ^ salt)`
- Benchmark height seeds: 0x9d2c5680a1b2c3d4, 0x1f83d9abfb41bd6b,
  0x5be0cd19137e2179 (work counts add 0xc0ffee03 ... 0xc0ffee09)

Height generation and workload generation never share state.

## Memory check

Tests under AddressSanitizer + UBSan + LeakSanitizer: 82,549 checks, 0
failures, no errors, no leaks. Valgrind was not used.

## Limitations

- Heights are capped at 32, which is irrelevant for n <= 2^20 (bounds hold up to about 2^32).
- Union/split timings are warm-cache and close to the clock overhead, so they
  cannot resolve the logarithmic slope; the work counts do.
- Measured under WSL2, which adds noise. `ordered_union` does not check its
  precondition (max(A) < min(B)).

## Acknowledgements

- Course lecture on randomized data structures; W. Pugh, *A Skip List
  Cookbook* (1990); SplitMix64 (Steele, Lea, Flood, OOPSLA 2014); Google
  Benchmark user guide.
- AI assistance: Claude (Anthropic, via Claude Code) generated parts of the code,
  scripts and report text under my direction. I have
  reviewed, run and verified all of it.

## Declaration of individual work

I declare that this project was carried out individually, that I did not
share code, results or report text with other students, and that all
external sources and tools, including AI assistance, are acknowledged above.
I understand every part of the submission and can explain it.
