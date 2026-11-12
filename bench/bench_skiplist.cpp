// bench_skiplist.cpp — Google Benchmark timing of the four structural kernels
// and of the rebuilding split baseline (Sections 6 and 7).
//
// Arguments of every family: N (size, 2^10 .. 2^20, factor 4) and seed (index
// into wl::kHeightSeeds). Repetitions(3) gives 3 timing repetitions per seed.
//
// Timing: manual timing with std::chrono::steady_clock around exactly the
// kernel calls (UseManualTime). Each benchmark iteration performs a fixed
// batch of B operations (B independent of N) and reports batch_time / B, so
// every reported time, and the complexity fit, is per single operation.
// Input construction, workload generation and restoration lie outside the
// clock. Iteration counts are fixed (no calibration), so large inputs are not
// rebuilt an unbounded number of times.
//
// The same source is built twice:
//   bench_skiplist : no SKIPLIST_INSTRUMENT; Google Benchmark timing, no
//                    counters run.
//   work_counts    : SKIPLIST_INSTRUMENT; no Google Benchmark. Runs the same
//                    workloads once and prints the logical-work counters
//                    (Section 6) as CSV on stdout.
//
// Workload generation (namespace wl) is shared by both builds, so they use
// exactly the same inputs.
#include <algorithm>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

#ifdef SKIPLIST_INSTRUMENT
#include <cstdio>
#else
#include <benchmark/benchmark.h>

#include <chrono>
#include <string>
#endif

#include <utility>

#include "skiplist.cpp"

// ---------------------------------------------------------------------------
// Workload generation
//
// Keys: the stored set for size N is the even integers 0, 2, ..., 2(N-1).
// Odd integers in [1, 2N-1] are guaranteed misses / absent keys. Keys are
// inserted in a shuffled order so that the memory layout of nodes is not
// correlated with key order.
//
// All key/query/order randomness comes from std::mt19937_64 seeded with
// kWorkloadSeed (mixed with N). Node heights come from each SkipList's own
// generator, seeded with one of kHeightSeeds. The two never share state.
// ---------------------------------------------------------------------------
namespace wl {

constexpr std::uint64_t kWorkloadSeed = 20261024;
constexpr std::uint64_t kHeightSeeds[] = {0x9d2c5680a1b2c3d4ULL,
                                          0x1f83d9abfb41bd6bULL,
                                          0x5be0cd19137e2179ULL};
constexpr int kNumHeightSeeds = 3;

// Batch sizes: fixed across N.
constexpr int kSearchBatch = 1024;  // queries per timed batch
constexpr int kInsertBatch = 1024;  // insertions per timed batch, N -> N+B
constexpr int kStructBatch = 256;   // union / split ops per iteration
constexpr int kSearchPool = 64;     // distinct search batches cycled
constexpr int kInsertPool = 8;      // distinct insert batches cycled

inline std::int64_t stored_key(std::int64_t i) { return 2 * i; }

inline std::mt19937_64 workload_rng(std::int64_t n, std::uint64_t salt) {
  return std::mt19937_64(kWorkloadSeed ^ (static_cast<std::uint64_t>(n) << 8) ^
                         salt);
}

// Keys stored_key(lo) .. stored_key(hi-1), inserted in shuffled order.
inline sl::SkipList build_range(std::int64_t lo, std::int64_t hi,
                                std::uint64_t height_seed) {
  std::vector<std::int64_t> keys;
  keys.reserve(static_cast<std::size_t>(hi - lo));
  for (std::int64_t i = lo; i < hi; ++i) keys.push_back(stored_key(i));
  auto rng = workload_rng(hi - lo, static_cast<std::uint64_t>(lo) + 1);
  std::shuffle(keys.begin(), keys.end(), rng);
  sl::SkipList s(height_seed);
  for (std::int64_t k : keys) s.insert(k);
  return s;
}

inline sl::SkipList build(std::int64_t n, std::uint64_t height_seed) {
  return build_range(0, n, height_seed);
}

// Search queries: `batches` batches of `batch` queries. Every batch has
// exactly batch/2 hits (2i) and batch/2 misses (2i+1) with i uniform in
// [0, N), i.e. spread over the whole stored key range, in random order.
// Cycling through many batches keeps the timed queries from re-touching the
// same few search paths at large N.
inline std::vector<std::vector<std::int64_t>> search_batches(std::int64_t n,
                                                             int batches,
                                                             int batch) {
  auto rng = workload_rng(n, 0x5ea7c4);
  std::uniform_int_distribution<std::int64_t> idx(0, n - 1);
  std::vector<std::vector<std::int64_t>> out(static_cast<std::size_t>(batches));
  for (auto& b : out) {
    for (int j = 0; j < batch; ++j)
      b.push_back(stored_key(idx(rng)) + (j % 2));  // even j hit, odd j miss
    std::shuffle(b.begin(), b.end(), rng);
  }
  return out;
}

// `batches` disjoint-within-batch sets of absent (odd) keys, uniform over
// the key range, each in random order.
inline std::vector<std::vector<std::int64_t>> insert_batches(std::int64_t n,
                                                             int batches,
                                                             int batch) {
  auto rng = workload_rng(n, 0x1a5e47);
  std::uniform_int_distribution<std::int64_t> idx(0, n - 1);
  std::vector<std::vector<std::int64_t>> out(static_cast<std::size_t>(batches));
  for (auto& b : out) {
    std::set<std::int64_t> chosen;
    while (static_cast<int>(chosen.size()) < batch)
      chosen.insert(stored_key(idx(rng)) + 1);
    b.assign(chosen.begin(), chosen.end());
    std::shuffle(b.begin(), b.end(), rng);
  }
  return out;
}

// Union inputs: A = first N/2 keys, B = last N/2 keys, independent heights.
inline std::uint64_t union_b_seed(std::uint64_t height_seed) {
  return height_seed * 0x9e3779b97f4a7c15ULL + 0x632be59bd9b4e019ULL;
}
inline std::int64_t union_pivot(std::int64_t n) { return stored_key(n / 2 - 1); }

// Split pivot: an absent key between the two halves, so |L| = |R| = N/2.
inline std::int64_t split_pivot(std::int64_t n) {
  return stored_key(n / 2) - 1;
}

}  // namespace wl

