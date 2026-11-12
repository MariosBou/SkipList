// Randomized skip list over distinct int64_t keys. Single file: tests and
// benchmarks #include it (one translation unit per executable).
//
// Representation: a node is one heap block {key, height, next[height]}; its
// address, key and height never change. The head is an array of kMaxLevel
// pointers inside the list and has no key, so every int64_t is a legal key
// (no sentinel collisions); nullptr ends a level. No size or tail pointers are
// stored, so union and split have no hidden O(n) repair.
//
// Heights: geometric with p = 1/2 from the list's own SplitMix64 generator
// (separate from any workload generator), capped at kMaxLevel = 32. For
// n <= 2^20 the chance that any node is truncated is below 0.03 %; the O(log n)
// bounds hold up to about n = 2^32.
//
// Ownership: a list owns its nodes (movable, not copyable); the destructor
// frees each node once. ordered_union and split consume their inputs, relink
// all nodes into the outputs and leave the inputs empty and reusable. Outputs
// are independent.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sl {

constexpr int kMaxLevel = 32;
constexpr std::uint64_t kDefaultSeed = 0x5eed5eed5eed5eedULL;

// Logical-work counters. Only active when compiled with SKIPLIST_INSTRUMENT
// (separate build); in timing builds SL_COUNT expands to nothing.
//   key_comparisons : comparisons between a stored key and the argument key
//   link_reads      : forward pointers followed while traversing
//   link_writes     : forward pointers (including head pointers) assigned
//   node_allocs     : data nodes allocated
//   node_frees      : data nodes freed
struct WorkCounters {
  std::uint64_t key_comparisons = 0;
  std::uint64_t link_reads = 0;
  std::uint64_t link_writes = 0;
  std::uint64_t node_allocs = 0;
  std::uint64_t node_frees = 0;
};
WorkCounters g_counters;
void reset_counters() { g_counters = WorkCounters{}; }

#ifdef SKIPLIST_INSTRUMENT
#define SL_COUNT(field, n) (::sl::g_counters.field += (n))
#else
#define SL_COUNT(field, n) ((void)0)
#endif

// Data node; the forward-pointer array follows the struct in the same block.
struct Node {
  std::int64_t key;
  int height;  // 1 .. kMaxLevel

  Node** next() { return reinterpret_cast<Node**>(this + 1); }
  Node* const* next() const { return reinterpret_cast<Node* const*>(this + 1); }
};

// Diagnostic view of one node, used by tests to check that structural
// union/split preserve node addresses and heights.
struct NodeInfo {
  const Node* addr;
  std::int64_t key;
  int height;
};

class SkipList {
 public:
  explicit SkipList(std::uint64_t height_seed = kDefaultSeed);
  ~SkipList();

  SkipList(const SkipList&) = delete;
  SkipList& operator=(const SkipList&) = delete;
  SkipList(SkipList&& other) noexcept;
  SkipList& operator=(SkipList&& other) noexcept;

  // search(S, k): true iff k is in the set. Does not modify the list.
  bool search(std::int64_t k) const;

  // insert(S, k): inserts k if absent; returns true iff it was inserted.
  // A duplicate changes neither the set nor any node height.
  bool insert(std::int64_t k);

  // Diagnostics (not kernels).
  bool empty() const { return level_ == 0; }
  std::size_t size() const;                     // O(n) traversal
  std::vector<std::int64_t> to_vector() const;  // ordered traversal
  std::vector<NodeInfo> nodes() const;          // bottom level, in order
  int level() const { return level_; }

  // Invariant checker: increasing keys on every level, upper levels are
  // subsequences of lower ones, valid heights and links, no cycles.
  // On failure returns false and, if err != nullptr, stores a description.
  bool check_invariants(std::string* err = nullptr) const;

  // Untimed restoration helper for benchmarks only; not part of the contract.
  bool erase_for_restore(std::int64_t k);

  // Reseeds the height generator (does not touch existing nodes).
  void reseed(std::uint64_t height_seed) { rng_state_ = height_seed; }

 private:
  friend SkipList ordered_union(SkipList& a, SkipList& b);
  friend std::pair<SkipList, SkipList> split(SkipList& s, std::int64_t x);
  friend std::pair<SkipList, SkipList> split_rebuild(SkipList& s,
                                                     std::int64_t x);

