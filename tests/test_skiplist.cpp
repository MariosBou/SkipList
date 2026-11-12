// test_skiplist.cpp — automated correctness checks (Section 5).
// Every result is compared with std::set as an independent reference, and
// the structural invariant checker runs after every operation.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "skiplist.cpp"

using sl::SkipList;
using Ref = std::set<std::int64_t>;

namespace {

constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      ++g_failures;                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,       \
                   __LINE__, #cond);                                    \
    }                                                                   \
  } while (0)

// List content equals the reference and every invariant holds.
#define CHECK_LIST(list, ref)                                           \
  do {                                                                  \
    std::string err_;                                                   \
    const Ref& ref_ = (ref);                                            \
    bool ok_ = (list).check_invariants(&err_);                          \
    CHECK(ok_);                                                         \
    if (!ok_) std::fprintf(stderr, "  invariant: %s\n", err_.c_str());  \
    CHECK((list).to_vector() ==                                         \
          std::vector<std::int64_t>(ref_.begin(), ref_.end()));         \
  } while (0)

bool same_nodes(const std::vector<sl::NodeInfo>& a,
                const std::vector<sl::NodeInfo>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].addr != b[i].addr || a[i].key != b[i].key ||
        a[i].height != b[i].height)
      return false;
  return true;
}