#ifndef SKIPLIST_INSTRUMENT
// ===========================================================================
// Timing build: Google Benchmark
// ===========================================================================
namespace {

using Clock = std::chrono::steady_clock;

double seconds(Clock::duration d) {
  return std::chrono::duration<double>(d).count();
}

std::uint64_t height_seed(const benchmark::State& state) {
  return wl::kHeightSeeds[state.range(1)];
}

void finish(benchmark::State& state, int batch) {
  state.SetComplexityN(state.range(0));
  state.counters["B"] = batch;
}

// search: N stored keys, batches of 1024 queries (50 % hits / 50 % misses).
void BM_Search(benchmark::State& state) {
  const std::int64_t n = state.range(0);
  sl::SkipList s = wl::build(n, height_seed(state));
  const auto batches = wl::search_batches(n, wl::kSearchPool, wl::kSearchBatch);
  std::size_t next = 0;
  for (auto _ : state) {
    const auto& q = batches[next++ % batches.size()];
    const auto t0 = Clock::now();
    for (std::int64_t k : q) {
      bool hit = s.search(k);
      benchmark::DoNotOptimize(hit);
    }
    const auto t1 = Clock::now();
    state.SetIterationTime(seconds(t1 - t0) / wl::kSearchBatch);
  }
  finish(state, wl::kSearchBatch);
}

// insert: a batch of 1024 absent keys grows the list from N to N+1024; the
// batch is then erased (untimed), restoring the identical N-key structure.
void BM_Insert(benchmark::State& state) {
  const std::int64_t n = state.range(0);
  sl::SkipList s = wl::build(n, height_seed(state));
  const auto batches = wl::insert_batches(n, wl::kInsertPool, wl::kInsertBatch);
  std::size_t next = 0;
  for (auto _ : state) {
    const auto& keys = batches[next++ % batches.size()];
    const auto t0 = Clock::now();
    for (std::int64_t k : keys) {
      bool inserted = s.insert(k);
      benchmark::DoNotOptimize(inserted);
    }
    benchmark::ClobberMemory();
    const auto t1 = Clock::now();
    state.SetIterationTime(seconds(t1 - t0) / wl::kInsertBatch);
    for (std::int64_t k : keys) s.erase_for_restore(k);  // untimed
  }
  finish(state, wl::kInsertBatch);
}

// ordered_union: |A| = |B| = N/2, disjoint ranges, independent height seeds.
// Each union is timed on its own and undone by an untimed split at max(A),
// which returns exactly the same nodes to A and B.
void BM_Union(benchmark::State& state) {
  const std::int64_t n = state.range(0);
  const std::uint64_t seed = height_seed(state);
  sl::SkipList a = wl::build_range(0, n / 2, seed);
  sl::SkipList b = wl::build_range(n / 2, n, wl::union_b_seed(seed));
  const std::int64_t pivot = wl::union_pivot(n);
  for (auto _ : state) {
    Clock::duration total{};
    for (int j = 0; j < wl::kStructBatch; ++j) {
      const auto t0 = Clock::now();
      sl::SkipList u = sl::ordered_union(a, b);
      benchmark::DoNotOptimize(u);
      benchmark::ClobberMemory();
      const auto t1 = Clock::now();
      total += t1 - t0;
      auto parts = sl::split(u, pivot);  // untimed restoration
      a = std::move(parts.first);
      b = std::move(parts.second);
    }
    state.SetIterationTime(seconds(total) / wl::kStructBatch);
  }
  finish(state, wl::kStructBatch);
}

// split: N keys, pivot between the two halves (|L| = |R| = N/2), chosen
// before timing. Each split is timed on its own and undone by an untimed
// ordered_union, which restores the identical structure.
void BM_Split(benchmark::State& state) {
  const std::int64_t n = state.range(0);
  sl::SkipList s = wl::build(n, height_seed(state));
  const std::int64_t pivot = wl::split_pivot(n);
  for (auto _ : state) {
    Clock::duration total{};
    for (int j = 0; j < wl::kStructBatch; ++j) {
      const auto t0 = Clock::now();
      auto parts = sl::split(s, pivot);
      benchmark::DoNotOptimize(parts);
      benchmark::ClobberMemory();
      const auto t1 = Clock::now();
      total += t1 - t0;
      s = sl::ordered_union(parts.first, parts.second);  // untimed
    }
    state.SetIterationTime(seconds(total) / wl::kStructBatch);
  }
  finish(state, wl::kStructBatch);
}

// Rebuilding split baseline, same workload as BM_Split. One operation per
// iteration (B = 1). The timed region includes all of the baseline's
// allocations, height draws and the freeing of the consumed nodes. The
// outputs are then discarded and S is rebuilt identically (untimed).
void BM_SplitRebuild(benchmark::State& state) {
  const std::int64_t n = state.range(0);
  const std::uint64_t seed = height_seed(state);
  sl::SkipList s = wl::build(n, seed);
  const std::int64_t pivot = wl::split_pivot(n);
  for (auto _ : state) {
    const auto t0 = Clock::now();
    auto parts = sl::split_rebuild(s, pivot);
    benchmark::DoNotOptimize(parts);
    benchmark::ClobberMemory();
    const auto t1 = Clock::now();
    state.SetIterationTime(seconds(t1 - t0));
    { auto discard = std::move(parts); }  // untimed
    s = wl::build(n, seed);               // untimed
  }
  finish(state, 1);
}

// Cost of one Clock::now() pair, which is included in every individually
// timed union/split above (search/insert time a whole batch per pair).
void BM_ClockOverhead(benchmark::State& state) {
  for (auto _ : state) {
    Clock::duration total{};
    for (int j = 0; j < wl::kStructBatch; ++j) {
      const auto t0 = Clock::now();
      benchmark::ClobberMemory();
      const auto t1 = Clock::now();
      total += t1 - t0;
    }
    state.SetIterationTime(seconds(total) / wl::kStructBatch);
  }
}

void SizesAndSeeds(benchmark::internal::Benchmark* b) {
  b->ArgNames({"N", "seed"});
  for (std::int64_t n = 1 << 10; n <= (1 << 20); n <<= 2)
    for (int s = 0; s < wl::kNumHeightSeeds; ++s) b->Args({n, s});
}

#define SKIPLIST_BENCH(fn, iters)                                     \
  BENCHMARK(fn)                                                       \
      ->Apply(SizesAndSeeds)                                          \
      ->UseManualTime()                                               \
      ->Iterations(iters)                                             \
      ->Repetitions(3)                                                \
      ->Complexity(benchmark::oAuto)                                  \
      ->Unit(benchmark::kNanosecond)

SKIPLIST_BENCH(BM_Search, 200);
SKIPLIST_BENCH(BM_Insert, 50);
SKIPLIST_BENCH(BM_Union, 20);
SKIPLIST_BENCH(BM_Split, 20);
SKIPLIST_BENCH(BM_SplitRebuild, 5);
BENCHMARK(BM_ClockOverhead)
    ->UseManualTime()
    ->Iterations(2000)
    ->Repetitions(3)
    ->Unit(benchmark::kNanosecond);

}  // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::AddCustomContext("compiler", SKIPLIST_COMPILER);
  benchmark::AddCustomContext("cxx_flags", SKIPLIST_FLAGS);
  benchmark::AddCustomContext("google_benchmark", SKIPLIST_GBENCH);
  benchmark::AddCustomContext("workload_seed",
                              std::to_string(wl::kWorkloadSeed));
  std::string seeds;
  for (std::uint64_t s : wl::kHeightSeeds) {
    if (!seeds.empty()) seeds += ",";
    seeds += std::to_string(s);
  }
  benchmark::AddCustomContext("height_seeds", seeds);
  benchmark::AddCustomContext(
      "timing", "manual steady_clock; reported time = batch time / B");
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}

