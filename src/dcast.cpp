// [[Rcpp::plugins(cpp17)]]
// [[Rcpp::plugins(openmp)]]
#include <Rcpp.h>
#include <R_ext/Rallocators.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <immintrin.h>

#ifdef __linux__
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace Rcpp;

// =====================================================================
// 1. jemalloc
// =====================================================================
typedef void* (*jmalloc_t)(size_t);
typedef void  (*jfree_t)(void*);
static jmalloc_t jm_alloc = malloc;
static jfree_t   jm_free  = free;
static void init_jemalloc() {
  static bool done = false;
  if (done) return;
  done = true;
#ifdef __linux__
  void* h = dlopen("libjemalloc.so.2", RTLD_LAZY);
  if (!h) h = dlopen("libjemalloc.so", RTLD_LAZY);
  if (h) {
    void* a = dlsym(h, "malloc");
    void* f = dlsym(h, "free");
    if (a && f) { jm_alloc = (jmalloc_t)a; jm_free = (jfree_t)f; }
  }
#endif
}

// =====================================================================
// 2. cgroup CPU quota (cached)
// =====================================================================
static int __attribute__((unused)) cgroup_cpu_limit() {
  static int cached = -2;
  if (__builtin_expect(cached != -2, 1)) return cached;
  cached = -1;
#ifdef __linux__
  FILE* f = fopen("/sys/fs/cgroup/cpu.max", "r");
  if (f) {
    char q[64] = {0}, p[64] = {0};
    int got = fscanf(f, "%63s %63s", q, p);
    fclose(f);
    if (got == 2 && strcmp(q, "max") != 0) {
      long long qq = atoll(q), pp = atoll(p);
      if (pp > 0) { int l = (int)((qq + pp - 1) / pp); cached = (l > 0 ? l : 1); }
    }
  }
#endif
  return cached;
}

// =====================================================================
// 3. Huge-page allocator
// =====================================================================
static constexpr size_t HUGE_THRESHOLD     = 1ULL << 20;
static constexpr size_t HUGE_ALIGN         = 2ULL << 20;
static constexpr size_t POPULATE_THRESHOLD = 128ULL << 20;
static constexpr size_t HEAD_SIZE          = 64;

static constexpr uint32_t HP_MAGIC       = 0x4D454C54u;
static constexpr uint32_t HP_KIND_MALLOC = 0u;
static constexpr uint32_t HP_KIND_MMAP   = 1u;

struct Header {
  uint32_t magic;
  uint32_t kind;
  size_t   total;
  uint64_t pad[5];
};
static_assert(sizeof(Header) <= HEAD_SIZE, "Header must fit");