  std::uint64_t next_random();  // SplitMix64 step
  int random_height();
  // Forward-pointer array of a predecessor; nullptr stands for the head.
  Node** links(Node* pred) { return pred ? pred->next() : head_; }
  Node* const* links(const Node* pred) const {
    return pred ? pred->next() : head_;
  }
  void find_preds(std::int64_t k, bool inclusive, Node* before[]);
  static Node* make_node(std::int64_t key, int height);
  static void free_node(Node* n);
  void clear();         
  void reset_empty();   
  void shrink_level();  
  Node* head_[kMaxLevel];
  int level_ = 0;
  std::uint64_t rng_state_;
};

// Structural kernels: relink existing nodes, no allocation, no new heights.
// Both consume their inputs (left empty and reusable).

// Precondition: if both are nonempty, max(A) < min(B). Expected O(log(n+m+2)).
// The result's height generator is seeded from a draw of A's generator.
SkipList ordered_union(SkipList& a, SkipList& b);

// L = {k <= x}, R = {k > x}. Expected O(log(n + 2)). L's and R's generators
// are seeded from draws of S's generator.
std::pair<SkipList, SkipList> split(SkipList& s, std::int64_t x);

// Rebuilding baseline (Section 6): same contract as split, but inserts every
// key into fresh lists (new nodes, new heights) and frees S. Expected O(n log n).
std::pair<SkipList, SkipList> split_rebuild(SkipList& s, std::int64_t x);

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

using namespace std;

namespace {

// Basic steps wrapped so the instrumented build can count them.
inline bool key_less(int64_t a, int64_t b) {
  SL_COUNT(key_comparisons, 1);
  return a < b;
}
inline bool key_eq(int64_t a, int64_t b) {
  SL_COUNT(key_comparisons, 1);
  return a == b;
}
inline Node* read_link(Node* const* links, int i) {
  SL_COUNT(link_reads, 1);
  return links[i];
}
inline void write_link(Node** links, int i, Node* value) {
  SL_COUNT(link_writes, 1);
  links[i] = value;
}

}  // namespace

// ---------------------------------------------------------------------------
// Creating, destroying and moving lists
// ---------------------------------------------------------------------------

SkipList::SkipList(uint64_t height_seed) : rng_state_(height_seed) {
  fill(head_, head_ + kMaxLevel, nullptr);
}

SkipList::~SkipList() { clear(); }

// Move transfers the nodes; the source is left empty and reusable.
SkipList::SkipList(SkipList&& other) noexcept : SkipList() {
  *this = move(other);
}

SkipList& SkipList::operator=(SkipList&& other) noexcept {
  if (this != &other) {
    clear();
    copy(other.head_, other.head_ + kMaxLevel, head_);
    level_ = other.level_;
    rng_state_ = other.rng_state_;
    other.reset_empty();
    other.rng_state_ = other.next_random();
  }
  return *this;
}

// Walks level 0, so every node is freed exactly once.
void SkipList::clear() {
  Node* cur = head_[0];
  while (cur != nullptr) {
    Node* next = cur->next()[0];
    free_node(cur);
    cur = next;
  }
  reset_empty();
}

// Forgets all nodes without freeing them (after they were handed over).
void SkipList::reset_empty() {
  fill(head_, head_ + kMaxLevel, nullptr);
  level_ = 0;
}

void SkipList::shrink_level() {
  while (level_ > 0 && head_[level_ - 1] == nullptr) --level_;
}

// ---------------------------------------------------------------------------
// Random heights and nodes
// ---------------------------------------------------------------------------