#else
// ===========================================================================
// Instrumented build: logical-work counts (Section 6)
//   key_comparisons : comparisons of a stored key with the argument key
//   link_reads      : forward/head pointers read while traversing
//   link_writes     : forward/head pointers assigned
//   node_allocs / node_frees : data nodes allocated / freed
// Counters are reset immediately before and read immediately after the
// measured kernel calls; construction and restoration are not counted.
// Output: CSV with totals per (kernel, N, seed) and the number of operations
// they cover.
// ===========================================================================
namespace {

// Height seeds: the three timing seeds plus seven more, for smoother means.
constexpr int kSeeds = 10;
std::uint64_t count_seed(int i) {
  return i < wl::kNumHeightSeeds ? wl::kHeightSeeds[i]
                                 : 0xc0ffee00ULL + static_cast<std::uint64_t>(i);
}

void row(const char* kernel, std::int64_t n, int seed, long ops) {
  const sl::WorkCounters& c = sl::g_counters;
  std::printf("%s,%lld,%d,%ld,%llu,%llu,%llu,%llu,%llu\n", kernel,
              static_cast<long long>(n), seed, ops,
              static_cast<unsigned long long>(c.key_comparisons),
              static_cast<unsigned long long>(c.link_reads),
              static_cast<unsigned long long>(c.link_writes),
              static_cast<unsigned long long>(c.node_allocs),
              static_cast<unsigned long long>(c.node_frees));
}

}  // namespace

