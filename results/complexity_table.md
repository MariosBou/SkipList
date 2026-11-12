Per-operation real time, 54 runs per kernel (6 sizes x 3 height seeds x 3 repetitions).
Clock-pair overhead (included in union/split): 26.0 ns

GB = model chosen by Google Benchmark (on CPU time), RMS here on real time; auto = same selection redone on real time.

| kernel | GB model | coef (ns) | RMS | auto (real) model | coef (ns) | RMS | supplied model | coef (ns) | RMS | a + b·lgN: a, b (ns) | RMS |
|---|---|---|---|---|---|---|---|---|---|---|---|
| search | N | 0.001028 | 38% | N | 0.001028 | 38% | lgN | 25.25 | 81% | -932, 84.33 | 51% |
| insert | N | 0.001012 | 45% | N | 0.001012 | 45% | lgN | 26.96 | 64% | -796.2, 77.43 | 43% |
| ordered_union | (1) | 88.07 | 12% | (1) | 88.07 | 12% | lgN | 5.671 | 18% | 61.15, 1.795 | 10% |
| split | (1) | 98.06 | 16% | (1) | 98.06 | 16% | lgN | 6.288 | 23% | 76.1, 1.464 | 15% |
| split_rebuild (baseline) | N^2 | 0.0003261 | 19% | NlgN | 16.7 | 18% | NlgN | 16.7 | 18% | n/a | n/a |

Logical work per op (comparisons + link reads + link writes), mean of 10 height seeds:

| kernel | 2^10 | 2^12 | 2^14 | 2^16 | 2^18 | 2^20 | fit | coef | RMS |
|---|---|---|---|---|---|---|---|---|---|
| search | 40.43 | 48.36 | 55.96 | 63.56 | 70.85 | 79.32 | lgN | 3.98 | 0.8% |
| insert | 49.67 | 55.9 | 63.06 | 70.56 | 77.79 | 86.28 | lgN | 4.44 | 4.1% |
| union | 60.1 | 78.2 | 83.7 | 92.8 | 106.2 | 122.6 | lgN | 6.03 | 3.4% |
| split | 94.7 | 111.2 | 129 | 144.7 | 158.6 | 184.9 | lgN | 9.12 | 2.2% |
| split_rebuild | 3.507e+04 | 1.604e+05 | 7.537e+05 | 3.376e+06 | 1.498e+07 | 6.658e+07 | NlgN | 3.17 | 0.2% |

Time per unit of logical work (ns), timing mean / work mean:

- search: 2^10: 1.58, 2^12: 1.98, 2^14: 2.62, 2^16: 3.66, 2^18: 6.12, 2^20: 12.91
- insert: 2^10: 2.46, 2^12: 2.50, 2^14: 3.07, 2^16: 3.91, 2^18: 5.98, 2^20: 11.53
- union: 2^10: 1.32, 2^12: 1.08, 2^14: 1.03, 2^16: 0.96, 2^18: 0.84, 2^20: 0.83
- split: 2^10: 1.01, 2^12: 0.82, 2^14: 0.72, 2^16: 0.68, 2^18: 0.64, 2^20: 0.59
- split_rebuild: 2^10: 2.18, 2^12: 2.28, 2^14: 1.97, 2^16: 2.06, 2^18: 3.49, 2^20: 5.36

split_rebuild allocations/frees per op: 1024/1024, 4096/4096, 16384/16384, 65536/65536, 262144/262144, 1048576/1048576 (structural kernels: 0/0)

Context: compiler=GNU 11.4.0, cxx_flags= -O3 -DNDEBUG -std=c++17, google_benchmark=v1.8.3 (FetchContent), mhz_per_cpu=1996, library_build_type=release