std::vector<sl::NodeInfo> concat(std::vector<sl::NodeInfo> a,
                                 const std::vector<sl::NodeInfo>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

void split_ref(const Ref& s, std::int64_t x, Ref& l, Ref& r) {
  l.clear();
  r.clear();
  for (std::int64_t k : s) (k <= x ? l : r).insert(k);
}

SkipList build(const Ref& keys, std::uint64_t seed) {
  SkipList s(seed);
  for (std::int64_t k : keys) s.insert(k);
  return s;
}

// Split S at x, check both parts and node preservation, union back and check
// that S is restored with the very same nodes.
void check_split_roundtrip(SkipList& s, const Ref& ref, std::int64_t x) {
  const auto before = s.nodes();
  Ref lref, rref;
  split_ref(ref, x, lref, rref);

  auto parts = sl::split(s, x);
  CHECK_LIST(parts.first, lref);
  CHECK_LIST(parts.second, rref);
  CHECK(s.empty());
  CHECK(s.check_invariants());
  CHECK(same_nodes(concat(parts.first.nodes(), parts.second.nodes()), before));

  s = sl::ordered_union(parts.first, parts.second);
  CHECK_LIST(s, ref);
  CHECK(parts.first.empty() && parts.second.empty());
  CHECK(same_nodes(s.nodes(), before));
}

// ---------------------------------------------------------------------------

void test_empty_and_singleton() {
  SkipList s(1);
  Ref ref;
  CHECK_LIST(s, ref);
  CHECK(!s.search(0) && !s.search(kMin) && !s.search(kMax));
  CHECK(s.size() == 0 && s.level() == 0);

  auto p = sl::split(s, 0);
  CHECK(p.first.empty() && p.second.empty() && s.empty());
  SkipList e1(2), e2(3);
  SkipList u = sl::ordered_union(e1, e2);
  CHECK_LIST(u, ref);

  CHECK(s.insert(42));
  ref.insert(42);
  CHECK_LIST(s, ref);
  CHECK(s.search(42) && !s.search(41) && !s.search(43));
  check_split_roundtrip(s, ref, 41);  // before the only key
  check_split_roundtrip(s, ref, 42);  // exactly at it
  check_split_roundtrip(s, ref, 43);  // after it
}

void test_duplicates() {
  SkipList s(7);
  Ref ref;
  for (std::int64_t k = 0; k < 200; k += 3) {
    s.insert(k);
    ref.insert(k);
  }
  const auto before = s.nodes();
  for (std::int64_t k = 0; k < 200; k += 3) CHECK(!s.insert(k));
  CHECK_LIST(s, ref);
  CHECK(same_nodes(s.nodes(), before));  // no node or height changed
}

void test_boundary_keys() {
  const Ref ref{kMin, kMin + 1, -1, 0, 1, kMax - 1, kMax};
  SkipList s = build(ref, 11);
  CHECK_LIST(s, ref);
  for (std::int64_t k : ref) CHECK(s.search(k));
  CHECK(!s.search(kMin + 2) && !s.search(kMax - 2) && !s.search(2));
  CHECK(!s.insert(kMin) && !s.insert(kMax));
  check_split_roundtrip(s, ref, kMin);      // L = {kMin}
  check_split_roundtrip(s, ref, kMax);      // R empty
  check_split_roundtrip(s, ref, kMax - 1);  // R = {kMax}
  check_split_roundtrip(s, ref, 0);

  const Ref inner{-5, 0, 5};
  SkipList t = build(inner, 12);
  check_split_roundtrip(t, inner, kMin);  // before all, kMin not stored
  check_split_roundtrip(t, inner, kMax);  // after all
}

void test_split_positions() {
  Ref ref;
  for (std::int64_t k = 0; k < 5000; k += 2) ref.insert(k);
  SkipList s = build(ref, 21);
  check_split_roundtrip(s, ref, -1);      // before all keys
  check_split_roundtrip(s, ref, 10000);   // after all keys
  check_split_roundtrip(s, ref, 2500);    // at a stored key
  check_split_roundtrip(s, ref, 2501);    // between keys
  check_split_roundtrip(s, ref, 0);       // at the first key
  check_split_roundtrip(s, ref, 4998);    // at the last key
}

void test_union_cases() {
  // Empty operands on either side.
  {
    Ref ref{1, 2, 3};
    SkipList a = build(ref, 31), empty(32);
    const auto before = a.nodes();
    SkipList u = sl::ordered_union(a, empty);
    CHECK_LIST(u, ref);
    CHECK(same_nodes(u.nodes(), before));
    SkipList empty2(33);
    SkipList v = sl::ordered_union(empty2, u);
    CHECK_LIST(v, ref);
    CHECK(same_nodes(v.nodes(), before));
    CHECK(a.empty() && empty.empty() && empty2.empty() && u.empty());
  }
  // Very unequal sizes, both orders, many height seeds so that both
  // level(A) < level(B) and level(A) > level(B) occur.
  bool a_taller = false, b_taller = false;
  for (std::uint64_t seed = 0; seed < 40; ++seed) {
    Ref small{-1000 - static_cast<std::int64_t>(seed)};
    Ref big;
    for (std::int64_t k = 0; k < 10000; ++k) big.insert(k);
    Ref all = small;
    all.insert(big.begin(), big.end());

    SkipList a = build(small, 100 + seed), b = build(big, 200 + seed);
    if (a.level() > b.level()) a_taller = true;
    if (a.level() < b.level()) b_taller = true;
    auto expect = concat(a.nodes(), b.nodes());
    SkipList u = sl::ordered_union(a, b);
    CHECK_LIST(u, all);
    CHECK(same_nodes(u.nodes(), expect));

    // big on the left, one key on the right
    Ref one{20000};
    Ref all2 = big;
    all2.insert(20000);
    SkipList c = build(big, 300 + seed), d = build(one, 400 + seed);
    if (c.level() > d.level()) a_taller = true;
    auto expect2 = concat(c.nodes(), d.nodes());
    SkipList w = sl::ordered_union(c, d);
    CHECK_LIST(w, all2);
    CHECK(same_nodes(w.nodes(), expect2));
  }
  // Force a tall left operand: a single-key list with a high node.
  for (std::uint64_t seed = 0; seed < 2000 && !a_taller; ++seed) {
    SkipList a(seed), b(seed + 1);
    a.insert(-1);
    for (int k = 0; k < 4; ++k) b.insert(k);
    if (a.level() > b.level()) {
      a_taller = true;
      SkipList u = sl::ordered_union(a, b);
      CHECK_LIST(u, (Ref{-1, 0, 1, 2, 3}));
    }
  }
  CHECK(a_taller);
  CHECK(b_taller);
}

void test_independence_and_reuse() {
  Ref ref;
  for (std::int64_t k = 0; k < 1000; ++k) ref.insert(k * 10);
  SkipList s = build(ref, 41);
  Ref lref, rref;
  split_ref(ref, 4995, lref, rref);
  auto parts = sl::split(s, 4995);
  SkipList& l = parts.first;
  SkipList& r = parts.second;

  // Modify L; R must be unaffected, and vice versa.
  const auto r_before = r.nodes();
  for (std::int64_t k = 1; k < 4995; k += 97) {
    CHECK(l.insert(k) == lref.insert(k).second);
  }
  CHECK(l.insert(1000000));  // L is a set of its own; any key is fine
  lref.insert(1000000);
  CHECK_LIST(l, lref);
  CHECK_LIST(r, rref);
  CHECK(same_nodes(r.nodes(), r_before));

  const auto l_before = l.nodes();
  for (std::int64_t k = 5001; k < 10000; k += 89)
    CHECK(r.insert(k) == rref.insert(k).second);
  CHECK(r.insert(-7));
  rref.insert(-7);
  CHECK_LIST(r, rref);
  CHECK_LIST(l, lref);
  CHECK(same_nodes(l.nodes(), l_before));

  // The consumed input is empty and fully reusable.
  CHECK(s.empty());
  Ref sref;
  for (std::int64_t k : {5, 3, 9, 1}) {
    CHECK(s.insert(k));
    sref.insert(k);
  }
  CHECK_LIST(s, sref);
  check_split_roundtrip(s, sref, 4);

  // Consumed union inputs are reusable too.
  SkipList a = build(Ref{1, 2}, 42), b = build(Ref{10, 20}, 43);
  SkipList u = sl::ordered_union(a, b);
  CHECK(a.empty() && b.empty());
  CHECK(a.insert(100) && b.insert(-100));
  CHECK_LIST(a, Ref{100});
  CHECK_LIST(b, Ref{-100});
  SkipList v = sl::ordered_union(b, a);
  CHECK_LIST(v, (Ref{-100, 100}));

  // Moved-from list is empty and reusable.
  SkipList m = std::move(u);
  CHECK(u.empty() && u.check_invariants());
  CHECK(u.insert(3));
  CHECK_LIST(u, Ref{3});
  CHECK_LIST(m, (Ref{1, 2, 10, 20}));
}

void test_split_rebuild_contract() {
  Ref ref;
  for (std::int64_t k = -500; k < 500; k += 3) ref.insert(k);
  for (std::int64_t x : {kMin, std::int64_t{-501}, std::int64_t{-500},
                         std::int64_t{0}, std::int64_t{1}, std::int64_t{499},
                         kMax}) {
    SkipList s = build(ref, 51);
    Ref lref, rref;
    split_ref(ref, x, lref, rref);
    auto parts = sl::split_rebuild(s, x);
    CHECK_LIST(parts.first, lref);
    CHECK_LIST(parts.second, rref);
    CHECK(s.empty() && s.check_invariants());
    CHECK(s.insert(7) && s.search(7));
    SkipList back = sl::ordered_union(parts.first, parts.second);
    CHECK_LIST(back, ref);
  }
}

void test_large_roundtrip() {
  Ref ref;
  std::mt19937_64 wl(61);
  while (ref.size() < 100000) ref.insert(static_cast<std::int64_t>(wl()));
  SkipList s(62);
  for (std::int64_t k : ref) s.insert(k);
  CHECK_LIST(s, ref);
  CHECK(s.level() <= sl::kMaxLevel);
  auto mid = std::next(ref.begin(), ref.size() / 2);
  check_split_roundtrip(s, ref, *mid);
}

// ---------------------------------------------------------------------------
// Randomized mixed operations against std::set.
// ---------------------------------------------------------------------------

std::int64_t random_key(std::mt19937_64& wl) {
  static const std::int64_t boundary[] = {kMin, kMin + 1, -1, 0,
                                          1,    kMax - 1, kMax};
  const unsigned r = wl() % 100;
  if (r < 85)  // small domain: plenty of duplicates and hits
    return static_cast<std::int64_t>(wl() % 2001) - 1000;
  if (r < 93) return boundary[wl() % 7];
  return static_cast<std::int64_t>(wl());  // anywhere in the int64 range
}

void test_random_mixed(std::uint64_t seed, int ops) {
  std::mt19937_64 wl(seed);                    // workload generator
  SkipList s(seed * 0x9e3779b97f4a7c15ULL);    // independent height seed
  Ref ref;
  int splits = 0, unions = 0;

  for (int op = 0; op < ops; ++op) {
    const unsigned kind = wl() % 100;
    if (kind < 40) {  // insert
      const std::int64_t k = random_key(wl);
      CHECK(s.insert(k) == ref.insert(k).second);
    } else if (kind < 65) {  // search
      const std::int64_t k = random_key(wl);
      CHECK(s.search(k) == (ref.count(k) != 0));
    } else if (kind < 75) {  // split + union round trip
      check_split_roundtrip(s, ref, random_key(wl));
      ++splits;
      ++unions;
    } else if (kind < 85) {  // split, modify both parts, union back
      const std::int64_t x = random_key(wl);
      Ref lref, rref;
      split_ref(ref, x, lref, rref);
      auto parts = sl::split(s, x);
      ++splits;
      for (int j = 0; j < 5; ++j) {
        const std::int64_t k = random_key(wl);
        if (k <= x)
          CHECK(parts.first.insert(k) == lref.insert(k).second);
        else
          CHECK(parts.second.insert(k) == rref.insert(k).second);
      }
      CHECK_LIST(parts.first, lref);
      CHECK_LIST(parts.second, rref);
      s = sl::ordered_union(parts.first, parts.second);
      ++unions;
      ref = lref;
      ref.insert(rref.begin(), rref.end());
    } else if (kind < 92) {  // repeated splits into up to 4 pieces, rejoin
      std::vector<std::int64_t> pivots;
      for (int j = 0; j < 3; ++j) pivots.push_back(random_key(wl));
      std::sort(pivots.begin(), pivots.end());
      const auto before = s.nodes();
      std::vector<SkipList> pieces;
      SkipList rest = std::move(s);
      for (std::int64_t x : pivots) {
        auto parts = sl::split(rest, x);
        ++splits;
        pieces.push_back(std::move(parts.first));
        rest = std::move(parts.second);
      }
      pieces.push_back(std::move(rest));
      SkipList acc;
      for (SkipList& p : pieces) {
        CHECK(p.check_invariants());
        acc = sl::ordered_union(acc, p);
        ++unions;
      }
      s = std::move(acc);
      CHECK_LIST(s, ref);
      CHECK(same_nodes(s.nodes(), before));
    } else if (kind < 96) {  // rebuilding baseline round trip
      const std::int64_t x = random_key(wl);
      Ref lref, rref;
      split_ref(ref, x, lref, rref);
      auto parts = sl::split_rebuild(s, x);
      CHECK_LIST(parts.first, lref);
      CHECK_LIST(parts.second, rref);
      CHECK(s.empty());
      s = sl::ordered_union(parts.first, parts.second);
    } else {  // use the consumed input, then discard it via move-assignment
      const std::int64_t x = random_key(wl);
      auto parts = sl::split(s, x);
      ++splits;
      CHECK(s.insert(x) && s.search(x));
      CHECK_LIST(s, Ref{x});
      s = sl::ordered_union(parts.first, parts.second);
      ++unions;
    }
    CHECK_LIST(s, ref);
  }
  std::printf("  seed %llu: %d ops (%d splits, %d unions), final size %zu\n",
              static_cast<unsigned long long>(seed), ops, splits, unions,
              ref.size());
}

}  // namespace

int main() {
  test_empty_and_singleton();
  test_duplicates();
  test_boundary_keys();
  test_split_positions();
  test_union_cases();
  test_independence_and_reuse();
  test_split_rebuild_contract();
  test_large_roundtrip();
  std::printf("randomized mixed operations:\n");
  for (std::uint64_t seed : {11ULL, 22ULL, 33ULL}) test_random_mixed(seed, 5000);

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