// SplitMix64 (Steele, Lea, Flood 2014): 64 random bits, 8 bytes of state.
uint64_t SkipList::next_random() {
  uint64_t z = (rng_state_ += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

// Geometric height, p = 1/2: P(height >= h) = 2^-(h-1), capped at kMaxLevel.
int SkipList::random_height() {
  uint64_t bits = next_random();
  int h = 1;
  while ((bits & 1u) != 0 && h < kMaxLevel) {
    ++h;
    bits >>= 1;
  }
  return h;
}

// One block: Node{key, height} followed by `height` next-pointers (unset).
Node* SkipList::make_node(int64_t key, int height) {
  void* mem = ::operator new(sizeof(Node) + sizeof(Node*) * height);
  Node* n = new (mem) Node{key, height};
  SL_COUNT(node_allocs, 1);
  return n;
}

void SkipList::free_node(Node* n) {
  n->~Node();
  ::operator delete(n);
  SL_COUNT(node_frees, 1);
}

// ---------------------------------------------------------------------------
// Search and insert
// ---------------------------------------------------------------------------

// before[i] = last node on line i with key < k (<= k if inclusive);
// nullptr stands for the head.
void SkipList::find_preds(int64_t k, bool inclusive, Node* before[]) {
  Node* cur = nullptr;
  for (int i = level_ - 1; i >= 0; --i) {
    Node* next = read_link(links(cur), i);
    while (next != nullptr &&
           (inclusive ? !key_less(k, next->key) : key_less(next->key, k))) {
      cur = next;
      next = read_link(next->next(), i);
    }
    before[i] = cur;
  }
}

bool SkipList::search(int64_t k) const {
  const Node* cur = nullptr;
  const Node* next = nullptr;
  for (int i = level_ - 1; i >= 0; --i) {
    next = read_link(links(cur), i);
    while (next != nullptr && key_less(next->key, k)) {
      cur = next;
      next = read_link(next->next(), i);
    }
  }
  return next != nullptr && key_eq(next->key, k);
}

bool SkipList::insert(int64_t k) {
  Node* before[kMaxLevel];
  find_preds(k, false, before);
  if (level_ > 0) {
    Node* found = read_link(links(before[0]), 0);
    if (found != nullptr && key_eq(found->key, k)) return false;  // duplicate
  }
  // Height is drawn only after the key is known to be absent.
  const int h = random_height();
  for (int i = level_; i < h; ++i) before[i] = nullptr;
  if (h > level_) level_ = h;

  Node* n = make_node(k, h);
  for (int i = 0; i < h; ++i) {
    Node** links_before = links(before[i]);
    write_link(n->next(), i, read_link(links_before, i));
    write_link(links_before, i, n);
  }
  return true;
}

// Benchmark restoration helper, not a kernel.
bool SkipList::erase_for_restore(int64_t k) {
  if (level_ == 0) return false;
  Node* before[kMaxLevel];
  find_preds(k, false, before);
  Node* found = links(before[0])[0];
  if (found == nullptr || found->key != k) return false;
  for (int i = 0; i < found->height; ++i)
    links(before[i])[i] = found->next()[i];
  free_node(found);
  shrink_level();
  return true;
}

// ---------------------------------------------------------------------------
// Split and union: only pointers change, no node is created or freed
// ---------------------------------------------------------------------------

// Cuts every line right after before[i] (last node with key <= x): the prefix
// goes to L, the rest to R. One search path, O(log n) expected.
pair<SkipList, SkipList> split(SkipList& s, int64_t x) {
  pair<SkipList, SkipList> out;
  SkipList& left = out.first;
  SkipList& right = out.second;

  Node* before[kMaxLevel];
  s.find_preds(x, true, before);
  for (int i = 0; i < s.level_; ++i) {
    Node* first = read_link(s.head_, i);
    if (before[i] == nullptr) {
      write_link(right.head_, i, first);  // whole line has keys > x
    } else {
      write_link(left.head_, i, first);
      write_link(right.head_, i, read_link(before[i]->next(), i));
      write_link(before[i]->next(), i, nullptr);
    }
  }
  left.level_ = s.level_;
  right.level_ = s.level_;
  left.shrink_level();
  right.shrink_level();

  left.rng_state_ = s.next_random();
  right.rng_state_ = s.next_random();

  s.reset_empty();
  return out;
}

// Precondition (unchecked): every key of A < every key of B.
// Finds A's last node on each line (tail[i]) by walking A's right spine, then
// glues B's line i after it. Expected O(log(n + m + 2)).
SkipList ordered_union(SkipList& a, SkipList& b) {
  assert(&a != &b);
  SkipList out;

  Node* tail[kMaxLevel];
  Node* cur = nullptr;
  for (int i = a.level_ - 1; i >= 0; --i) {
    Node* next = read_link(a.links(cur), i);
    while (next != nullptr) {
      cur = next;
      next = read_link(next->next(), i);
    }
    tail[i] = cur;
  }

  for (int i = 0; i < a.level_; ++i)
    write_link(out.head_, i, read_link(a.head_, i));
  for (int i = 0; i < b.level_; ++i) {
    Node* first_of_b = read_link(b.head_, i);
    if (i < a.level_)
      write_link(tail[i]->next(), i, first_of_b);
    else
      write_link(out.head_, i, first_of_b);  // line only B has
  }
  out.level_ = max(a.level_, b.level_);

  out.rng_state_ = a.next_random();

  a.reset_empty();
  b.reset_empty();
  return out;
}

// ---------------------------------------------------------------------------
// Rebuilding split (baseline): fresh nodes and heights via insert, then free S.
pair<SkipList, SkipList> split_rebuild(SkipList& s, int64_t x) {
  pair<SkipList, SkipList> out;
  out.first.rng_state_ = s.next_random();
  out.second.rng_state_ = s.next_random();

  for (Node* n = read_link(s.head_, 0); n != nullptr;
       n = read_link(n->next(), 0)) {
    if (!key_less(x, n->key))  // n->key <= x
      out.first.insert(n->key);
    else
      out.second.insert(n->key);
  }
  s.clear();
  return out;
}

// ---------------------------------------------------------------------------
// Diagnostics used by the tests
// ---------------------------------------------------------------------------

size_t SkipList::size() const {
  size_t n = 0;
  for (const Node* p = head_[0]; p != nullptr; p = p->next()[0]) ++n;
  return n;
}

vector<int64_t> SkipList::to_vector() const {
  vector<int64_t> v;
  for (const Node* p = head_[0]; p != nullptr; p = p->next()[0])
    v.push_back(p->key);
  return v;
}

vector<NodeInfo> SkipList::nodes() const {
  vector<NodeInfo> v;
  for (const Node* p = head_[0]; p != nullptr; p = p->next()[0])
    v.push_back({p, p->key, p->height});
  return v;
}

bool SkipList::check_invariants(string* err) const {
  auto fail = [err](const string& msg) {
    if (err != nullptr) *err = msg;
    return false;
  };

  if (level_ < 0 || level_ > kMaxLevel) return fail("level_ out of range");
  for (int i = 0; i < kMaxLevel; ++i) {
    if (i < level_ && head_[i] == nullptr)
      return fail("empty level " + to_string(i) + " below level_");
    if (i >= level_ && head_[i] != nullptr)
      return fail("non-null head pointer at level " + to_string(i) +
                  " >= level_");
  }

  // Line 0: increasing keys, valid heights, no repeated node (cycle).
  unordered_map<const Node*, size_t> position;  // index on line 0
  vector<size_t> taller_than(kMaxLevel + 1, 0);
  const Node* prev = nullptr;
  int max_height = 0;
  for (const Node* n = head_[0]; n != nullptr; n = n->next()[0]) {
    if (position.count(n) != 0) return fail("cycle on level 0");
    if (n->height < 1 || n->height > kMaxLevel)
      return fail("node height out of [1, kMaxLevel]");
    if (prev != nullptr && !(prev->key < n->key))
      return fail("keys not strictly increasing on level 0");
    position.emplace(n, position.size());
    for (int i = 0; i < n->height; ++i) ++taller_than[i];
    max_height = max(max_height, n->height);
    prev = n;
  }
  if (max_height != level_) return fail("level_ differs from maximum height");

  // Line i must hold exactly the nodes of height > i, in line-0 order.
  for (int i = 1; i < level_; ++i) {
    size_t seen = 0;
    const Node* p = nullptr;
    size_t prev_pos = 0;
    for (const Node* n = head_[i]; n != nullptr; n = n->next()[i]) {
      auto it = position.find(n);
      if (it == position.end())
        return fail("level " + to_string(i) +
                    " links to a node not on level 0");
      if (n->height <= i)
        return fail("node linked on level " + to_string(i) +
                    " above its height");
      if (p != nullptr && (it->second <= prev_pos || !(p->key < n->key)))
        return fail("level " + to_string(i) +
                    " is not an increasing subsequence (or has a cycle)");
      if (++seen > taller_than[i])
        return fail("too many nodes on level " + to_string(i));
      p = n;
      prev_pos = it->second;
    }
    if (seen != taller_than[i])
      return fail("a node is missing from level " + to_string(i));
  }
  return true;
}

}  // namespace sl