static void* huge_alloc(R_allocator_t*, size_t size) {
  size_t alloc_size = size + HEAD_SIZE;
  if (alloc_size < HUGE_THRESHOLD) {
    void* p = jm_alloc(alloc_size);
    if (!p) Rf_error("allocator: OOM (%zu)", size);
    Header* h = (Header*)p;
    h->magic = HP_MAGIC; h->kind = HP_KIND_MALLOC; h->total = size;
    return (char*)p + HEAD_SIZE;
  }
  size_t total = (alloc_size + HUGE_ALIGN - 1) & ~(HUGE_ALIGN - 1);
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  if (total >= POPULATE_THRESHOLD) flags |= MAP_POPULATE;
  void* raw = mmap(nullptr, total, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (raw == MAP_FAILED) {
    void* p = jm_alloc(alloc_size);
    if (!p) Rf_error("allocator: OOM (fallback, %zu)", size);
    Header* h = (Header*)p;
    h->magic = HP_MAGIC; h->kind = HP_KIND_MALLOC; h->total = size;
    return (char*)p + HEAD_SIZE;
  }
#ifdef MADV_HUGEPAGE
  madvise(raw, total, MADV_HUGEPAGE);
#endif
#ifdef MADV_POPULATE_WRITE
  if (total >= POPULATE_THRESHOLD) madvise(raw, total, MADV_POPULATE_WRITE);
#endif
  Header* h = (Header*)raw;
  h->magic = HP_MAGIC; h->kind = HP_KIND_MMAP; h->total = total;
  return (char*)raw + HEAD_SIZE;
}

static void huge_free(R_allocator_t*, void* p) {
  if (!p || p == MAP_FAILED) return;
  Header* h = (Header*)((char*)p - HEAD_SIZE);
  if (h->magic != HP_MAGIC) { jm_free(h); return; }
  if (h->kind == HP_KIND_MALLOC) { jm_free(h); return; }
  munmap(h, h->total);
}

static R_allocator_t huge_allocator = { huge_alloc, huge_free };

// =====================================================================
// v3.13: 强制 64B 对齐的 aligend 分配（针对小 REALSXP 向量）
// =====================================================================
struct AlignedBuf {
  void*  base;
  size_t total;
};

static inline SEXP alloc_aligned_real(R_xlen_t n) {
  // 用 huge_alloc 保证 64B 对齐（HEAD_SIZE = 64）
  size_t bytes = (size_t)n * sizeof(double);
  if (bytes < HUGE_THRESHOLD) return Rf_allocVector(REALSXP, n);
  return Rf_allocVector3(REALSXP, n, &huge_allocator);
}

static inline size_t sizeof_sexpvec(SEXPTYPE t) {
  switch (t) {
  case INTSXP:  return sizeof(int);
  case REALSXP: return sizeof(double);
  case LGLSXP:  return sizeof(int);
  case STRSXP:  return sizeof(SEXP);
  case VECSXP:  return sizeof(SEXP);
  default:      return 0;
  }
}
static inline SEXP alloc_smart(SEXPTYPE type, R_xlen_t n) {
  size_t bytes = (size_t)n * sizeof_sexpvec(type);
  if (bytes < HUGE_THRESHOLD) return Rf_allocVector(type, n);
  return Rf_allocVector3(type, n, &huge_allocator);
}

// =====================================================================
// 4. SIMD fill
// =====================================================================
static void fill_fallback(double* d, double v, size_t n) {
  for (size_t i = 0; i < n; ++i) d[i] = v;
}

#ifdef __AVX512F__
static void fill_avx512(double* __restrict__ d, double v, size_t n) {
  size_t i = 0; __m512d vv = _mm512_set1_pd(v);
  for (; i + 32 <= n; i += 32) {
    _mm512_storeu_pd(d + i, vv); _mm512_storeu_pd(d + i + 8, vv);
    _mm512_storeu_pd(d + i + 16, vv); _mm512_storeu_pd(d + i + 24, vv);
  }
  for (; i + 8 <= n; i += 8) _mm512_storeu_pd(d + i, vv);
  for (; i < n; ++i) d[i] = v;
}
#endif

#ifdef __AVX2__
static void fill_avx2(double* __restrict__ d, double v, size_t n) {
  size_t i = 0; __m256d vv = _mm256_set1_pd(v);
  for (; i + 16 <= n; i += 16) {
    _mm256_storeu_pd(d + i, vv); _mm256_storeu_pd(d + i + 4, vv);
    _mm256_storeu_pd(d + i + 8, vv); _mm256_storeu_pd(d + i + 12, vv);
  }
  for (; i + 4 <= n; i += 4) _mm256_storeu_pd(d + i, vv);
  for (; i < n; ++i) d[i] = v;
}
#endif

static void (*fill_double_ptr)(double*, double, size_t) = fill_fallback;
static void init_cpu_features() {
  static bool done = false;
  if (done) return;
  done = true;
#if defined(__AVX512F__)
  if (__builtin_cpu_supports("avx512f")) { fill_double_ptr = fill_avx512; return; }
#endif
#if defined(__AVX2__)
  if (__builtin_cpu_supports("avx2")) fill_double_ptr = fill_avx2;
#endif
}

// =====================================================================
// 5. 8x8 转置 (AVX-512)
// =====================================================================
#if defined(__AVX512F__)
static inline void transpose8x8_avx512(
    __m512d r0, __m512d r1, __m512d r2, __m512d r3,
    __m512d r4, __m512d r5, __m512d r6, __m512d r7,
    __m512d& c0, __m512d& c1, __m512d& c2, __m512d& c3,
    __m512d& c4, __m512d& c5, __m512d& c6, __m512d& c7) {
  __m512d t0 = _mm512_unpacklo_pd(r0, r1);
  __m512d t1 = _mm512_unpackhi_pd(r0, r1);
  __m512d t2 = _mm512_unpacklo_pd(r2, r3);
  __m512d t3 = _mm512_unpackhi_pd(r2, r3);
  __m512d t4 = _mm512_unpacklo_pd(r4, r5);
  __m512d t5 = _mm512_unpackhi_pd(r4, r5);
  __m512d t6 = _mm512_unpacklo_pd(r6, r7);
  __m512d t7 = _mm512_unpackhi_pd(r6, r7);
  __m512d s0 = _mm512_shuffle_f64x2(t0, t2, 0x88);
  __m512d s1 = _mm512_shuffle_f64x2(t0, t2, 0xDD);
  __m512d s2 = _mm512_shuffle_f64x2(t1, t3, 0x88);
  __m512d s3 = _mm512_shuffle_f64x2(t1, t3, 0xDD);
  __m512d s4 = _mm512_shuffle_f64x2(t4, t6, 0x88);
  __m512d s5 = _mm512_shuffle_f64x2(t4, t6, 0xDD);
  __m512d s6 = _mm512_shuffle_f64x2(t5, t7, 0x88);
  __m512d s7 = _mm512_shuffle_f64x2(t5, t7, 0xDD);
  c0 = _mm512_shuffle_f64x2(s0, s4, 0x88);  // 列 0
  c1 = _mm512_shuffle_f64x2(s2, s6, 0x88);  // 列 1
  c2 = _mm512_shuffle_f64x2(s1, s5, 0x88);  // 列 2
  c3 = _mm512_shuffle_f64x2(s3, s7, 0x88);  // 列 3
  c4 = _mm512_shuffle_f64x2(s0, s4, 0xDD);  // 列 4
  c5 = _mm512_shuffle_f64x2(s2, s6, 0xDD);  // 列 5
  c6 = _mm512_shuffle_f64x2(s1, s5, 0xDD);  // 列 6
  c7 = _mm512_shuffle_f64x2(s3, s7, 0xDD);  // 列 7
}
#endif

// =====================================================================
// 6. fast_itoa
// =====================================================================
static inline int fast_itoa(int v, char* out) {
  if (v == NA_INTEGER) { out[0] = 'N'; out[1] = 'A'; return 2; }
  if (v == 0) { out[0] = '0'; return 1; }
  bool neg = (v < 0);
  unsigned u = neg ? (unsigned)(-(int64_t)v) : (unsigned)v;
  char tmp[12]; int k = 0;
  while (u) { tmp[k++] = char('0' + (u % 10)); u /= 10; }
  int pos = 0;
  if (neg) out[pos++] = '-';
  for (int i = k - 1; i >= 0; --i) out[pos++] = tmp[i];
  return pos;
}

// =====================================================================
// 7. Hash primitives
// =====================================================================
static inline uint64_t mix64(uint64_t x) {
  x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
  x ^= x >> 27; x *= 0x94D049BB133111EBULL;
  x ^= x >> 31;
  return x;
}
static inline uint64_t fmix64(uint64_t x) {
  x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

// =====================================================================
// 8. Column descriptor + KeyFn
// =====================================================================
struct ColDesc {
  const int32_t* raw  = nullptr;
  const int32_t* code = nullptr;
  int32_t  mn = 0;
  uint32_t na_code = 0;
  uint32_t units   = 1;
  bool     has_na  = false;
};

static inline uint32_t code_of(const ColDesc& d, R_xlen_t i) {
  if (__builtin_expect(d.code != nullptr, 1)) return (uint32_t)d.code[i];
  int32_t v = d.raw[i];
  if (__builtin_expect(v == NA_INTEGER, 0)) return d.na_code;
  return (uint32_t)((int64_t)v - (int64_t)d.mn);
}

template <int N>
struct KeyFnN {
  const ColDesc* c; const int* sh;
  inline uint64_t operator()(R_xlen_t i) const {
    uint64_t k = 0;
#pragma GCC unroll 8
    for (int j = 0; j < N; ++j)
      k |= (uint64_t)code_of(c[j], i) << sh[j];
    return k;
  }
};
struct KeyFnDyn {
  const ColDesc* c; const int* sh; int n;
  inline uint64_t operator()(R_xlen_t i) const {
    uint64_t k = 0;
    for (int j = 0; j < n; ++j)
      k |= (uint64_t)code_of(c[j], i) << sh[j];
    return k;
  }
};

// =====================================================================
// 9. Hash tables
// =====================================================================
struct KeyTable64 {
  std::unique_ptr<uint64_t[]> keys;
  std::vector<int32_t> vals;
  size_t mask = 0, count = 0;

  void allocate(size_t cap) {
    size_t c = 1024; while (c < cap) c <<= 1;
    keys.reset(new uint64_t[c]);
    vals.assign(c, -1);
    mask = c - 1; count = 0;
  }
  inline void insert_raw(uint64_t k, int32_t v) {
    size_t idx = (size_t)mix64(k) & mask;
    while (vals[idx] >= 0) idx = (idx + 1) & mask;
    keys[idx] = k; vals[idx] = v;
  }
  void grow() {
    KeyTable64 nh; nh.allocate((mask + 1) << 1);
    for (size_t i = 0; i <= mask; ++i)
      if (vals[i] >= 0) nh.insert_raw(keys[i], vals[i]);
      keys.swap(nh.keys); vals.swap(nh.vals);
      mask = nh.mask; count = nh.count;
  }
  inline int32_t find_or_insert(uint64_t k, int32_t nv) {
    if (__builtin_expect((count + 1) * 2 >= mask + 1, 0)) grow();
    size_t idx = (size_t)mix64(k) & mask;
    while (vals[idx] >= 0) {
      if (keys[idx] == k) return vals[idx];
      idx = (idx + 1) & mask;
    }
    keys[idx] = k; vals[idx] = nv; ++count;
    return nv;
  }
  inline void prefetch(uint64_t k) const {
    size_t idx = (size_t)mix64(k) & mask;
    __builtin_prefetch(&vals[idx], 0, 3);
  }
};

// =====================================================================
// 10. 96-bit fingerprint
// =====================================================================
static inline void hash_codes_96(const int32_t* __restrict__ codes, int n,
                                 uint64_t& h1_out, uint32_t& h2_out) {
  constexpr uint64_t P0 = 0x100000001b3ULL;
  constexpr uint64_t P1 = 0x9E3779B97F4A7C15ULL;
  constexpr uint64_t P2 = 0xBF58476D1CE4E5B9ULL;
  constexpr uint64_t P3 = 0x94D049BB133111EBULL;

  uint64_t a0 = 0xcbf29ce484222325ULL;
  uint64_t a1 = 0x84222325cbf29ce4ULL;
  uint64_t a2 = 0x9E3779B97F4A7C15ULL;
  uint64_t a3 = 0xBF58476D1CE4E5B9ULL;
  int j = 0;
  for (; j + 4 <= n; j += 4) {
    a0 = (a0 ^ (uint64_t)codes[j + 0]) * P0;
    a1 = (a1 ^ (uint64_t)codes[j + 1]) * P1;
    a2 = (a2 ^ (uint64_t)codes[j + 2]) * P2;
    a3 = (a3 ^ (uint64_t)codes[j + 3]) * P3;
  }
  for (; j < n; ++j)
    a0 = (a0 ^ (uint64_t)codes[j]) * P0;
  uint64_t x = fmix64(a0) ^ fmix64(a1) ^ fmix64(a2) ^ fmix64(a3);
  uint64_t y = fmix64(a0 + P1) + fmix64(a1 + P2) +
    fmix64(a2 + P3) + fmix64(a3 + P0);
  h1_out = x ^ (y * 0x9E3779B97F4A7C15ULL);
  h2_out = (uint32_t)(fmix64(x + y) >> 32);
}

struct Slot96 {
  uint64_t h1;
  int32_t  val;
  uint32_t h2;
};
static_assert(sizeof(Slot96) == 16, "Slot96 must be 16 bytes");

struct VerifyTable96 {
  std::unique_ptr<Slot96[]> slots;
  size_t mask = 0, count = 0;

  static inline uint64_t hash_of(uint64_t h1) {
    return mix64(h1 ^ 0x9E3779B97F4A7C15ULL);
  }
  void allocate(size_t cap) {
    size_t c = 1024; while (c < cap) c <<= 1;
    slots.reset(new Slot96[c]);
    for (size_t i = 0; i < c; ++i) slots[i].val = -1;
    mask = c - 1; count = 0;
  }
  void grow() {
    std::vector<Slot96> tmp;
    tmp.reserve(count);
    for (size_t i = 0; i <= mask; ++i)
      if (slots[i].val >= 0) tmp.push_back(slots[i]);
      size_t new_cap = (mask + 1) << 1;
      slots.reset(new Slot96[new_cap]);
      for (size_t i = 0; i < new_cap; ++i) slots[i].val = -1;
      mask = new_cap - 1;
      count = 0;
      for (auto& s : tmp) {
        size_t idx = (size_t)hash_of(s.h1) & mask;
        while (slots[idx].val >= 0) idx = (idx + 1) & mask;
        slots[idx] = s; ++count;
      }
  }
  inline int32_t find_or_insert(uint64_t h1, uint32_t h2, int32_t nv) {
    if (__builtin_expect((count + 1) * 2 >= mask + 1, 0)) grow();
    size_t idx = (size_t)hash_of(h1) & mask;
    while (slots[idx].val >= 0) {
      if (slots[idx].h1 == h1 && slots[idx].h2 == h2) return slots[idx].val;
      idx = (idx + 1) & mask;
    }
    slots[idx].h1 = h1; slots[idx].h2 = h2; slots[idx].val = nv;
    ++count;
    return nv;
  }
  inline void prefetch(uint64_t h1) const {
    size_t idx = (size_t)hash_of(h1) & mask;
    __builtin_prefetch(&slots[idx], 0, 3);
  }
};

// =====================================================================
// 11. Radix sort
// =====================================================================
static void radix_sort_u64_idx(const uint64_t* keys, std::vector<int32_t>& idx,
                               R_xlen_t n) {
  if (n < 2) return;
  std::vector<int32_t> tmp((size_t)n);
  std::vector<int32_t> cnt(1 << 16);
  for (int pass = 0; pass < 4; ++pass) {
    int shift = pass * 16;
    std::fill(cnt.begin(), cnt.end(), 0);
    for (R_xlen_t i = 0; i < n; ++i)
      ++cnt[(keys[idx[(size_t)i]] >> shift) & 0xFFFF];
    int32_t s = 0;
    for (int k = 0; k < (1 << 16); ++k) { int32_t c = cnt[k]; cnt[k] = s; s += c; }
    for (R_xlen_t i = 0; i < n; ++i)
      tmp[(size_t)cnt[(keys[idx[(size_t)i]] >> shift) & 0xFFFF]++] = idx[(size_t)i];
    idx.swap(tmp);
  }
}

// =====================================================================
// 12. Phase 1 builders
// =====================================================================
template <class KF>
static void build_block_phase1(KF kf,
                               const ColDesc* desc, int n_id,
                               R_xlen_t n_blocks, R_xlen_t period,
                               size_t ht_cap,
                               std::vector<int32_t>& first_src,
                               int32_t& n_out)
{
  bool sorted = true;
  if (n_blocks >= 1) {
    uint64_t prev = kf(0);
    for (R_xlen_t b = 1; b < n_blocks; ++b) {
      uint64_t k = kf(b);
      if (k < prev) { sorted = false; break; }
      prev = k;
    }
  }
  if (sorted) {
    int32_t r = 0;
    first_src.push_back(0);
    uint64_t prev = kf(0);
    for (R_xlen_t b = 1; b < n_blocks; ++b) {
      uint64_t k = kf(b);
      if (k != prev) { ++r; first_src.push_back((int32_t)(b * period)); prev = k; }
    }
    n_out = r + 1;
    return;
  }
  {
    std::vector<uint64_t> keys((size_t)n_blocks);
    for (R_xlen_t b = 0; b < n_blocks; ++b) keys[(size_t)b] = kf(b);
    std::vector<int32_t> idx((size_t)n_blocks);
    std::iota(idx.begin(), idx.end(), 0);
    radix_sort_u64_idx(keys.data(), idx, (R_xlen_t)n_blocks);
    bool has_dups = false;
    for (R_xlen_t t = 1; t < n_blocks; ++t) {
      if (keys[(size_t)idx[(size_t)t]] == keys[(size_t)idx[(size_t)(t - 1)]]) {
        has_dups = true; break;
      }
    }
    if (!has_dups) {
      first_src.resize((size_t)n_blocks);
      for (R_xlen_t t = 0; t < n_blocks; ++t)
        first_src[(size_t)t] = (int32_t)((int64_t)idx[(size_t)t] * (int64_t)period);
      n_out = (int32_t)n_blocks;
      return;
    }
  }
  KeyTable64 ht; ht.allocate(ht_cap);
  uint64_t pk = ~0ull; bool hp = false;
  constexpr R_xlen_t PF = 32;
  const bool do_id_pf = (n_id >= 4);
  first_src.clear();
  first_src.reserve((size_t)n_blocks);
  n_out = 0;
  for (R_xlen_t b = 0; b < n_blocks; ++b) {
    if (do_id_pf && b + PF < n_blocks) {
      for (int j = 0; j < n_id; ++j) {
        const ColDesc& d = desc[j];
        if (d.code) __builtin_prefetch(&d.code[b + PF], 0, 1);
        else        __builtin_prefetch(&d.raw[b + PF], 0, 1);
      }
    }
    uint64_t k = kf(b);
    if (hp && k == pk) continue;
    int32_t r = ht.find_or_insert(k, n_out);
    if (r == n_out) { first_src.push_back((int32_t)(b * period)); ++n_out; }
    pk = k; hp = true;
  }
}

template <class KF>
static void build_general_phase1(KF kf,
                                 const ColDesc* desc, int n_id,
                                 R_xlen_t nlong, size_t ht_cap,
                                 std::vector<int32_t>& row_of,
                                 std::vector<int32_t>& first_src,
                                 int32_t& n_out)
{
  KeyTable64 ht; ht.allocate(ht_cap);
  uint64_t pk = ~0ull; bool hp = false;
  constexpr R_xlen_t PF = 32;
  const bool do_id_pf = (n_id >= 4);
  for (R_xlen_t i = 0; i < nlong; ++i) {
    if (do_id_pf && i + PF < nlong) {
      for (int j = 0; j < n_id; ++j) {
        const ColDesc& d = desc[j];
        if (d.code) __builtin_prefetch(&d.code[i + PF], 0, 1);
        else        __builtin_prefetch(&d.raw[i + PF], 0, 1);
      }
    }
    uint64_t k = kf(i);
    if (hp && k == pk) { row_of[(size_t)i] = n_out - 1; continue; }
    int32_t r = ht.find_or_insert(k, n_out);
    if (r == n_out) { first_src.push_back((int32_t)i); ++n_out; }
    row_of[(size_t)i] = r; pk = k; hp = true;
  }
}

// =====================================================================
// 13. Arg resolution
// =====================================================================
static int resolve_col(SEXP data, SEXP spec, int default_idx) {
  if (Rf_isNull(spec)) return default_idx;
  int ncols = Rf_length(data);
  SEXP names = Rf_getAttrib(data, R_NamesSymbol);
  if (TYPEOF(spec) == INTSXP && XLENGTH(spec) >= 1) {
    int idx = INTEGER(spec)[0] - 1;
    if (idx >= 0 && idx < ncols) return idx;
    stop("Column index out of range");
  }
  if (TYPEOF(spec) == STRSXP && XLENGTH(spec) >= 1) {
    const char* target = CHAR(STRING_ELT(spec, 0));
    for (int i = 0; i < ncols; ++i)
      if (strcmp(CHAR(STRING_ELT(names, i)), target) == 0) return i;
      stop("Column '%s' not found", target);
  }
  stop("Invalid column spec");
  return -1;
}

static std::vector<int> resolve_id_cols(SEXP data, SEXP id_spec,
                                        int var_idx, int val_idx) {
  int ncols = Rf_length(data);
  SEXP names = Rf_getAttrib(data, R_NamesSymbol);
  std::vector<int> id_cols;
  if (Rf_isNull(id_spec)) {
    for (int i = 0; i < ncols; ++i)
      if (i != var_idx && i != val_idx) id_cols.push_back(i);
      if (id_cols.empty()) stop("dcast_cpp: no id columns inferred; specify id explicitly");
      return id_cols;
  }
  if (TYPEOF(id_spec) == INTSXP) {
    R_xlen_t len = XLENGTH(id_spec);
    std::vector<char> seen(ncols, 0);
    for (R_xlen_t i = 0; i < len; ++i) {
      int a = INTEGER(id_spec)[i] - 1;
      if (a < 0 || a >= ncols) stop("id index out of range");
      if (!seen[a]) { seen[a] = 1; id_cols.push_back(a); }
    }
    return id_cols;
  }
  if (TYPEOF(id_spec) == STRSXP) {
    R_xlen_t len = XLENGTH(id_spec);
    std::vector<char> seen(ncols, 0);
    for (R_xlen_t i = 0; i < len; ++i) {
      const char* tgt = CHAR(STRING_ELT(id_spec, i));
      bool found = false;
      for (int j = 0; j < ncols; ++j)
        if (strcmp(CHAR(STRING_ELT(names, j)), tgt) == 0) {
          if (!seen[j]) { seen[j] = 1; id_cols.push_back(j); }
          found = true; break;
        }
        if (!found) stop("Column '%s' not found", tgt);
    }
    return id_cols;
  }
  stop("Invalid 'id' argument");
  return id_cols;
}

static void infer_var_val(SEXP data, SEXP& variable, SEXP& value) {
  SEXP names = Rf_getAttrib(data, R_NamesSymbol);
  int ncols = Rf_length(data);
  if (Rf_isNull(variable))
    for (int i = 0; i < ncols; ++i)
      if (strcmp(CHAR(STRING_ELT(names, i)), "variable") == 0) {
        variable = Rf_ScalarInteger(i + 1); break;
      }
      if (Rf_isNull(value))
        for (int i = 0; i < ncols; ++i)
          if (strcmp(CHAR(STRING_ELT(names, i)), "value") == 0) {
            value = Rf_ScalarInteger(i + 1); break;
          }
}

static bool parse_fill(SEXP fill, double& out) {
  if (Rf_isNull(fill)) return false;
  if (TYPEOF(fill) == REALSXP && XLENGTH(fill) >= 1) {
    double v = REAL(fill)[0];
    if (ISNA(v) || ISNAN(v)) return false;
    out = v; return true;
  }
  if (TYPEOF(fill) == INTSXP && XLENGTH(fill) >= 1) {
    int v = INTEGER(fill)[0];
    if (v == NA_INTEGER) return false;
    out = (double)v; return true;
  }
  if (TYPEOF(fill) == LGLSXP && XLENGTH(fill) >= 1) {
    int v = LOGICAL(fill)[0];
    if (v == NA_LOGICAL) return false;
    out = v ? 1.0 : 0.0; return true;
  }
  return false;
}

// =====================================================================
// 14. Main
// =====================================================================
// [[Rcpp::export]]
SEXP dcast_cpp(SEXP data, SEXP id = R_NilValue,
               SEXP variable = R_NilValue, SEXP value = R_NilValue,
               SEXP variable_name = R_NilValue,
               int cores = 0,
               SEXP fill = R_NilValue,
               bool na_rm = false) {
  init_jemalloc();
  init_cpu_features();

  if (TYPEOF(data) != VECSXP) stop("data must be a data.frame");
  int ncols = Rf_length(data);
  if (ncols < 2) stop("data must have at least 2 columns");

  infer_var_val(data, variable, value);
  if (Rf_isNull(variable)) stop("cannot infer 'variable' column; specify explicitly");
  if (Rf_isNull(value))    stop("cannot infer 'value' column; specify explicitly");

  int var_idx = resolve_col(data, variable, ncols - 2);
  int val_idx = resolve_col(data, value,    ncols - 1);
  if (var_idx < 0 || val_idx < 0) stop("invalid variable/value columns");
  if (var_idx == val_idx) stop("variable and value must be distinct columns");

  std::vector<int> id_cols = resolve_id_cols(data, id, var_idx, val_idx);
  int n_id = (int)id_cols.size();
  if (n_id == 0) stop("no id columns");

  SEXP id_names = Rf_getAttrib(data, R_NamesSymbol);
  SEXP var_col  = VECTOR_ELT(data, var_idx);
  SEXP val_col  = VECTOR_ELT(data, val_idx);
  R_xlen_t nlong = XLENGTH(var_col);
  for (int idx : id_cols)
    if (XLENGTH(VECTOR_ELT(data, idx)) != nlong)
      stop("id columns must have the same length as variable/value");
    if (nlong == 0) stop("data has no rows");
    if (nlong > (R_xlen_t)INT32_MAX)
      stop("dcast_cpp: input too large (nlong > INT32_MAX)");

    double fill_val = NA_REAL;
    bool use_fill = parse_fill(fill, fill_val);

    bool use_parallel = false;
    int  actual_threads = 1;
    (void)use_parallel;
    (void)actual_threads;
#ifdef _OPENMP
    int hw = omp_get_max_threads();
    int cg = cgroup_cpu_limit();
    if (cg > 0 && cg < hw) hw = cg;
    int target = 1;
    if (cores > 0) {
      target = std::max(1, std::min(cores, hw));
    } else if (nlong >= (R_xlen_t)200000 && hw > 1) {
      R_xlen_t calc = nlong / 200000;
      target = (int)std::min<R_xlen_t>((R_xlen_t)hw, calc);
      target = std::max(1, target);
    }
    actual_threads = target;
    use_parallel = (actual_threads > 1) && (nlong > 50000);
    if (use_parallel) {
      omp_set_dynamic(0);
      omp_set_num_threads(actual_threads);
    }
#else
    (void)cores;
    (void)nlong;
#endif

    bool var_is_factor = Rf_isFactor(var_col);
    SEXP var_levels = var_is_factor
    ? Rf_getAttrib(var_col, R_LevelsSymbol) : R_NilValue;
    SEXPTYPE var_type = TYPEOF(var_col);
    const int*    var_int = (var_type == INTSXP)  ? INTEGER(var_col) : nullptr;
    const int*    var_lgl = (var_type == LGLSXP)  ? LOGICAL(var_col) : nullptr;
    const double* var_dbl = (var_type == REALSXP) ? REAL(var_col)    : nullptr;
    const int*    var_fac = var_is_factor ? INTEGER(var_col) : nullptr;

    // ---------------- period detection ----------------
    R_xlen_t period = 0;
    {
      auto veq = [&](R_xlen_t a, R_xlen_t b) -> bool {
        if (var_type == STRSXP) return STRING_ELT(var_col, a) == STRING_ELT(var_col, b);
        if (var_type == REALSXP) {
          double x = var_dbl[a], y = var_dbl[b];
          if (ISNAN(x) || ISNAN(y)) return ISNAN(x) && ISNAN(y);
          return x == y;
        }
        if (var_is_factor) return var_fac[a] == var_fac[b];
        if (var_type == INTSXP) return var_int[a] == var_int[b];
        if (var_type == LGLSXP) return var_lgl[a] == var_lgl[b];
        return false;
      };
      R_xlen_t lim = std::min<R_xlen_t>(nlong, (R_xlen_t)1024);
      for (R_xlen_t i = 1; i < lim; ++i)
        if (veq(i, 0)) { period = i; break; }
        if (period > 0 && (nlong % period) == 0) {
          R_xlen_t step = std::max<R_xlen_t>(1, nlong / 4096);
          for (R_xlen_t i = 0; i < nlong; i += step)
            if (!veq(i, i % period)) { period = 0; break; }
        } else { period = 0; }
    }
    bool block_path = (period > 0);

    // ---------------- verify block alignment ----------------
    if (block_path) {
      R_xlen_t n_blocks = nlong / period;
      for (int j = 0; j < n_id && period > 0; ++j) {
        SEXP col = VECTOR_ELT(data, id_cols[j]);
        SEXPTYPE t = TYPEOF(col);
        if (t == INTSXP || t == LGLSXP) {
          const int* v = (t == INTSXP) ? INTEGER(col) : LOGICAL(col);
          for (R_xlen_t b = 0; b < n_blocks && period > 0; ++b) {
            int first = v[b * period];
            if (period > 1 && v[b * period + period - 1] != first) { period = 0; break; }
            if (period > 2 && v[b * period + period / 2] != first) { period = 0; break; }
          }
        } else if (t == STRSXP) {
          const SEXP* s = (const SEXP*)DATAPTR(col);
          for (R_xlen_t b = 0; b < n_blocks && period > 0; ++b) {
            SEXP first = s[b * period];
            if (period > 1 && s[b * period + period - 1] != first) { period = 0; break; }
            if (period > 2 && s[b * period + period / 2] != first) { period = 0; break; }
          }
        } else if (t == REALSXP) {
          const double* s = REAL(col);
          for (R_xlen_t b = 0; b < n_blocks && period > 0; ++b) {
            double first = s[b * period];
            if (period > 1 && s[b * period + period - 1] != first) { period = 0; break; }
            if (period > 2 && s[b * period + period / 2] != first) { period = 0; break; }
          }
        } else { period = 0; }
      }
      if (period > 0) {
        std::unordered_set<std::string> seen;
        char buf[64];
        for (R_xlen_t k = 0; k < period; ++k) {
          std::string key;
          if (var_type == STRSXP) {
            SEXP s = STRING_ELT(var_col, k);
            key = (s == NA_STRING) ? std::string("\x01NA") : std::string(CHAR(s));
          } else if (var_is_factor) {
            int code = var_fac[k];
            if (code == NA_INTEGER) key = "\x01NA";
            else key = std::string(CHAR(STRING_ELT(var_levels, code - 1)));
          } else if (var_type == INTSXP) {
            int len = fast_itoa(var_int[k], buf); key.assign(buf, len);
          } else if (var_type == LGLSXP) {
            int v = var_lgl[k];
            key = (v == NA_LOGICAL) ? "\x01NA" : (v ? "TRUE" : "FALSE");
          } else if (var_type == REALSXP) {
            double x = var_dbl[k];
            if (ISNA(x)) { key = "\x01NA"; }
            else {
              int l = std::snprintf(buf, sizeof(buf), "%.17g", x);
              if (l < 0) l = 0;
              if ((size_t)l >= sizeof(buf)) l = (int)sizeof(buf) - 1;
              key.assign(buf, l);
            }
          }
          if (!seen.insert(key).second) { period = 0; break; }
        }
      }
      block_path = (period > 0);
    }

    // ---------------- Phase -1: perm shortcut ----------------
    bool perm_shortcut = false;
    std::vector<int32_t> perm_first_src;

    if (block_path) {
      R_xlen_t n_blk = nlong / period;
      for (int j = 0; j < n_id && !perm_shortcut; ++j) {
        SEXP col = VECTOR_ELT(data, id_cols[j]);
        SEXPTYPE t = TYPEOF(col);
        if (t != INTSXP && t != LGLSXP) continue;
        const int32_t* v = (t == INTSXP) ? (const int32_t*)INTEGER(col)
          : (const int32_t*)LOGICAL(col);
        int32_t mn = INT32_MAX, mx = INT32_MIN;
        bool has_na = false;
        for (R_xlen_t b = 0; b < n_blk; ++b) {
          int32_t x = v[b * period];
          if (x == NA_INTEGER) { has_na = true; break; }
          if (x < mn) mn = x;
          if (x > mx) mx = x;
        }
        if (has_na) continue;
        int64_t range = (int64_t)mx - (int64_t)mn + 1;
        if (range <= 0 || range > (int64_t)n_blk * 4) continue;
        std::vector<int32_t> lut((size_t)range, -1);
        std::vector<int32_t> codes((size_t)n_blk);
        int32_t next_code = 0;
        bool ok = true;
        for (R_xlen_t b = 0; b < n_blk; ++b) {
          int32_t x = v[b * period] - mn;
          if (x < 0 || x >= range) { ok = false; break; }
          int32_t& slot = lut[(size_t)x];
          if (slot < 0) slot = next_code++;
          codes[(size_t)b] = slot;
        }
        if (!ok || (R_xlen_t)next_code != n_blk) continue;
        perm_first_src.assign((size_t)n_blk, 0);
        for (R_xlen_t b = 0; b < n_blk; ++b)
          perm_first_src[(size_t)codes[(size_t)b]] =
            (int32_t)((int64_t)b * (int64_t)period);
        perm_shortcut = true;
      }
    }

    // ---------------- column descriptors ----------------
    std::vector<ColDesc> desc(n_id);
    std::vector<std::vector<int32_t>> owned(n_id);
    const R_xlen_t id_n    = block_path ? (nlong / period) : nlong;
    const R_xlen_t id_step = block_path ? period : 1;
    auto id_index = [&](R_xlen_t b) -> R_xlen_t { return b * id_step; };

    if (!perm_shortcut) {
      for (int j = 0; j < n_id; ++j) {
        SEXP col = VECTOR_ELT(data, id_cols[j]);
        SEXPTYPE t = TYPEOF(col);
        ColDesc& d = desc[j];
        if (t == INTSXP || t == LGLSXP) {
          const int32_t* v = (t == INTSXP) ? (const int32_t*)INTEGER(col)
            : (const int32_t*)LOGICAL(col);
          int32_t mn = INT32_MAX, mx = INT32_MIN;
          bool has_na = false;
          for (R_xlen_t b = 0; b < id_n; ++b) {
            int32_t x = v[id_index(b)];
            if (x == NA_INTEGER) { has_na = true; continue; }
            if (x < mn) mn = x;
            if (x > mx) mx = x;
          }
          if (mn > mx) { mn = 0; mx = 0; }
          uint64_t range = (uint64_t)((int64_t)mx - (int64_t)mn) + 1;
          uint64_t units = range + (has_na ? 1 : 0);
          if (units <= ((uint64_t)1 << 26) || units <= (uint64_t)id_n * 4) {
            if (block_path) {
              owned[j].resize((size_t)id_n);
              for (R_xlen_t b = 0; b < id_n; ++b) {
                int32_t x = v[id_index(b)];
                int32_t code;
                if (x == NA_INTEGER) code = has_na ? (int32_t)(units - 1) : 0;
                else                 code = (int32_t)((int64_t)x - (int64_t)mn);
                owned[j][b] = code;
              }
              d.code = owned[j].data();
            } else {
              d.raw = v; d.mn = mn; d.has_na = has_na;
              d.na_code = has_na ? (uint32_t)(units - 1) : 0;
            }
            d.units = (uint32_t)units;
          } else {
            owned[j].resize((size_t)id_n);
            std::unordered_map<int32_t, int32_t> m; m.reserve(4096);
            int32_t card = 0;
            for (R_xlen_t b = 0; b < id_n; ++b) {
              int32_t x = v[id_index(b)];
              auto it = m.find(x);
              if (it == m.end()) m.emplace(x, card), owned[j][b] = card++;
              else               owned[j][b] = it->second;
            }
            d.code = owned[j].data(); d.units = (uint32_t)card;
          }
        } else if (t == STRSXP) {
          owned[j].resize((size_t)id_n);
          std::unordered_map<SEXP, int32_t> m; m.reserve(4096);
          int32_t card = 0;
          for (R_xlen_t b = 0; b < id_n; ++b) {
            SEXP s = STRING_ELT(col, id_index(b));
            auto it = m.find(s);
            if (it == m.end()) m.emplace(s, card), owned[j][b] = card++;
            else               owned[j][b] = it->second;
          }
          d.code = owned[j].data(); d.units = (uint32_t)card;
        } else if (t == REALSXP) {
          owned[j].resize((size_t)id_n);
          std::unordered_map<uint64_t, int32_t> m; m.reserve(4096);
          int32_t card = 0;
          for (R_xlen_t b = 0; b < id_n; ++b) {
            double x = REAL(col)[id_index(b)];
            uint64_t bits; std::memcpy(&bits, &x, sizeof(bits));
            if (x == 0.0) bits = 0;
            auto it = m.find(bits);
            if (it == m.end()) m.emplace(bits, card), owned[j][b] = card++;
            else               owned[j][b] = it->second;
          }
          d.code = owned[j].data(); d.units = (uint32_t)card;
        } else { stop("unsupported id column type"); }
      }
    }

    // ---------------- column keys ----------------
    std::vector<std::string> col_keys;
    col_keys.reserve(256);
    std::vector<int32_t> col_of;
    int32_t k_out = 0;

    if (block_path) {
      char buf[64];
      for (R_xlen_t k = 0; k < period; ++k) {
        if (var_is_factor) {
          int code = var_fac[k];
          col_keys.emplace_back(code == NA_INTEGER
                                  ? "NA" : CHAR(STRING_ELT(var_levels, code - 1)));
        } else if (var_type == INTSXP) {
          char b[16]; int l = fast_itoa(var_int[k], b);
          col_keys.emplace_back(b, l);
        } else if (var_type == LGLSXP) {
          int v = var_lgl[k];
          col_keys.emplace_back(v == NA_LOGICAL ? "NA" : (v ? "TRUE" : "FALSE"));
        } else if (var_type == REALSXP) {
          double x = var_dbl[k];
          int l;
          if (ISNA(x)) { buf[0]='N'; buf[1]='A'; l = 2; }
          else {
            l = std::snprintf(buf, sizeof(buf), "%.17g", x);
            if (l < 0) l = 0;
            if ((size_t)l >= sizeof(buf)) l = (int)sizeof(buf) - 1;
          }
          col_keys.emplace_back(buf, l);
        } else {
          SEXP s = STRING_ELT(var_col, k);
          col_keys.emplace_back(s == NA_STRING ? "NA" : CHAR(s));
        }
      }
      k_out = (int32_t)period;
    } else {
      col_of.assign((size_t)nlong, -1);
      if (var_is_factor && Rf_length(var_levels) < 100000) {
        int L = Rf_length(var_levels);
        std::vector<int32_t> c2c((size_t)L + 1, -1);
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int code = var_fac[i];
          size_t slot = (code == NA_INTEGER) ? 0 : (size_t)code;
          int32_t c = c2c[slot];
          if (c < 0) {
            c = k_out++; c2c[slot] = c;
            col_keys.emplace_back(code == NA_INTEGER
                                    ? "NA" : CHAR(STRING_ELT(var_levels, code - 1)));
          }
          col_of[i] = c;
        }
      } else if (var_type == STRSXP) {
        std::unordered_map<SEXP, int32_t> m; m.reserve(256);
        for (R_xlen_t i = 0; i < nlong; ++i) {
          SEXP s = STRING_ELT(var_col, i);
          auto it = m.find(s);
          if (it == m.end()) {
            int32_t c = k_out++; m.emplace(s, c);
            col_keys.emplace_back(s == NA_STRING ? "NA" : CHAR(s));
            col_of[i] = c;
          } else col_of[i] = it->second;
        }
      } else if (var_type == INTSXP) {
        int32_t mn = INT32_MAX, mx = INT32_MIN;
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int32_t v = var_int[i];
          if (v == NA_INTEGER) continue;
          if (v < mn) mn = v;
          if (v > mx) mx = v;
        }
        if (mx >= mn && (int64_t)mx - (int64_t)mn < 1000000) {
          std::vector<int32_t> lut((size_t)((int64_t)mx - (int64_t)mn) + 1, -1);
          int32_t na_col = -1;
          for (R_xlen_t i = 0; i < nlong; ++i) {
            int32_t v = var_int[i];
            if (v == NA_INTEGER) {
              if (na_col < 0) { na_col = k_out++; col_keys.emplace_back("NA"); }
              col_of[i] = na_col;
            } else {
              int32_t& s = lut[(size_t)((int64_t)v - (int64_t)mn)];
              if (s < 0) {
                s = k_out++;
                char b[16]; int l = fast_itoa(v, b);
                col_keys.emplace_back(b, l);
              }
              col_of[i] = s;
            }
          }
        } else {
          std::unordered_map<int32_t, int32_t> m; m.reserve(256);
          for (R_xlen_t i = 0; i < nlong; ++i) {
            int32_t v = var_int[i];
            auto it = m.find(v);
            if (it == m.end()) {
              int32_t c = k_out++; m.emplace(v, c);
              char b[16]; int l = fast_itoa(v, b);
              col_keys.emplace_back(b, l); col_of[i] = c;
            } else col_of[i] = it->second;
          }
        }
      } else if (var_type == REALSXP) {
        std::unordered_map<uint64_t, int32_t> m; m.reserve(256);
        char buf[64];
        for (R_xlen_t i = 0; i < nlong; ++i) {
          double x = var_dbl[i];
          uint64_t bits; std::memcpy(&bits, &x, sizeof(bits));
          if (x == 0.0) bits = 0;
          auto it = m.find(bits);
          if (it == m.end()) {
            int32_t c = k_out++; m.emplace(bits, c);
            int l;
            if (ISNA(x)) { buf[0]='N'; buf[1]='A'; l = 2; }
            else {
              l = std::snprintf(buf, sizeof(buf), "%.17g", x);
              if (l < 0) l = 0;
              if ((size_t)l >= sizeof(buf)) l = (int)sizeof(buf) - 1;
            }
            col_keys.emplace_back(buf, l); col_of[i] = c;
          } else col_of[i] = it->second;
        }
      } else if (var_type == LGLSXP) {
        int32_t F = -1, T = -1, N = -1;
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int v = var_lgl[i];
          if (v == NA_LOGICAL) {
            if (N < 0) { N = k_out++; col_keys.emplace_back("NA"); }
            col_of[i] = N;
          } else if (v) {
            if (T < 0) { T = k_out++; col_keys.emplace_back("TRUE"); }
            col_of[i] = T;
          } else {
            if (F < 0) { F = k_out++; col_keys.emplace_back("FALSE"); }
            col_of[i] = F;
          }
        }
      } else { stop("unsupported variable column type"); }
    }
    if (k_out == 0) stop("empty output");

    // =====================================================================
    // Phase 1
    // =====================================================================
    std::vector<int32_t> row_of;
    std::vector<int32_t> first_src;
    int32_t n_out = 0;

    R_xlen_t n_items = block_path ? (nlong / period) : nlong;
    const size_t ht_cap = (size_t)std::min<uint64_t>(
      ((uint64_t)1 << 25),
                      std::max<uint64_t>(4096, 2 * (uint64_t)std::min<R_xlen_t>(n_items, (R_xlen_t)8e6)));

    if (block_path) {
      R_xlen_t n_blocks = n_items;
      first_src.reserve((size_t)n_blocks);
      bool done = false;
      if (perm_shortcut) {
        first_src = std::move(perm_first_src);
        n_out = (int32_t)n_blocks;
        done = true;
      }
      bool bits_overflow = false;
      std::vector<int> shifts(n_id);
      {
        int s = 0;
        for (int j = n_id - 1; j >= 0; --j) {
          uint32_t u = desc[j].units;
          int b = (u <= 1) ? 1 : (int)(64 - __builtin_clzll((uint64_t)(u - 1)));
          if (b < 1) b = 1;
          shifts[j] = s; s += b;
          if (s > 64) { bits_overflow = true; break; }
        }
        if (s > 64) bits_overflow = true;
      }
      if (!done && !bits_overflow) {
        switch (n_id) {
        case 1: build_block_phase1(KeyFnN<1>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        case 2: build_block_phase1(KeyFnN<2>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        case 3: build_block_phase1(KeyFnN<3>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        case 4: build_block_phase1(KeyFnN<4>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        case 5: build_block_phase1(KeyFnN<5>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        case 6: build_block_phase1(KeyFnN<6>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        case 7: build_block_phase1(KeyFnN<7>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        case 8: build_block_phase1(KeyFnN<8>{desc.data(), shifts.data()}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        default: build_block_phase1(KeyFnDyn{desc.data(), shifts.data(), n_id}, desc.data(), n_id, n_blocks, period, ht_cap, first_src, n_out); break;
        }
        done = true;
      }
      if (!done) {
        constexpr int BATCH = 64;
        std::unique_ptr<uint64_t[]> hashes_h1(new uint64_t[(size_t)n_blocks]);
        std::unique_ptr<uint32_t[]> hashes_h2(new uint32_t[(size_t)n_blocks]);
        std::vector<int32_t> code_buf((size_t)BATCH * (size_t)n_id);
        for (R_xlen_t base = 0; base < n_blocks; base += BATCH) {
          int tb = (int)std::min<R_xlen_t>((R_xlen_t)BATCH, n_blocks - base);
          for (int c = 0; c < n_id; ++c) {
            const ColDesc& d = desc[c];
            int32_t* dst = code_buf.data() + c;
            if (d.code) {
              const int32_t* src = d.code + base;
              for (int r = 0; r < tb; ++r) dst[(size_t)r * n_id] = src[r];
            } else {
              const int32_t* src = d.raw + base;
              const int32_t mn = d.mn;
              const uint32_t na_code = d.na_code;
              for (int r = 0; r < tb; ++r) {
                int32_t v = src[r];
                dst[(size_t)r * n_id] = (v == NA_INTEGER)
                  ? (int32_t)na_code : (int32_t)((int64_t)v - (int64_t)mn);
              }
            }
          }
          for (int r = 0; r < tb; ++r) {
            uint64_t h1; uint32_t h2;
            hash_codes_96(code_buf.data() + (size_t)r * n_id, n_id, h1, h2);
            hashes_h1[(size_t)(base + r)] = h1;
            hashes_h2[(size_t)(base + r)] = h2;
          }
        }
        VerifyTable96 ht; ht.allocate(ht_cap);
        uint64_t pk_h1 = ~0ull; uint32_t pk_h2 = 0; bool hp = false;
        constexpr R_xlen_t PREFETCH_AHEAD = 32;
        for (R_xlen_t b = 0; b < n_blocks; ++b) {
          if (b + PREFETCH_AHEAD < n_blocks)
            ht.prefetch(hashes_h1[(size_t)(b + PREFETCH_AHEAD)]);
          uint64_t h1 = hashes_h1[(size_t)b];
          uint32_t h2 = hashes_h2[(size_t)b];
          if (hp && h1 == pk_h1 && h2 == pk_h2) continue;
          int32_t r = ht.find_or_insert(h1, h2, n_out);
          if (r == n_out) { first_src.push_back((int32_t)(b * period)); ++n_out; }
          pk_h1 = h1; pk_h2 = h2; hp = true;
        }
      }
    } else {
      row_of.assign((size_t)nlong, -1);
      first_src.reserve((size_t)std::min<R_xlen_t>(nlong, (R_xlen_t)1 << 22));
      bool bits_overflow = false;
      std::vector<int> shifts(n_id);
      {
        int s = 0;
        for (int j = n_id - 1; j >= 0; --j) {
          uint32_t u = desc[j].units;
          int b = (u <= 1) ? 1 : (int)(64 - __builtin_clzll((uint64_t)(u - 1)));
          if (b < 1) b = 1;
          shifts[j] = s; s += b;
          if (s > 64) { bits_overflow = true; break; }
        }
        if (s > 64) bits_overflow = true;
      }
      if (!bits_overflow) {
        switch (n_id) {
        case 1: build_general_phase1(KeyFnN<1>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        case 2: build_general_phase1(KeyFnN<2>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        case 3: build_general_phase1(KeyFnN<3>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        case 4: build_general_phase1(KeyFnN<4>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        case 5: build_general_phase1(KeyFnN<5>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        case 6: build_general_phase1(KeyFnN<6>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        case 7: build_general_phase1(KeyFnN<7>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        case 8: build_general_phase1(KeyFnN<8>{desc.data(), shifts.data()}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        default: build_general_phase1(KeyFnDyn{desc.data(), shifts.data(), n_id}, desc.data(), n_id, nlong, ht_cap, row_of, first_src, n_out); break;
        }
      } else {
        constexpr int BATCH = 64;
        std::unique_ptr<uint64_t[]> hashes_h1(new uint64_t[(size_t)nlong]);
        std::unique_ptr<uint32_t[]> hashes_h2(new uint32_t[(size_t)nlong]);
        std::vector<int32_t> code_buf((size_t)BATCH * (size_t)n_id);
        for (R_xlen_t base = 0; base < nlong; base += BATCH) {
          int tb = (int)std::min<R_xlen_t>((R_xlen_t)BATCH, nlong - base);
          for (int c = 0; c < n_id; ++c) {
            const ColDesc& d = desc[c];
            int32_t* dst = code_buf.data() + c;
            if (d.code) {
              const int32_t* src = d.code + base;
              for (int r = 0; r < tb; ++r) dst[(size_t)r * n_id] = src[r];
            } else {
              const int32_t* src = d.raw + base;
              const int32_t mn = d.mn;
              const uint32_t na_code = d.na_code;
              for (int r = 0; r < tb; ++r) {
                int32_t v = src[r];
                dst[(size_t)r * n_id] = (v == NA_INTEGER)
                  ? (int32_t)na_code : (int32_t)((int64_t)v - (int64_t)mn);
              }
            }
          }
          for (int r = 0; r < tb; ++r) {
            uint64_t h1; uint32_t h2;
            hash_codes_96(code_buf.data() + (size_t)r * n_id, n_id, h1, h2);
            hashes_h1[(size_t)(base + r)] = h1;
            hashes_h2[(size_t)(base + r)] = h2;
          }
        }
        VerifyTable96 ht; ht.allocate(ht_cap);
        uint64_t pk_h1 = ~0ull; uint32_t pk_h2 = 0; bool hp = false;
        constexpr R_xlen_t PREFETCH_AHEAD = 32;
        for (R_xlen_t i = 0; i < nlong; ++i) {
          if (i + PREFETCH_AHEAD < nlong)
            ht.prefetch(hashes_h1[(size_t)(i + PREFETCH_AHEAD)]);
          uint64_t h1 = hashes_h1[(size_t)i];
          uint32_t h2 = hashes_h2[(size_t)i];
          if (hp && h1 == pk_h1 && h2 == pk_h2) { row_of[(size_t)i] = n_out - 1; continue; }
          int32_t r = ht.find_or_insert(h1, h2, n_out);
          if (r == n_out) { first_src.push_back((int32_t)i); ++n_out; }
          row_of[(size_t)i] = r; pk_h1 = h1; pk_h2 = h2; hp = true;
        }
      }
    }
    if (n_out == 0) stop("empty output");

    // =====================================================================
    // Phase 2: ID column output
    // =====================================================================
    int total_out_cols = n_id + k_out;
    SEXP out       = PROTECT(Rf_allocVector(VECSXP, total_out_cols));
    SEXP out_names = PROTECT(Rf_allocVector(STRSXP, total_out_cols));

    const int32_t* fs = first_src.data();
    constexpr int32_t PF2 = 16;
    constexpr int PFJ = 4;

    bool can_merge = (n_id >= 4);
    SEXPTYPE id_t0 = (SEXPTYPE)TYPEOF(VECTOR_ELT(data, id_cols[0]));
    if (id_t0 == VECSXP) can_merge = false;
    if (can_merge) {
      for (int j = 1; j < n_id; ++j) {
        if ((SEXPTYPE)TYPEOF(VECTOR_ELT(data, id_cols[j])) != id_t0) { can_merge = false; break; }
      }
    }

    const bool use_id_pf = (n_out <= (int32_t)4e6);

    if (can_merge) {
      std::vector<SEXP> dsts(n_id);
      std::vector<const void*> srcs(n_id);
      std::vector<void*> dptrs(n_id);
      for (int j = 0; j < n_id; ++j) {
        SEXP src = VECTOR_ELT(data, id_cols[j]);
        SEXP dst = PROTECT(alloc_smart(id_t0, n_out));
        Rf_copyMostAttrib(src, dst);
        dsts[j] = dst;
        srcs[j] = DATAPTR(src);
        dptrs[j] = DATAPTR(dst);
        SET_VECTOR_ELT(out, j, dst);
        SET_STRING_ELT(out_names, j, STRING_ELT(id_names, id_cols[j]));
      }

      if (id_t0 == INTSXP || id_t0 == LGLSXP) {
        std::vector<const int*> s(n_id);
        std::vector<int*> d(n_id);
        for (int j = 0; j < n_id; ++j) { s[j] = (const int*)srcs[j]; d[j] = (int*)dptrs[j]; }
#if defined(__AVX512F__)
        // ============================================================
        // v3.13: 修复双路 gather 的 j 未重置 bug
        // ============================================================
        int32_t i = 0;
        for (; i + 16 <= n_out; i += 16) {
          __m512i idx = _mm512_loadu_si512((const void*)&fs[i]);
          int j = 0;
          for (; j + 1 < n_id; j += 2) {
            __m512i g1 = _mm512_i32gather_epi32(idx, (const void*)s[j+0], 4);
            __m512i g2 = _mm512_i32gather_epi32(idx, (const void*)s[j+1], 4);
            _mm512_storeu_si512((void*)&d[j+0][i], g1);
            _mm512_storeu_si512((void*)&d[j+1][i], g2);
          }
          if (j < n_id) {
            __m512i g = _mm512_i32gather_epi32(idx, (const void*)s[j], 4);
            _mm512_storeu_si512((void*)&d[j][i], g);
          }
        }
        for (; i < n_out; ++i) {
          R_xlen_t io = (R_xlen_t)fs[i];
          for (int jj = 0; jj < n_id; ++jj) d[jj][i] = s[jj][io];
        }
#else
        if (use_id_pf) {
          for (int32_t i = 0; i < n_out; ++i) {
            if (i + PF2 < n_out) {
              R_xlen_t nxt = (R_xlen_t)fs[i + PF2];
              for (int j = 0; j < n_id; j += PFJ) __builtin_prefetch(&s[j][nxt], 0, 1);
            }
            R_xlen_t io = (R_xlen_t)fs[i];
#pragma GCC unroll 4
            for (int j = 0; j < n_id; ++j) d[j][i] = s[j][io];
          }
        } else {
          for (int32_t i = 0; i < n_out; ++i) {
            R_xlen_t io = (R_xlen_t)fs[i];
#pragma GCC unroll 4
            for (int j = 0; j < n_id; ++j) d[j][i] = s[j][io];
          }
        }
#endif
      } else if (id_t0 == REALSXP) {
        std::vector<const double*> s(n_id);
        std::vector<double*> d(n_id);
        for (int j = 0; j < n_id; ++j) { s[j] = (const double*)srcs[j]; d[j] = (double*)dptrs[j]; }
#if defined(__AVX512F__)
        int32_t i = 0;
        for (; i + 8 <= n_out; i += 8) {
          __m256i idx = _mm256_loadu_si256((const __m256i*)&fs[i]);
          int j = 0;
          for (; j + 1 < n_id; j += 2) {
            __m512d g1 = _mm512_i32gather_pd(idx, (const void*)s[j+0], 8);
            __m512d g2 = _mm512_i32gather_pd(idx, (const void*)s[j+1], 8);
            _mm512_storeu_pd(&d[j+0][i], g1);
            _mm512_storeu_pd(&d[j+1][i], g2);
          }
          if (j < n_id) {
            __m512d g = _mm512_i32gather_pd(idx, (const void*)s[j], 8);
            _mm512_storeu_pd(&d[j][i], g);
          }
        }
        for (; i < n_out; ++i) {
          R_xlen_t io = (R_xlen_t)fs[i];
          for (int jj = 0; jj < n_id; ++jj) d[jj][i] = s[jj][io];
        }
#else
        if (use_id_pf) {
          for (int32_t i = 0; i < n_out; ++i) {
            if (i + PF2 < n_out) {
              R_xlen_t nxt = (R_xlen_t)fs[i + PF2];
              for (int j = 0; j < n_id; j += PFJ) __builtin_prefetch(&s[j][nxt], 0, 1);
            }
            R_xlen_t io = (R_xlen_t)fs[i];
#pragma GCC unroll 4
            for (int j = 0; j < n_id; ++j) d[j][i] = s[j][io];
          }
        } else {
          for (int32_t i = 0; i < n_out; ++i) {
            R_xlen_t io = (R_xlen_t)fs[i];
#pragma GCC unroll 4
            for (int j = 0; j < n_id; ++j) d[j][i] = s[j][io];
          }
        }
#endif
      } else if (id_t0 == STRSXP) {
        std::vector<const SEXP*> s(n_id);
        std::vector<SEXP*> d(n_id);
        for (int j = 0; j < n_id; ++j) { s[j] = (const SEXP*)srcs[j]; d[j] = (SEXP*)dptrs[j]; }
        if (use_id_pf) {
          for (int32_t i = 0; i < n_out; ++i) {
            if (i + PF2 < n_out) {
              R_xlen_t nxt = (R_xlen_t)fs[i + PF2];
              for (int j = 0; j < n_id; j += PFJ) __builtin_prefetch(&s[j][nxt], 0, 1);
            }
            R_xlen_t io = (R_xlen_t)fs[i];
#pragma GCC unroll 4
            for (int j = 0; j < n_id; ++j) d[j][i] = s[j][io];
          }
        } else {
          for (int32_t i = 0; i < n_out; ++i) {
            R_xlen_t io = (R_xlen_t)fs[i];
#pragma GCC unroll 4
            for (int j = 0; j < n_id; ++j) d[j][i] = s[j][io];
          }
        }
      }
      UNPROTECT(n_id);
    } else {
      for (int j = 0; j < n_id; ++j) {
        int src_idx = id_cols[j];
        SEXP src = VECTOR_ELT(data, src_idx);
        SEXPTYPE t = TYPEOF(src);
        SEXP dst = PROTECT(alloc_smart(t, n_out));
        Rf_copyMostAttrib(src, dst);
        switch (t) {
        case INTSXP: {
          int* d = INTEGER(dst); const int* s = INTEGER(src);
          if (use_id_pf) {
            for (int32_t i = 0; i < n_out; ++i) {
              if (i + PF2 < n_out) __builtin_prefetch(&s[(R_xlen_t)fs[i + PF2]], 0, 1);
              d[i] = s[fs[i]];
            }
          } else {
            for (int32_t i = 0; i < n_out; ++i) d[i] = s[fs[i]];
          }
          break;
        }
        case LGLSXP: {
          int* d = LOGICAL(dst); const int* s = LOGICAL(src);
          if (use_id_pf) {
            for (int32_t i = 0; i < n_out; ++i) {
              if (i + PF2 < n_out) __builtin_prefetch(&s[(R_xlen_t)fs[i + PF2]], 0, 1);
              d[i] = s[fs[i]];
            }
          } else {
            for (int32_t i = 0; i < n_out; ++i) d[i] = s[fs[i]];
          }
          break;
        }
        case REALSXP: {
          double* d = REAL(dst); const double* s = REAL(src);
          if (use_id_pf) {
            for (int32_t i = 0; i < n_out; ++i) {
              if (i + PF2 < n_out) __builtin_prefetch(&s[(R_xlen_t)fs[i + PF2]], 0, 1);
              d[i] = s[fs[i]];
            }
          } else {
            for (int32_t i = 0; i < n_out; ++i) d[i] = s[fs[i]];
          }
          break;
        }
        case STRSXP: {
          const SEXP* s = (const SEXP*)DATAPTR(src);
          SEXP* d = (SEXP*)DATAPTR(dst);
          if (use_id_pf) {
            for (int32_t i = 0; i < n_out; ++i) {
              if (i + PF2 < n_out) __builtin_prefetch(&s[(R_xlen_t)fs[i + PF2]], 0, 1);
              d[i] = s[fs[i]];
            }
          } else {
            for (int32_t i = 0; i < n_out; ++i) d[i] = s[fs[i]];
          }
          break;
        }
        case VECSXP: {
          const SEXP* s = (const SEXP*)DATAPTR(src);
          SEXP* d = (SEXP*)DATAPTR(dst);
          for (int32_t i = 0; i < n_out; ++i) d[i] = s[fs[i]];
          break;
        }
        default: stop("unsupported id column type");
        }
        SET_VECTOR_ELT(out, j, dst);
        SET_STRING_ELT(out_names, j, STRING_ELT(id_names, src_idx));
        UNPROTECT(1);
      }
    }

    SEXPTYPE val_type = TYPEOF(val_col);
    if (val_type != REALSXP && val_type != INTSXP && val_type != LGLSXP)
      stop("dcast_cpp only supports numeric/logical value columns");

    const bool dense = ((R_xlen_t)n_out * (R_xlen_t)k_out == nlong);

    // =====================================================================
    // Phase 3
    // =====================================================================
    std::vector<double*> out_val((size_t)k_out);
    {
      for (int k = 0; k < k_out; ++k) {
        SEXP vc = PROTECT(alloc_smart(REALSXP, n_out));
        out_val[(size_t)k] = REAL(vc);
        SET_VECTOR_ELT(out, n_id + k, vc);
        SET_STRING_ELT(out_names, n_id + k, Rf_mkChar(col_keys[(size_t)k].c_str()));
        UNPROTECT(1);
      }
    }

    const double* vs = (val_type == REALSXP) ? REAL(val_col) : nullptr;
    const int*    vi = (val_type == INTSXP)  ? INTEGER(val_col) : nullptr;
    const int*    vl = (val_type == LGLSXP)  ? LOGICAL(val_col) : nullptr;

#if defined(__AVX512F__)
    bool nt_store_ok = false;
    if (n_out * 8 >= (R_xlen_t)(64 * 1024 * 1024) && k_out >= 1) {
      nt_store_ok = (((uintptr_t)out_val[0] & 63) == 0);
    }
#endif

    // =====================================================================
    // Phase 4: 16 行 tile + 无补零 (mask store 处理尾部)
    // =====================================================================
    if (block_path) {
      R_xlen_t n_blocks = (R_xlen_t)n_out;
      const int kk = (int)k_out;
      const int period_i = (int)period;
      const bool merged_direct = dense && !na_rm && !use_fill;

      if (merged_direct && val_type == REALSXP) {
#if defined(__AVX512F__)
        if (period_i >= 8 && period_i <= 128) {
          const int period_pad = ((period_i + 7) / 8) * 8;
          constexpr int TR = 16;
          const R_xlen_t bs_limit16 = (n_blocks >= TR) ? (n_blocks - TR) : (R_xlen_t)-1;

#ifdef _OPENMP
#pragma omp parallel num_threads(actual_threads) if(use_parallel)
#endif
{
  alignas(64) double tile16[TR * 128];
#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
  for (R_xlen_t bs = 0; bs <= bs_limit16; bs += TR) {
    for (int t = 0; t < TR; ++t) {
      R_xlen_t io = (R_xlen_t)first_src[(size_t)(bs + t)];
      const char* p = (const char*)(vs + io);
      _mm_prefetch(p, _MM_HINT_T0);
      if (period_i * 8 > 64)  _mm_prefetch(p + 64,  _MM_HINT_T0);
      if (period_i * 8 > 128) _mm_prefetch(p + 128, _MM_HINT_T0);
      if (period_i * 8 > 192) _mm_prefetch(p + 192, _MM_HINT_T0);
    }
    {
      constexpr R_xlen_t AHEAD = 4;
      R_xlen_t pf_base = bs + AHEAD * TR;
      if (pf_base + TR - 1 <= bs_limit16) {
        for (int t = 0; t < TR; ++t) {
          R_xlen_t io = (R_xlen_t)first_src[(size_t)(pf_base + t)];
          _mm_prefetch((const char*)(vs + io), _MM_HINT_T1);
        }
      }
    }
    for (int t = 0; t < TR; ++t) {
      R_xlen_t io = (R_xlen_t)first_src[(size_t)(bs + t)];
      std::memcpy(tile16 + (size_t)t * period_pad, vs + io,
                  (size_t)period_i * sizeof(double));
      for (int kp = period_i; kp < period_pad; ++kp)
        tile16[(size_t)t * period_pad + kp] = 0.0;
    }
    for (int k = 0; k < kk; k += 8) {
      __m512d r0a = _mm512_loadu_pd(tile16 + 0*period_pad + k);
      __m512d r1a = _mm512_loadu_pd(tile16 + 1*period_pad + k);
      __m512d r2a = _mm512_loadu_pd(tile16 + 2*period_pad + k);
      __m512d r3a = _mm512_loadu_pd(tile16 + 3*period_pad + k);
      __m512d r4a = _mm512_loadu_pd(tile16 + 4*period_pad + k);
      __m512d r5a = _mm512_loadu_pd(tile16 + 5*period_pad + k);
      __m512d r6a = _mm512_loadu_pd(tile16 + 6*period_pad + k);
      __m512d r7a = _mm512_loadu_pd(tile16 + 7*period_pad + k);

      __m512d r0b = _mm512_loadu_pd(tile16 + 8*period_pad + k);
      __m512d r1b = _mm512_loadu_pd(tile16 + 9*period_pad + k);
      __m512d r2b = _mm512_loadu_pd(tile16 + 10*period_pad + k);
      __m512d r3b = _mm512_loadu_pd(tile16 + 11*period_pad + k);
      __m512d r4b = _mm512_loadu_pd(tile16 + 12*period_pad + k);
      __m512d r5b = _mm512_loadu_pd(tile16 + 13*period_pad + k);
      __m512d r6b = _mm512_loadu_pd(tile16 + 14*period_pad + k);
      __m512d r7b = _mm512_loadu_pd(tile16 + 15*period_pad + k);

      __m512d c0a,c1a,c2a,c3a,c4a,c5a,c6a,c7a;
      transpose8x8_avx512(r0a, r1a, r2a, r3a, r4a, r5a, r6a, r7a,
                          c0a,c1a,c2a,c3a,c4a,c5a,c6a,c7a);
      __m512d c0b,c1b,c2b,c3b,c4b,c5b,c6b,c7b;
      transpose8x8_avx512(r0b, r1b, r2b, r3b, r4b, r5b, r6b, r7b,
                          c0b,c1b,c2b,c3b,c4b,c5b,c6b,c7b);

      int m = (kk - k) < 8 ? (kk - k) : 8;
      if (m >= 1) { _mm512_storeu_pd(out_val[k+0] + bs, c0a); _mm512_storeu_pd(out_val[k+0] + bs + 8, c0b); }
      if (m >= 2) { _mm512_storeu_pd(out_val[k+1] + bs, c1a); _mm512_storeu_pd(out_val[k+1] + bs + 8, c1b); }
      if (m >= 3) { _mm512_storeu_pd(out_val[k+2] + bs, c2a); _mm512_storeu_pd(out_val[k+2] + bs + 8, c2b); }
      if (m >= 4) { _mm512_storeu_pd(out_val[k+3] + bs, c3a); _mm512_storeu_pd(out_val[k+3] + bs + 8, c3b); }
      if (m >= 5) { _mm512_storeu_pd(out_val[k+4] + bs, c4a); _mm512_storeu_pd(out_val[k+4] + bs + 8, c4b); }
      if (m >= 6) { _mm512_storeu_pd(out_val[k+5] + bs, c5a); _mm512_storeu_pd(out_val[k+5] + bs + 8, c5b); }
      if (m >= 7) { _mm512_storeu_pd(out_val[k+6] + bs, c6a); _mm512_storeu_pd(out_val[k+6] + bs + 8, c6b); }
      if (m >= 8) { _mm512_storeu_pd(out_val[k+7] + bs, c7a); _mm512_storeu_pd(out_val[k+7] + bs + 8, c7b); }
    }
  }
}
R_xlen_t bs_tail = (n_blocks / TR) * TR;
for (R_xlen_t bs = bs_tail; bs < n_blocks; ++bs) {
  const double* row = vs + (R_xlen_t)first_src[(size_t)bs];
  for (int k = 0; k < kk; ++k) out_val[(size_t)k][bs] = row[k];
}
if (nt_store_ok) _mm_sfence();
        } else {
          for (R_xlen_t bs = 0; bs < n_blocks; ++bs) {
            const double* row = vs + (R_xlen_t)first_src[(size_t)bs];
            for (int k = 0; k < kk; ++k) out_val[(size_t)k][bs] = row[k];
          }
        }
#else
        for (R_xlen_t bs = 0; bs < n_blocks; ++bs) {
          const double* row = vs + (R_xlen_t)first_src[(size_t)bs];
          for (int k = 0; k < kk; ++k) out_val[(size_t)k][bs] = row[k];
        }
#endif
      } else {
        bool need_fill = (!dense) || na_rm;
        double fv = use_fill ? fill_val : NA_REAL;
        if (need_fill) {
          for (int k = 0; k < k_out; ++k)
            fill_double_ptr(out_val[(size_t)k], fv, (size_t)n_out);
        }
        for (R_xlen_t bs = 0; bs < n_blocks; ++bs) {
          R_xlen_t io = (R_xlen_t)first_src[(size_t)bs];
          if (val_type == REALSXP) {
            const double* s = vs + io;
            for (int k = 0; k < kk; ++k) {
              double v = s[k];
              if (na_rm && ISNAN(v)) continue;
              out_val[(size_t)k][bs] = v;
            }
          } else if (val_type == INTSXP) {
            const int* s = vi + io;
            for (int k = 0; k < kk; ++k) {
              int v = s[k];
              if (na_rm && v == NA_INTEGER) continue;
              out_val[(size_t)k][bs] = (v == NA_INTEGER) ? NA_REAL : (double)v;
            }
          } else {
            const int* s = vl + io;
            for (int k = 0; k < kk; ++k) {
              int v = s[k];
              if (na_rm && v == NA_LOGICAL) continue;
              out_val[(size_t)k][bs] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
            }
          }
        }
      }
    } else {
      bool need_fill = (!dense) || na_rm;
      double fv = use_fill ? fill_val : NA_REAL;
      if (need_fill) {
        for (int k = 0; k < k_out; ++k)
          fill_double_ptr(out_val[(size_t)k], fv, (size_t)n_out);
      }
      const int32_t* rp = row_of.data();
      const int32_t* cp = col_of.data();
      if (val_type == REALSXP) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
        for (R_xlen_t i = 0; i < nlong; ++i) {
          double v = vs[i];
          if (na_rm && ISNAN(v)) continue;
          out_val[(size_t)cp[i]][rp[i]] = v;
        }
      } else if (val_type == INTSXP) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int v = vi[i];
          if (na_rm && v == NA_INTEGER) continue;
          out_val[(size_t)cp[i]][rp[i]] = (v == NA_INTEGER) ? NA_REAL : (double)v;
        }
      } else {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int v = vl[i];
          if (na_rm && v == NA_LOGICAL) continue;
          out_val[(size_t)cp[i]][rp[i]] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
        }
      }
    }

    // =====================================================================
    // Phase 5
    // =====================================================================
    Rf_setAttrib(out, R_NamesSymbol, out_names);
    {
      SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
      INTEGER(rn)[0] = NA_INTEGER;
      INTEGER(rn)[1] = -n_out;
      Rf_setAttrib(out, R_RowNamesSymbol, rn);
      UNPROTECT(1);
    }
    Rf_setAttrib(out, R_ClassSymbol, Rf_mkString("data.frame"));

    UNPROTECT(2);
    return out;
}