int main() {
  std::printf(
      "kernel,N,seed,ops,key_comparisons,link_reads,link_writes,node_allocs,"
      "node_frees\n");
  for (std::int64_t n = 1 << 10; n <= (1 << 20); n <<= 2) {
    for (int si = 0; si < kSeeds; ++si) {
      const std::uint64_t seed = count_seed(si);
      sl::SkipList s = wl::build(n, seed);

      // search: every query of the timing pool
      const auto qb = wl::search_batches(n, wl::kSearchPool, wl::kSearchBatch);
      sl::reset_counters();
      long ops = 0;
      for (const auto& q : qb)
        for (std::int64_t k : q) {
          s.search(k);
          ++ops;
        }
      row("search", n, si, ops);

      // insert: one batch of absent keys, N -> N + B, then undone
      const auto ib = wl::insert_batches(n, 1, wl::kInsertBatch);
      sl::reset_counters();
      for (std::int64_t k : ib[0]) s.insert(k);
      row("insert", n, si, wl::kInsertBatch);
      for (std::int64_t k : ib[0]) s.erase_for_restore(k);

      // split (structural), then restore with union (not counted)
      const std::int64_t pivot = wl::split_pivot(n);
      sl::reset_counters();
      auto parts = sl::split(s, pivot);
      row("split", n, si, 1);
      s = sl::ordered_union(parts.first, parts.second);

      // split (rebuilding baseline) on the identical structure
      sl::reset_counters();
      auto rparts = sl::split_rebuild(s, pivot);
      row("split_rebuild", n, si, 1);

      // ordered_union with |A| = |B| = N/2 and independent heights
      sl::SkipList a = wl::build_range(0, n / 2, seed);
      sl::SkipList b = wl::build_range(n / 2, n, wl::union_b_seed(seed));
      sl::reset_counters();
      sl::SkipList u = sl::ordered_union(a, b);
      row("union", n, si, 1);
    }
    std::fprintf(stderr, "work_counts: N=%lld done\n",
                 static_cast<long long>(n));
  }
  return 0;
}
#endif
