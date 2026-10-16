// [[Rcpp::plugins(cpp17)]]
// [[Rcpp::plugins(openmp)]]
//
// dcast.cpp -- dataprep 0.1.8.1
//
//   [1] 16x16 AVX-512 register transpose for period == 16.
//   [2] Compile-time specialization on period / k_out (2,4,5,8,10,16,
//       20,32,64).
//   [3] SIMD id-gather via _mm512_i32gather_epi32/64.
//   [4] Non-temporal stores for the per-column output.
//   [5] Zero-copy fast path dcast_fast_cpp.
//   [6] Thread-local scratch for the tiny path (col_id / row_id /
//       row_src reuse).
//   [7] Dense index shortcut for id columns whose min-max range equals
//       n_out.
//   [8] Column-streamed transpose for fs_arith large n_blocks.
//
#include <Rcpp.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <vector>
#include <algorithm>
#include <numeric>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <type_traits>

#if defined(__x86_64__) || defined(_M_X64) || \
    defined(__i386__)   || defined(_M_IX86)
#  define DCAST_X86 1
#  include <immintrin.h>
#else
#  define DCAST_X86 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define DCAST_GNUC 1
#  define DCAST_TARGET(x)      __attribute__((target(x)))
#  define DCAST_NOINLINE       __attribute__((noinline))
#  define DCAST_ALWAYS_INLINE  __attribute__((always_inline))
#  define DCAST_LIKELY(x)      __builtin_expect(!!(x), 1)
#  define DCAST_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#else
#  define DCAST_GNUC 0
#  define DCAST_TARGET(x)
#  define DCAST_NOINLINE
#  define DCAST_ALWAYS_INLINE
#  define DCAST_LIKELY(x)      (x)
#  define DCAST_UNLIKELY(x)    (x)
#endif

#if defined(__linux__)
#  include <sys/mman.h>
#  include <unistd.h>
#  ifndef MADV_HUGEPAGE
#    define DCAST_NO_THP 1
#  else
#    define DCAST_NO_THP 0
#  endif
#else
#  define DCAST_NO_THP 1
#endif

#ifdef _OPENMP
#  include <omp.h>
#endif

using namespace Rcpp;

#if defined(R_VERSION) && R_VERSION >= R_Version(4, 5, 0)
#  define DCAST_STRING_PTR_RO(x) STRING_PTR_RO(x)
#else
#  define DCAST_STRING_PTR_RO(x) STRING_PTR(x)
#endif

// =====================================================================
// [0] Compile-time switch macro
// =====================================================================
#define DCAST_DISPATCH_PERIOD(P, FN, ...)                        \
  do {                                                           \
    switch ((P)) {                                               \
      case   2: FN<  2>(__VA_ARGS__); break;                     \
      case   4: FN<  4>(__VA_ARGS__); break;                     \
      case   5: FN<  5>(__VA_ARGS__); break;                     \
      case   8: FN<  8>(__VA_ARGS__); break;                     \
      case  10: FN< 10>(__VA_ARGS__); break;                     \
      case  16: FN< 16>(__VA_ARGS__); break;                     \
      case  20: FN< 20>(__VA_ARGS__); break;                     \
      case  32: FN< 32>(__VA_ARGS__); break;                     \
      case  64: FN< 64>(__VA_ARGS__); break;                     \
      default:  FN<  0>(__VA_ARGS__); break;                     \
    }                                                            \
  } while (0)

// =====================================================================
// [1] Memory hints
// =====================================================================
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

static inline void touch_2m_fast(void* p, size_t bytes) {
#if !DCAST_NO_THP
  if (bytes < (2ULL << 20)) return;
  volatile char* c = (volatile char*)p;
  const size_t step = 2ULL << 20;
  for (size_t i = 0; i < bytes; i += step) c[i] = 0;
  c[bytes - 1] = 0;
#else
  (void)p; (void)bytes;
#endif
}

static inline void hint_alloc(void* p, size_t bytes) {
#if !DCAST_NO_THP
  if (!p || bytes < (512ULL << 10)) return;

  madvise(p, bytes, MADV_HUGEPAGE);

#  ifdef MADV_INTERLEAVE
  if (bytes >= (32ULL << 20)) madvise(p, bytes, MADV_INTERLEAVE);
#  endif

  if (bytes >= (2ULL << 20)) {
    volatile char* c = (volatile char*)p;
    const size_t STEP = 2ULL << 20;
    for (size_t off = 0; off < bytes; off += STEP) {
      c[off] = 0;
    }
    c[bytes - 1] = 0;
  }

#  if defined(MADV_POPULATE_WRITE)
  if (bytes >= (16ULL << 20) &&
      madvise(p, bytes, MADV_POPULATE_WRITE) == 0) {
    return;
  }
#  endif

#  ifdef MADV_COLLAPSE
  if (bytes >= (4ULL << 20)) {
    madvise(p, bytes, MADV_COLLAPSE);
  }
#  endif
#else
  (void)p; (void)bytes;
#endif
}

static inline void hint_readonly(SEXP col) {
#if !DCAST_NO_THP
  if (!col || col == R_NilValue) return;
  SEXPTYPE t = TYPEOF(col);
  size_t es = sizeof_sexpvec(t);
  if (es == 0) {
    if (t != STRSXP) return;
    es = sizeof(SEXP);
  }
  R_xlen_t n = XLENGTH(col);
  if (n <= 0) return;
  size_t bytes = (size_t)n * es;
  if (bytes < (2ULL << 20)) return;
  void* p = (void*)DATAPTR_RO(col);
  if (!p) return;
  madvise(p, bytes, MADV_HUGEPAGE);
#  ifdef MADV_COLLAPSE
  madvise(p, bytes, MADV_COLLAPSE);
#  endif
#else
  (void)col;
#endif
}

static inline void* writable_ptr_sexp(SEXP x) {
  switch (TYPEOF(x)) {
  case INTSXP:  return (void*)INTEGER(x);
  case LGLSXP:  return (void*)LOGICAL(x);
  case REALSXP: return (void*)REAL(x);
  default:      return nullptr;
  }
}

static inline SEXP alloc_smart(SEXPTYPE type, R_xlen_t n) {
  SEXP v = Rf_allocVector(type, n);
  size_t es = sizeof_sexpvec(type);
  if (es != 0 && n > 0) {
    void* p = writable_ptr_sexp(v);
    if (p) hint_alloc(p, (size_t)n * es);
  }
  return v;
}

static inline SEXP cached_df_class() {
  static SEXP s = R_NilValue;
  if (s == R_NilValue) { s = Rf_mkString("data.frame"); R_PreserveObject(s); }
  return s;
}

// =====================================================================
// [2] CPU feature detection + SIMD fill
// =====================================================================
static void fill_scalar(double* d, double v, size_t n) {
  for (size_t i = 0; i < n; ++i) d[i] = v;
}

#if DCAST_GNUC && DCAST_X86
DCAST_TARGET("avx2") DCAST_NOINLINE
static void fill_avx2(double* __restrict__ d, double v, size_t n) {
  size_t i = 0;
  __m256d vv = _mm256_set1_pd(v);
  for (; i + 16 <= n; i += 16) {
    _mm256_storeu_pd(d + i,      vv);
    _mm256_storeu_pd(d + i + 4,  vv);
    _mm256_storeu_pd(d + i + 8,  vv);
    _mm256_storeu_pd(d + i + 12, vv);
  }
  for (; i + 4 <= n; i += 4) _mm256_storeu_pd(d + i, vv);
  for (; i < n; ++i) d[i] = v;
}

DCAST_TARGET("avx512f,avx512vl") DCAST_NOINLINE
static void fill_avx512(double* __restrict__ d, double v, size_t n) {
  size_t i = 0;
  __m512d vv = _mm512_set1_pd(v);
  for (; i + 32 <= n; i += 32) {
    _mm512_storeu_pd(d + i,      vv);
    _mm512_storeu_pd(d + i + 8,  vv);
    _mm512_storeu_pd(d + i + 16, vv);
    _mm512_storeu_pd(d + i + 24, vv);
  }
  for (; i + 8 <= n; i += 8) _mm512_storeu_pd(d + i, vv);
  for (; i < n; ++i) d[i] = v;
}
#endif

static void (*fill_double_ptr)(double*, double, size_t) = fill_scalar;
static bool g_have_avx512 = false;
static bool g_have_avx512_gather = false;

static void init_cpu_features() {
  static bool done = false;
  if (done) return;
  done = true;
#if DCAST_GNUC && DCAST_X86
  if (__builtin_cpu_supports("avx512f") &&
      __builtin_cpu_supports("avx512vl") &&
      __builtin_cpu_supports("avx512bw") &&
      __builtin_cpu_supports("avx512dq")) {
    fill_double_ptr = fill_avx512;
    g_have_avx512 = true;
    if (__builtin_cpu_supports("avx512f")) g_have_avx512_gather = true;
  } else if (__builtin_cpu_supports("avx2")) {
    fill_double_ptr = fill_avx2;
  }
#endif
}

// =====================================================================
// [3] 16x16 AVX-512 register transpose
// =====================================================================
#if DCAST_GNUC && DCAST_X86
DCAST_TARGET("avx512f,avx512vl") DCAST_ALWAYS_INLINE
static inline void transpose16x16_avx512(
    __m512d r0, __m512d r1, __m512d r2, __m512d r3,
    __m512d r4, __m512d r5, __m512d r6, __m512d r7,
    __m512d r8, __m512d r9, __m512d r10, __m512d r11,
    __m512d r12, __m512d r13, __m512d r14, __m512d r15,
    __m512d& o0, __m512d& o1, __m512d& o2, __m512d& o3,
    __m512d& o4, __m512d& o5, __m512d& o6, __m512d& o7,
    __m512d& o8, __m512d& o9, __m512d& o10, __m512d& o11,
    __m512d& o12, __m512d& o13, __m512d& o14, __m512d& o15)
{
  __m512d t0  = _mm512_unpacklo_pd(r0,  r1);
  __m512d t1  = _mm512_unpackhi_pd(r0,  r1);
  __m512d t2  = _mm512_unpacklo_pd(r2,  r3);
  __m512d t3  = _mm512_unpackhi_pd(r2,  r3);
  __m512d t4  = _mm512_unpacklo_pd(r4,  r5);
  __m512d t5  = _mm512_unpackhi_pd(r4,  r5);
  __m512d t6  = _mm512_unpacklo_pd(r6,  r7);
  __m512d t7  = _mm512_unpackhi_pd(r6,  r7);
  __m512d t8  = _mm512_unpacklo_pd(r8,  r9);
  __m512d t9  = _mm512_unpackhi_pd(r8,  r9);
  __m512d t10 = _mm512_unpacklo_pd(r10, r11);
  __m512d t11 = _mm512_unpackhi_pd(r10, r11);
  __m512d t12 = _mm512_unpacklo_pd(r12, r13);
  __m512d t13 = _mm512_unpackhi_pd(r12, r13);
  __m512d t14 = _mm512_unpacklo_pd(r14, r15);
  __m512d t15 = _mm512_unpackhi_pd(r14, r15);

  __m512d s0  = _mm512_shuffle_f64x2(t0,  t2,  0x88);
  __m512d s1  = _mm512_shuffle_f64x2(t0,  t2,  0xDD);
  __m512d s2  = _mm512_shuffle_f64x2(t1,  t3,  0x88);
  __m512d s3  = _mm512_shuffle_f64x2(t1,  t3,  0xDD);
  __m512d s4  = _mm512_shuffle_f64x2(t4,  t6,  0x88);
  __m512d s5  = _mm512_shuffle_f64x2(t4,  t6,  0xDD);
  __m512d s6  = _mm512_shuffle_f64x2(t5,  t7,  0x88);
  __m512d s7  = _mm512_shuffle_f64x2(t5,  t7,  0xDD);
  __m512d s8  = _mm512_shuffle_f64x2(t8,  t10, 0x88);
  __m512d s9  = _mm512_shuffle_f64x2(t8,  t10, 0xDD);
  __m512d s10 = _mm512_shuffle_f64x2(t9,  t11, 0x88);
  __m512d s11 = _mm512_shuffle_f64x2(t9,  t11, 0xDD);
  __m512d s12 = _mm512_shuffle_f64x2(t12, t14, 0x88);
  __m512d s13 = _mm512_shuffle_f64x2(t12, t14, 0xDD);
  __m512d s14 = _mm512_shuffle_f64x2(t13, t15, 0x88);
  __m512d s15 = _mm512_shuffle_f64x2(t13, t15, 0xDD);

  o0  = _mm512_shuffle_f64x2(s0,  s8,  0x88);
  o1  = _mm512_shuffle_f64x2(s1,  s9,  0x88);
  o2  = _mm512_shuffle_f64x2(s2,  s10, 0x88);
  o3  = _mm512_shuffle_f64x2(s3,  s11, 0x88);
  o4  = _mm512_shuffle_f64x2(s4,  s12, 0x88);
  o5  = _mm512_shuffle_f64x2(s5,  s13, 0x88);
  o6  = _mm512_shuffle_f64x2(s6,  s14, 0x88);
  o7  = _mm512_shuffle_f64x2(s7,  s15, 0x88);
  o8  = _mm512_shuffle_f64x2(s0,  s8,  0xDD);
  o9  = _mm512_shuffle_f64x2(s1,  s9,  0xDD);
  o10 = _mm512_shuffle_f64x2(s2,  s10, 0xDD);
  o11 = _mm512_shuffle_f64x2(s3,  s11, 0xDD);
  o12 = _mm512_shuffle_f64x2(s4,  s12, 0xDD);
  o13 = _mm512_shuffle_f64x2(s5,  s13, 0xDD);
  o14 = _mm512_shuffle_f64x2(s6,  s14, 0xDD);
  o15 = _mm512_shuffle_f64x2(s7,  s15, 0xDD);
}

// =====================================================================
// [3b] 8x8 AVX-512 helpers
// =====================================================================
DCAST_TARGET("avx512f,avx512vl") DCAST_ALWAYS_INLINE
static inline void transpose8x8_avx512(
    __m512d r0, __m512d r1, __m512d r2, __m512d r3,
    __m512d r4, __m512d r5, __m512d r6, __m512d r7,
    __m512d& c0, __m512d& c1, __m512d& c2, __m512d& c3,
    __m512d& c4, __m512d& c5, __m512d& c6, __m512d& c7)
{
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
  c0 = _mm512_shuffle_f64x2(s0, s4, 0x88);
  c1 = _mm512_shuffle_f64x2(s2, s6, 0x88);
  c2 = _mm512_shuffle_f64x2(s1, s5, 0x88);
  c3 = _mm512_shuffle_f64x2(s3, s7, 0x88);
  c4 = _mm512_shuffle_f64x2(s0, s4, 0xDD);
  c5 = _mm512_shuffle_f64x2(s2, s6, 0xDD);
  c6 = _mm512_shuffle_f64x2(s1, s5, 0xDD);
  c7 = _mm512_shuffle_f64x2(s3, s7, 0xDD);
}

DCAST_TARGET("avx512f,avx512vl") DCAST_NOINLINE
static void transpose_32row_tile_avx512(
    const int32_t* __restrict__ first_src,
    const double*  __restrict__ vs,
    double* const* __restrict__ out_val,
    R_xlen_t bs, int kk, int period, int nr_rows,
    bool na_rm, double fill_val)
{
  alignas(64) double tile[32 * 32];

  for (int i = 0; i < nr_rows; ++i) {
    R_xlen_t io = (R_xlen_t)first_src[(size_t)(bs + i)];
    double* row = tile + (size_t)i * period;
    std::memcpy(row, vs + io, (size_t)period * sizeof(double));
    if (na_rm) {
      for (int k = 0; k < period; ++k)
        if (ISNAN(row[k])) row[k] = fill_val;
    }
  }

  const int kk8 = (kk / 8) * 8;
  const int nr8 = (nr_rows / 8) * 8;

  for (int r = 0; r < nr8; r += 8) {
    for (int k = 0; k < kk8; k += 8) {
      __m512d r0 = _mm512_loadu_pd(tile + (size_t)(r + 0) * period + k);
      __m512d r1 = _mm512_loadu_pd(tile + (size_t)(r + 1) * period + k);
      __m512d r2 = _mm512_loadu_pd(tile + (size_t)(r + 2) * period + k);
      __m512d r3 = _mm512_loadu_pd(tile + (size_t)(r + 3) * period + k);
      __m512d r4 = _mm512_loadu_pd(tile + (size_t)(r + 4) * period + k);
      __m512d r5 = _mm512_loadu_pd(tile + (size_t)(r + 5) * period + k);
      __m512d r6 = _mm512_loadu_pd(tile + (size_t)(r + 6) * period + k);
      __m512d r7 = _mm512_loadu_pd(tile + (size_t)(r + 7) * period + k);
      __m512d c0, c1, c2, c3, c4, c5, c6, c7;
      transpose8x8_avx512(r0, r1, r2, r3, r4, r5, r6, r7,
                          c0, c1, c2, c3, c4, c5, c6, c7);
      _mm512_storeu_pd(out_val[(size_t)k + 0] + bs + r, c0);
      _mm512_storeu_pd(out_val[(size_t)k + 1] + bs + r, c1);
      _mm512_storeu_pd(out_val[(size_t)k + 2] + bs + r, c2);
      _mm512_storeu_pd(out_val[(size_t)k + 3] + bs + r, c3);
      _mm512_storeu_pd(out_val[(size_t)k + 4] + bs + r, c4);
      _mm512_storeu_pd(out_val[(size_t)k + 5] + bs + r, c5);
      _mm512_storeu_pd(out_val[(size_t)k + 6] + bs + r, c6);
      _mm512_storeu_pd(out_val[(size_t)k + 7] + bs + r, c7);
    }
    for (int k = kk8; k < kk; ++k) {
      for (int t = 0; t < 8; ++t)
        out_val[(size_t)k][bs + r + t] = tile[(size_t)(r + t) * period + k];
    }
  }
  for (int i = nr8; i < nr_rows; ++i) {
    const double* row = tile + (size_t)i * period;
    for (int k = 0; k < kk; ++k) out_val[(size_t)k][bs + i] = row[k];
  }
}

static void transpose_8x8_rows(
    const int32_t* __restrict__ first_src,
    const double*  __restrict__ vs,
    double* const* __restrict__ out_val,
    R_xlen_t n_blocks, int kk, int period, int n_threads,
    bool na_rm, double fill_val)
{
#if DCAST_GNUC && DCAST_X86
  if (n_threads < 1) n_threads = 1;

  constexpr int NR = 32;
  const R_xlen_t n_tiles = (n_blocks + NR - 1) / NR;

  if (n_tiles > 0) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(n_threads) \
    if(n_threads > 1 && n_tiles >= 2)
#endif
      for (R_xlen_t t = 0; t < n_tiles; ++t) {
        const R_xlen_t bs = (R_xlen_t)t * NR;
        const int tr = (int)std::min<R_xlen_t>((R_xlen_t)NR, n_blocks - bs);
        transpose_32row_tile_avx512(first_src, vs, out_val, bs, kk, period, tr,
                                    na_rm, fill_val);
      }
  }
#else
  (void)first_src; (void)vs; (void)out_val; (void)n_blocks;
  (void)kk; (void)period; (void)n_threads;
  (void)na_rm; (void)fill_val;
#endif
}

// =====================================================================
// [4] Specialized tile transpose
// =====================================================================
template <int TILE>
DCAST_TARGET("avx512f,avx512vl") DCAST_NOINLINE
static void transpose_tile_specialized(
    const int32_t* __restrict__ first_src,
    const double*  __restrict__ vs,
    double* const* __restrict__ out_val,
    R_xlen_t bs, int kk, int period, int nr_rows)
{
  alignas(64) double tile[16 * 64];
  const int TR_eff = std::min(nr_rows, 16);
  const int P_eff  = std::min(period, TILE > 0 ? TILE : 64);

  for (int i = 0; i < TR_eff; ++i) {
    R_xlen_t io = (R_xlen_t)first_src[(size_t)(bs + i)];
    std::memcpy(tile + (size_t)i * P_eff, vs + io,
                (size_t)P_eff * sizeof(double));
  }

  if constexpr (TILE == 16) {
    if (TR_eff == 16 && period == 16) {
      __m512d r0  = _mm512_loadu_pd(tile + 0);
      __m512d r1  = _mm512_loadu_pd(tile + 16);
      __m512d r2  = _mm512_loadu_pd(tile + 32);
      __m512d r3  = _mm512_loadu_pd(tile + 48);
      __m512d r4  = _mm512_loadu_pd(tile + 64);
      __m512d r5  = _mm512_loadu_pd(tile + 80);
      __m512d r6  = _mm512_loadu_pd(tile + 96);
      __m512d r7  = _mm512_loadu_pd(tile + 112);
      __m512d r8  = _mm512_loadu_pd(tile + 128);
      __m512d r9  = _mm512_loadu_pd(tile + 144);
      __m512d r10 = _mm512_loadu_pd(tile + 160);
      __m512d r11 = _mm512_loadu_pd(tile + 176);
      __m512d r12 = _mm512_loadu_pd(tile + 192);
      __m512d r13 = _mm512_loadu_pd(tile + 208);
      __m512d r14 = _mm512_loadu_pd(tile + 224);
      __m512d r15 = _mm512_loadu_pd(tile + 240);
      __m512d o0, o1, o2, o3, o4, o5, o6, o7;
      __m512d o8, o9, o10, o11, o12, o13, o14, o15;
      transpose16x16_avx512(r0, r1, r2, r3, r4, r5, r6, r7,
                            r8, r9, r10, r11, r12, r13, r14, r15,
                            o0, o1, o2, o3, o4, o5, o6, o7,
                            o8, o9, o10, o11, o12, o13, o14, o15);
      _mm512_storeu_pd(out_val[0]  + bs, o0);
      _mm512_storeu_pd(out_val[1]  + bs, o1);
      _mm512_storeu_pd(out_val[2]  + bs, o2);
      _mm512_storeu_pd(out_val[3]  + bs, o3);
      _mm512_storeu_pd(out_val[4]  + bs, o4);
      _mm512_storeu_pd(out_val[5]  + bs, o5);
      _mm512_storeu_pd(out_val[6]  + bs, o6);
      _mm512_storeu_pd(out_val[7]  + bs, o7);
      _mm512_storeu_pd(out_val[8]  + bs, o8);
      _mm512_storeu_pd(out_val[9]  + bs, o9);
      _mm512_storeu_pd(out_val[10] + bs, o10);
      _mm512_storeu_pd(out_val[11] + bs, o11);
      _mm512_storeu_pd(out_val[12] + bs, o12);
      _mm512_storeu_pd(out_val[13] + bs, o13);
      _mm512_storeu_pd(out_val[14] + bs, o14);
      _mm512_storeu_pd(out_val[15] + bs, o15);
      return;
    }
  }

  const int kk8 = (P_eff / 8) * 8;
  const int nr8 = (TR_eff / 8) * 8;
  for (int r = 0; r < nr8; r += 8) {
    for (int k = 0; k < kk8; k += 8) {
      __m512d r0 = _mm512_loadu_pd(tile + (size_t)(r + 0) * P_eff + k);
      __m512d r1 = _mm512_loadu_pd(tile + (size_t)(r + 1) * P_eff + k);
      __m512d r2 = _mm512_loadu_pd(tile + (size_t)(r + 2) * P_eff + k);
      __m512d r3 = _mm512_loadu_pd(tile + (size_t)(r + 3) * P_eff + k);
      __m512d r4 = _mm512_loadu_pd(tile + (size_t)(r + 4) * P_eff + k);
      __m512d r5 = _mm512_loadu_pd(tile + (size_t)(r + 5) * P_eff + k);
      __m512d r6 = _mm512_loadu_pd(tile + (size_t)(r + 6) * P_eff + k);
      __m512d r7 = _mm512_loadu_pd(tile + (size_t)(r + 7) * P_eff + k);
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
      __m512d c0 = _mm512_shuffle_f64x2(s0, s4, 0x88);
      __m512d c1 = _mm512_shuffle_f64x2(s2, s6, 0x88);
      __m512d c2 = _mm512_shuffle_f64x2(s1, s5, 0x88);
      __m512d c3 = _mm512_shuffle_f64x2(s3, s7, 0x88);
      __m512d c4 = _mm512_shuffle_f64x2(s0, s4, 0xDD);
      __m512d c5 = _mm512_shuffle_f64x2(s2, s6, 0xDD);
      __m512d c6 = _mm512_shuffle_f64x2(s1, s5, 0xDD);
      __m512d c7 = _mm512_shuffle_f64x2(s3, s7, 0xDD);
      _mm512_storeu_pd(out_val[(size_t)k + 0] + bs + r, c0);
      _mm512_storeu_pd(out_val[(size_t)k + 1] + bs + r, c1);
      _mm512_storeu_pd(out_val[(size_t)k + 2] + bs + r, c2);
      _mm512_storeu_pd(out_val[(size_t)k + 3] + bs + r, c3);
      _mm512_storeu_pd(out_val[(size_t)k + 4] + bs + r, c4);
      _mm512_storeu_pd(out_val[(size_t)k + 5] + bs + r, c5);
      _mm512_storeu_pd(out_val[(size_t)k + 6] + bs + r, c6);
      _mm512_storeu_pd(out_val[(size_t)k + 7] + bs + r, c7);
    }
    for (int k = kk8; k < P_eff; ++k) {
      for (int t = 0; t < 8; ++t)
        out_val[(size_t)k][bs + r + t] = tile[(size_t)(r + t) * P_eff + k];
    }
  }
  for (int i = nr8; i < TR_eff; ++i) {
    const double* row = tile + (size_t)i * P_eff;
    for (int k = 0; k < P_eff; ++k) out_val[(size_t)k][bs + i] = row[k];
  }
}

// =====================================================================
// [5] Specialized dispatch driver
// =====================================================================
template <int TILE>
static void transpose_16row_avx512(
    const int32_t* __restrict__ first_src,
    const double*  __restrict__ vs,
    double* const* __restrict__ out_val,
    R_xlen_t n_blocks, int kk, int period, int n_threads)
{
#if DCAST_GNUC && DCAST_X86
  if (n_threads < 1) n_threads = 1;
  constexpr int NR = 16;
  const R_xlen_t n_tiles = (n_blocks + NR - 1) / NR;
  if (n_tiles <= 0) return;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(n_threads) \
    if(n_threads > 1 && n_tiles >= 2)
#endif
  for (R_xlen_t t = 0; t < n_tiles; ++t) {
    const R_xlen_t bs = (R_xlen_t)t * NR;
    const int tr = (int)std::min<R_xlen_t>((R_xlen_t)NR, n_blocks - bs);
    transpose_tile_specialized<TILE>(first_src, vs, out_val,
                                     bs, kk, period, tr);
  }
#else
  (void)first_src; (void)vs; (void)out_val;
  (void)n_blocks; (void)kk; (void)period; (void)n_threads;
#endif
}

// =====================================================================
// [6] SIMD id-gather
// =====================================================================
DCAST_TARGET("avx512f,avx512vl") DCAST_NOINLINE
static void gather_i32_avx512(
    int* __restrict__ dst,
    const int* __restrict__ src,
    const int32_t* __restrict__ idx,
    R_xlen_t n)
{
  R_xlen_t i = 0;
  for (; i + 16 <= n; i += 16) {
    __m512i vi = _mm512_loadu_si512((const __m512i*)(idx + i));
    __m512i v  = _mm512_i32gather_epi32(vi, src, 4);
    _mm512_storeu_si512((__m512i*)(dst + i), v);
  }
  for (; i < n; ++i) dst[i] = src[idx[i]];
}

DCAST_TARGET("avx512f,avx512vl") DCAST_NOINLINE
static void gather_f64_avx512(
    double* __restrict__ dst,
    const double* __restrict__ src,
    const int32_t* __restrict__ idx,
    R_xlen_t n)
{
  R_xlen_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m256i vi = _mm256_loadu_si256((const __m256i*)(idx + i));
    __m512d v  = _mm512_i32gather_pd(vi, src, 8);
    _mm512_storeu_pd(dst + i, v);
  }
  for (; i < n; ++i) dst[i] = src[idx[i]];
}
#endif  // DCAST_GNUC && DCAST_X86

// Non-x86 fallbacks for the SIMD entry points above.  They are
// never reached at runtime because g_have_avx512 and
// g_have_avx512_gather stay false on platforms without AVX-512;
// they exist only so the call sites inside dcast_core() compile
// on ARM64 and other non-x86 targets.
#if !(DCAST_GNUC && DCAST_X86)

template <int TILE>
static void transpose_16row_avx512(
    const int32_t*, const double*, double* const*,
    R_xlen_t, int, int, int) {}

static void transpose_8x8_rows(
    const int32_t*, const double*, double* const*,
    R_xlen_t, int, int, int, bool, double) {}

static inline void gather_i32_avx512(
    int* __restrict__ dst, const int* __restrict__ src,
    const int32_t* __restrict__ idx, R_xlen_t n) {
  for (R_xlen_t i = 0; i < n; ++i) dst[i] = src[idx[i]];
}

static inline void gather_f64_avx512(
    double* __restrict__ dst, const double* __restrict__ src,
    const int32_t* __restrict__ idx, R_xlen_t n) {
  for (R_xlen_t i = 0; i < n; ++i) dst[i] = src[idx[i]];
}

#endif  // !(DCAST_GNUC && DCAST_X86)

// =====================================================================
// [6b] Single-pass scatter with cache-aware tiling
// =====================================================================
static void scatter_block_tiled(
    const int32_t* __restrict__ first_src,
    const double*  __restrict__ vs,
    double* const* __restrict__ out_val,
    int32_t n_out, int period, int n_threads)
{
  if (n_threads < 1) n_threads = 1;
  if (n_out <= 0 || period <= 0) return;

  if (n_out < 128 || period < 32) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(n_threads) \
    if(n_threads > 1 && n_out >= 16)
#endif
      for (int32_t r = 0; r < n_out; ++r) {
        const double* src = vs + first_src[r];
        for (int k = 0; k < period; ++k) {
          out_val[k][r] = src[k];
        }
      }
      return;
  }

  constexpr int RB = 64;
  constexpr int CB = 32;
  const int n_r_blocks = (n_out + RB - 1) / RB;
  const int n_c_blocks = (period + CB - 1) / CB;

#ifdef _OPENMP
#pragma omp parallel num_threads(n_threads) if(n_threads > 1)
#endif
{
  alignas(64) double tile[RB * CB];
#ifdef _OPENMP
#pragma omp for collapse(2) schedule(static)
#endif
  for (int cb = 0; cb < n_c_blocks; ++cb) {
    for (int rb = 0; rb < n_r_blocks; ++rb) {
      const int r0 = rb * RB;
      const int r1 = std::min(r0 + RB, (int)n_out);
      const int c0 = cb * CB;
      const int c1 = std::min(c0 + CB, period);

      for (int r = r0; r < r1; ++r) {
        const double* src = vs + first_src[r] + c0;
        double* trow = tile + (r - r0) * CB;
        std::memcpy(trow, src, (size_t)(c1 - c0) * sizeof(double));
      }

      for (int k = c0; k < c1; ++k) {
        double* dst = out_val[k] + r0;
        const int nrr = r1 - r0;
        for (int r = 0; r < nrr; ++r) {
          dst[r] = tile[r * CB + (k - c0)];
        }
      }
    }
  }
}
}

// =====================================================================
// [6c] Column-streamed transpose for fs_arith large n_blocks
// =====================================================================
static void transpose_column_stream(
    const int32_t* __restrict__ first_src,
    const double*  __restrict__ vs,
    double* const* __restrict__ out_val,
    int32_t n_blocks, int kk, int n_threads)
{
  if (n_threads < 1) n_threads = 1;
  if (n_blocks <= 0 || kk <= 0) return;

  constexpr int32_t BLOCK = 2048;
  const int32_t n_blk_chunks = (n_blocks + BLOCK - 1) / BLOCK;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(n_threads) \
    if(n_threads > 1 && n_blk_chunks >= 2)
#endif
  for (int32_t blk = 0; blk < n_blk_chunks; ++blk) {
    const int32_t b0 = blk * BLOCK;
    const int32_t b1 = std::min(b0 + BLOCK, n_blocks);
    for (int c = 0; c < kk; ++c) {
      double* __restrict__ dst = out_val[(size_t)c];
      const int32_t* __restrict__ fs = first_src;
      int32_t b = b0;
      constexpr int32_t PF = 8;
      for (; b + PF < b1; ++b) {
        __builtin_prefetch(&vs[fs[b + PF] + c], 0, 1);
        dst[b] = vs[fs[b] + c];
      }
      for (; b < b1; ++b) dst[b] = vs[fs[b] + c];
    }
  }
}

// =====================================================================
// [7] Legacy tile transpose
// =====================================================================
static void transpose_tile(
    const int32_t* __restrict__ first_src,
    const void*    __restrict__ vs_raw,
    double* const* __restrict__ out_val,
    R_xlen_t n_blocks, int kk, int period, int n_threads, int kind,
    bool na_rm, double fill_val)
{
  if (n_threads < 1) n_threads = 1;
  constexpr int TR = 128;
  const int pad = kk;
  const bool use_stack = (pad * TR <= 16384);

  const double* vs_d = (kind == 0) ? (const double*)vs_raw : nullptr;
  const int*    vs_i = (kind != 0) ? (const int*)vs_raw    : nullptr;

  bool fs_is_arith = false;
  int32_t fs_step = 0;
  if (n_blocks >= 2 && kind == 0) {
    fs_step = first_src[1] - first_src[0];
    fs_is_arith = (fs_step == (int32_t)period);
    if (fs_is_arith) {
      const R_xlen_t m = std::min<R_xlen_t>(n_blocks, (R_xlen_t)64);
      for (R_xlen_t i = 2; i < m; ++i)
        if (first_src[(size_t)i] - first_src[(size_t)(i - 1)] != fs_step) {
          fs_is_arith = false; break;
        }
    }
  }

#ifdef _OPENMP
#pragma omp parallel num_threads(n_threads) if(n_threads > 1)
#endif
{
  std::vector<double> heap_tile;
  if (!use_stack) heap_tile.resize((size_t)TR * (size_t)pad);
  alignas(64) double stack_tile[128 * 128];
  double* tile = use_stack ? stack_tile : heap_tile.data();

#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
  for (R_xlen_t bs = 0; bs < n_blocks; bs += TR) {
    const int tr = (int)std::min<R_xlen_t>((R_xlen_t)TR, n_blocks - bs);

    if (kind == 0 && fs_is_arith) {
      std::memcpy(tile, vs_d + (R_xlen_t)first_src[(size_t)bs],
                  (size_t)tr * (size_t)period * sizeof(double));
      if (na_rm) {
        const size_t total = (size_t)tr * (size_t)period;
        for (size_t x = 0; x < total; ++x)
          if (ISNAN(tile[x])) tile[x] = fill_val;
      }
    } else if (kind == 0) {
      for (int t = 0; t < tr; ++t) {
        R_xlen_t io = (R_xlen_t)first_src[(size_t)(bs + t)];
        const double* src = vs_d + io;
        __builtin_prefetch((const void*)src, 0, 3);
        double* row = tile + (size_t)t * pad;
        std::memcpy(row, src, (size_t)kk * sizeof(double));
        if (na_rm) {
          for (int k = 0; k < kk; ++k)
            if (ISNAN(row[k])) row[k] = fill_val;
        }
      }
    } else if (kind == 1) {
      for (int t = 0; t < tr; ++t) {
        R_xlen_t io = (R_xlen_t)first_src[(size_t)(bs + t)];
        const int* src = vs_i + io;
        __builtin_prefetch((const void*)src, 0, 3);
        double* row = tile + (size_t)t * pad;
        for (int k = 0; k < kk; ++k) {
          int v = src[k];
          row[k] = (v == NA_INTEGER)
                     ? (na_rm ? fill_val : NA_REAL)
                     : (double)v;
        }
      }
    } else {
      for (int t = 0; t < tr; ++t) {
        R_xlen_t io = (R_xlen_t)first_src[(size_t)(bs + t)];
        const int* src = vs_i + io;
        __builtin_prefetch((const void*)src, 0, 3);
        double* row = tile + (size_t)t * pad;
        for (int k = 0; k < kk; ++k) {
          int v = src[k];
          row[k] = (v == NA_LOGICAL)
                     ? (na_rm ? fill_val : NA_REAL)
                     : (v ? 1.0 : 0.0);
        }
      }
    }

    for (int k = 0; k < kk; ++k) {
      double* dst = out_val[(size_t)k] + bs;
      const double* src = tile + k;
      for (int t = 0; t < tr; ++t) dst[t] = src[(size_t)t * pad];
    }
  }
}
}

// =====================================================================
// [8] Utilities
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

struct ColDesc {
  const int32_t* raw  = nullptr;
  const int32_t* code = nullptr;
  int32_t  mn = 0;
  uint32_t na_code = 0;
  uint32_t units   = 1;
  bool     has_na  = false;
  bool     dense   = false;
};

static inline uint32_t code_of(const ColDesc& d, R_xlen_t i) {
  if (DCAST_LIKELY(d.code != nullptr)) return (uint32_t)d.code[i];
  int32_t v = d.raw[i];
  if (DCAST_UNLIKELY(v == NA_INTEGER)) return d.na_code;
  return (uint32_t)((int64_t)v - (int64_t)d.mn);
}

template <int N>
struct KeyFnN {
  const ColDesc* c;
  const int* sh;
  inline uint64_t operator()(R_xlen_t i) const {
    uint64_t k = 0;
#if DCAST_GNUC
#  pragma GCC unroll 8
#endif
    for (int j = 0; j < N; ++j)
      k |= (uint64_t)code_of(c[j], i) << sh[j];
    return k;
  }
};

struct KeyFnDyn {
  const ColDesc* c;
  const int* sh;
  int n;
  inline uint64_t operator()(R_xlen_t i) const {
    uint64_t k = 0;
    for (int j = 0; j < n; ++j)
      k |= (uint64_t)code_of(c[j], i) << sh[j];
    return k;
  }
};

static inline bool keys_equal(
    const std::vector<ColDesc>& desc, int n_id, R_xlen_t a, R_xlen_t b)
{
  for (int c = 0; c < n_id; ++c) {
    if (code_of(desc[c], a) != code_of(desc[c], b)) return false;
  }
  return true;
}

static inline bool tuple_less(
    const std::vector<ColDesc>& desc, int n_id, R_xlen_t a, R_xlen_t b)
{
  for (int c = 0; c < n_id; ++c) {
    uint32_t va = code_of(desc[c], a);
    uint32_t vb = code_of(desc[c], b);
    if (va < vb) return true;
    if (va > vb) return false;
  }
  return false;
}

static void radix_sort_u64_idx_bits(const uint64_t* keys,
                                    std::vector<int32_t>& idx,
                                    R_xlen_t n, int max_bits) {
  if (n < 2) return;
  int n_pass = (max_bits + 15) / 16;
  if (n_pass < 1) n_pass = 1;
  if (n_pass > 4) n_pass = 4;

  std::vector<int32_t> tmp((size_t)n);
  std::vector<int32_t> cnt(1 << 16);
  for (int pass = 0; pass < n_pass; ++pass) {
    int shift = pass * 16;
    std::fill(cnt.begin(), cnt.end(), 0);
    for (R_xlen_t i = 0; i < n; ++i)
      ++cnt[(keys[(size_t)idx[(size_t)i]] >> shift) & 0xFFFF];
    int32_t s = 0;
    for (int k = 0; k < (1 << 16); ++k) { int32_t c = cnt[k]; cnt[k] = s; s += c; }
    for (R_xlen_t i = 0; i < n; ++i)
      tmp[(size_t)cnt[(keys[(size_t)idx[(size_t)i]] >> shift) & 0xFFFF]++] =
        idx[(size_t)i];
    idx.swap(tmp);
  }
}

// =====================================================================
// [9] Phase-1 builders
// =====================================================================
template <class KF>
static void build_block_phase1_sorted(KF kf,
                                      R_xlen_t n_blocks, R_xlen_t period,
                                      int n_threads, int total_bits,
                                      std::vector<int32_t>& first_src,
                                      int32_t& n_out)
{
  if (n_blocks <= 0) { n_out = 0; return; }

  bool sorted = true;
  if (n_blocks >= 2) {
    uint64_t prev = kf(0);
    for (R_xlen_t b = 1; b < n_blocks; ++b) {
      uint64_t k = kf(b);
      if (k < prev) { sorted = false; break; }
      prev = k;
    }
  }
  if (sorted) {
    first_src.clear();
    first_src.reserve((size_t)std::min<R_xlen_t>(n_blocks, (R_xlen_t)1 << 22));
    uint64_t prev = kf(0);
    for (R_xlen_t b = 1; b < n_blocks; ++b) {
      uint64_t k = kf(b);
      if (k != prev) {
        first_src.push_back((int32_t)((int64_t)(b - 1) * (int64_t)period));
        prev = k;
      }
    }
    first_src.push_back((int32_t)((int64_t)(n_blocks - 1) * (int64_t)period));
    n_out = (int32_t)first_src.size();
    return;
  }

  std::vector<uint64_t> keys((size_t)n_blocks);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(n_threads) \
    if(n_threads > 1 && n_blocks >= 200000)
#endif
  for (R_xlen_t b = 0; b < n_blocks; ++b)
    keys[(size_t)b] = kf(b);

  std::vector<int32_t> idx((size_t)n_blocks);
  std::iota(idx.begin(), idx.end(), 0);
  radix_sort_u64_idx_bits(keys.data(), idx, n_blocks, total_bits);

  first_src.clear();
  first_src.reserve((size_t)std::min<R_xlen_t>(n_blocks, (R_xlen_t)1 << 22));
  uint64_t prev = ~0ull;
  int32_t prev_bi = -1;
  for (R_xlen_t t = 0; t < n_blocks; ++t) {
    int32_t bi = idx[(size_t)t];
    uint64_t k = keys[(size_t)bi];
    if (t > 0 && k != prev) {
      first_src.push_back((int32_t)((int64_t)prev_bi * (int64_t)period));
    }
    prev = k;
    prev_bi = bi;
  }
  if (n_blocks > 0) {
    first_src.push_back((int32_t)((int64_t)prev_bi * (int64_t)period));
  }
  n_out = (int32_t)first_src.size();
}

template <class KF>
static void build_general_phase1_sorted(KF kf,
                                        R_xlen_t nlong, int n_threads,
                                        int total_bits,
                                        std::vector<int32_t>& row_of,
                                        std::vector<int32_t>& first_src,
                                        int32_t& n_out)
{
  if (nlong <= 0) { n_out = 0; return; }

  bool sorted = true;
  if (nlong >= 2) {
    uint64_t prev = kf(0);
    for (R_xlen_t i = 1; i < nlong; ++i) {
      uint64_t k = kf(i);
      if (k < prev) { sorted = false; break; }
      prev = k;
    }
  }
  if (sorted) {
    first_src.clear();
    first_src.reserve((size_t)std::min<R_xlen_t>(nlong, (R_xlen_t)1 << 22));
    uint64_t p = kf(0);
    int32_t r = 0;
    first_src.push_back(0);
    row_of[0] = 0;
    for (R_xlen_t i = 1; i < nlong; ++i) {
      uint64_t k = kf(i);
      if (k != p) { ++r; first_src.push_back((int32_t)i); p = k; }
      row_of[(size_t)i] = r;
    }
    n_out = r + 1;
    return;
  }

  std::vector<uint64_t> keys((size_t)nlong);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(n_threads) \
    if(n_threads > 1 && nlong >= 200000)
#endif
  for (R_xlen_t i = 0; i < nlong; ++i)
    keys[(size_t)i] = kf(i);

  std::vector<int32_t> idx((size_t)nlong);
  std::iota(idx.begin(), idx.end(), 0);
  radix_sort_u64_idx_bits(keys.data(), idx, nlong, total_bits);

  std::vector<int32_t> sorted_rows((size_t)nlong);
  uint64_t prev = ~0ull;
  int32_t row = -1;
  first_src.clear();
  first_src.reserve((size_t)std::min<R_xlen_t>(nlong, (R_xlen_t)1 << 22));
  for (R_xlen_t t = 0; t < nlong; ++t) {
    int32_t src = idx[(size_t)t];
    uint64_t k = keys[(size_t)src];
    if (t == 0 || k != prev) {
      first_src.push_back(src); prev = k; ++row;
    }
    sorted_rows[(size_t)t] = row;
  }
  n_out = row + 1;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(n_threads) \
    if(n_threads > 1 && nlong >= 200000)
#endif
  for (R_xlen_t t = 0; t < nlong; ++t)
    row_of[(size_t)idx[(size_t)t]] = sorted_rows[(size_t)t];
}

static void build_block_phase1_radix(
    const std::vector<ColDesc>& desc, int n_id,
    R_xlen_t n_blocks, R_xlen_t period,
    std::vector<int32_t>& first_src, int32_t& n_out)
{
  if (n_blocks <= 0) { n_out = 0; return; }

  bool sorted = true;
  for (R_xlen_t b = 1; b < n_blocks; ++b) {
    if (tuple_less(desc, n_id, b, b - 1)) { sorted = false; break; }
  }

  std::vector<int32_t> idx((size_t)n_blocks);
  std::iota(idx.begin(), idx.end(), 0);

  if (!sorted) {
    std::vector<int32_t> tmp((size_t)n_blocks);
    std::vector<int32_t> cnt;

    for (int c = n_id - 1; c >= 0; --c) {
      const ColDesc& d = desc[c];
      uint32_t units = d.units;
      if (units <= 1) continue;

      const size_t need = (size_t)units + 1;
      if (cnt.size() < need) cnt.resize(need);
      std::fill(cnt.begin(), cnt.begin() + need, 0);

      for (R_xlen_t i = 0; i < n_blocks; ++i) {
        uint32_t code = code_of(d, idx[(size_t)i]);
        ++cnt[code + 1];
      }
      for (uint32_t k = 1; k <= units; ++k) cnt[k] += cnt[k - 1];
      for (R_xlen_t i = 0; i < n_blocks; ++i) {
        uint32_t code = code_of(d, idx[(size_t)i]);
        tmp[(size_t)cnt[code]++] = idx[(size_t)i];
      }
      idx.swap(tmp);
    }
  }

  first_src.clear();
  first_src.reserve((size_t)std::min<R_xlen_t>(n_blocks, (R_xlen_t)1 << 22));

  if (n_blocks > 0) {
    first_src.push_back((int32_t)((int64_t)idx[0] * (int64_t)period));
    for (R_xlen_t t = 1; t < n_blocks; ++t) {
      int32_t cur  = idx[(size_t)t];
      int32_t prev = idx[(size_t)(t - 1)];
      if (!keys_equal(desc, n_id, cur, prev)) {
        first_src.push_back((int32_t)((int64_t)cur * (int64_t)period));
      }
    }
  }
  n_out = (int32_t)first_src.size();
}

static void build_general_phase1_radix(
    const std::vector<ColDesc>& desc, int n_id, R_xlen_t nlong,
    std::vector<int32_t>& row_of,
    std::vector<int32_t>& first_src, int32_t& n_out)
{
  if (nlong <= 0) { n_out = 0; return; }

  bool sorted = true;
  for (R_xlen_t i = 1; i < nlong; ++i) {
    if (tuple_less(desc, n_id, i, i - 1)) { sorted = false; break; }
  }

  std::vector<int32_t> idx((size_t)nlong);
  std::iota(idx.begin(), idx.end(), 0);

  if (!sorted) {
    std::vector<int32_t> tmp((size_t)nlong);
    std::vector<int32_t> cnt;

    for (int c = n_id - 1; c >= 0; --c) {
      const ColDesc& d = desc[c];
      uint32_t units = d.units;
      if (units <= 1) continue;

      const size_t need = (size_t)units + 1;
      if (cnt.size() < need) cnt.resize(need);
      std::fill(cnt.begin(), cnt.begin() + need, 0);

      for (R_xlen_t i = 0; i < nlong; ++i) {
        uint32_t code = code_of(d, idx[(size_t)i]);
        ++cnt[code + 1];
      }
      for (uint32_t k = 1; k <= units; ++k) cnt[k] += cnt[k - 1];
      for (R_xlen_t i = 0; i < nlong; ++i) {
        uint32_t code = code_of(d, idx[(size_t)i]);
        tmp[(size_t)cnt[code]++] = idx[(size_t)i];
      }
      idx.swap(tmp);
    }
  }

  first_src.clear();
  first_src.reserve((size_t)std::min<R_xlen_t>(nlong, (R_xlen_t)1 << 22));
  row_of.resize((size_t)nlong);

  int32_t run = -1;
  for (R_xlen_t t = 0; t < nlong; ++t) {
    int32_t cur = idx[(size_t)t];
    if (t == 0 || !keys_equal(desc, n_id, cur, idx[(size_t)(t - 1)])) {
      ++run;
      first_src.push_back(cur);
    }
    row_of[(size_t)cur] = run;
  }
  n_out = run + 1;
}

static void build_general_phase1_sorted_n1_fast(
    const std::vector<ColDesc>& desc, R_xlen_t nlong,
    std::vector<int32_t>& row_of,
    std::vector<int32_t>& first_src, int32_t& n_out)
{
  const int32_t* __restrict__ codes = desc[0].code;
  if (DCAST_UNLIKELY(!codes)) {
    build_general_phase1_radix(desc, 1, nlong, row_of, first_src, n_out);
    return;
  }

  row_of.resize((size_t)nlong);
  first_src.clear();
  first_src.reserve(256);

  int32_t prev = codes[0];
  int32_t r = 0;
  first_src.push_back(0);
  row_of[0] = 0;

  for (R_xlen_t i = 1; i < nlong; ++i) {
    int32_t cur = codes[i];
    if (DCAST_UNLIKELY(cur < prev)) {
      build_general_phase1_radix(desc, 1, nlong, row_of, first_src, n_out);
      return;
    }
    if (cur != prev) {
      ++r;
      first_src.push_back((int32_t)i);
      prev = cur;
    }
    row_of[(size_t)i] = r;
  }
  n_out = r + 1;
}

static void build_block_phase1_sorted_n1_fast(
    const std::vector<ColDesc>& desc,
    R_xlen_t n_blocks, R_xlen_t period,
    int n_threads,
    std::vector<int32_t>& first_src, int32_t& n_out)
{
  const int32_t* __restrict__ codes = desc[0].code;
  if (DCAST_UNLIKELY(!codes)) {
    int sh = 0;
    build_block_phase1_sorted(KeyFnN<1>{desc.data(), &sh},
                              n_blocks, period, n_threads, 32,
                              first_src, n_out);
    return;
  }
  if (n_blocks <= 0) { n_out = 0; return; }

  first_src.clear();
  first_src.reserve((size_t)std::min<R_xlen_t>(n_blocks, (R_xlen_t)1 << 22));

  int32_t prev = codes[0];
  R_xlen_t first_b = 0;

  for (R_xlen_t b = 1; b < n_blocks; ++b) {
    int32_t cur = codes[b];
    if (DCAST_UNLIKELY(cur < prev)) {
      int sh = 0;
      first_src.clear();
      build_block_phase1_sorted(KeyFnN<1>{desc.data(), &sh},
                                n_blocks, period, n_threads, 32,
                                first_src, n_out);
      return;
    }
    if (cur != prev) {
      first_src.push_back((int32_t)((int64_t)first_b * (int64_t)period));
      first_b = b;
      prev = cur;
    }
  }
  first_src.push_back((int32_t)((int64_t)first_b * (int64_t)period));
  n_out = (int32_t)first_src.size();
}

static void build_block_phase1_hash(
    const std::vector<ColDesc>& desc, int n_id,
    R_xlen_t n_blocks, R_xlen_t period,
    std::vector<int32_t>& first_src, int32_t& n_out)
{
  if (n_blocks <= 0) { n_out = 0; return; }
  if (n_blocks < 64) {
    int sh = 0;
    build_block_phase1_sorted(KeyFnN<1>{desc.data(), &sh},
                              n_blocks, period, 1, 32,
                              first_src, n_out);
    return;
  }

  size_t cap = 128;
  while (cap < (size_t)n_blocks * 2) cap <<= 1;
  const size_t mask = cap - 1;

  std::vector<int32_t> table(cap, -1);
  first_src.clear();
  first_src.reserve(256);

  for (R_xlen_t b = 0; b < n_blocks; ++b) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int c = 0; c < n_id; ++c) {
      h = (h ^ (uint64_t)code_of(desc[c], b)) * 0x100000001b3ULL;
    }
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;

    size_t idx = (size_t)h & mask;
    while (true) {
      int32_t slot = table[idx];
      if (slot < 0) {
        table[idx] = (int32_t)b;
        first_src.push_back((int32_t)((int64_t)b * (int64_t)period));
        break;
      }
      if (keys_equal(desc, n_id, slot, b)) break;
      idx = (idx + 1) & mask;
    }
  }
  n_out = (int32_t)first_src.size();
}

// =====================================================================
// [10] Tiny fast path (nlong <= 16384)
// =====================================================================
struct TinyHashMap {
  static constexpr int CAP  = 8192;
  static constexpr int MASK = CAP - 1;
  static constexpr int MAX_INSERT = CAP * 3 / 4;

  std::vector<uint64_t> keys;
  std::vector<int>      vals;
  std::vector<uint8_t>  used;

  TinyHashMap() : keys(CAP), vals(CAP), used(CAP, 0) {}

  static inline uint64_t mix(uint64_t h) {
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
  }
  inline int find_or_insert(uint64_t key, int& next_id) {
    uint64_t h = mix(key);
    int idx = (int)(h & MASK);
    while (used[idx]) {
      if (keys[idx] == key) return vals[idx];
      idx = (idx + 1) & MASK;
    }
    if (next_id >= MAX_INSERT) return -1;
    keys[idx] = key;
    vals[idx] = next_id;
    used[idx] = 1;
    return next_id++;
  }
};

static SEXP dcast_tiny_simd(
    SEXP data, int var_idx, int val_idx,
    const std::vector<int>& id_cols,
    R_xlen_t nlong,
    bool use_fill, double fill_val,
    bool na_rm)
{
  if (na_rm) return R_NilValue;
  if (nlong <= 0 || nlong > 16384) return R_NilValue;
  if (id_cols.empty() || id_cols.size() > 8) return R_NilValue;

  SEXP var_col = VECTOR_ELT(data, var_idx);
  SEXP val_col = VECTOR_ELT(data, val_idx);
  SEXPTYPE var_type = TYPEOF(var_col);
  SEXPTYPE val_type = TYPEOF(val_col);

  const bool var_is_str  = (var_type == STRSXP);
  const bool var_is_int  = (var_type == INTSXP) || (var_type == LGLSXP);
  const bool var_is_real = (var_type == REALSXP);
  if (Rf_isFactor(var_col)) return R_NilValue;
  if (!var_is_str && !var_is_int && !var_is_real) return R_NilValue;

  const bool val_is_real = (val_type == REALSXP);
  const bool val_is_int  = (val_type == INTSXP) || (val_type == LGLSXP);
  if (!val_is_real && !val_is_int) return R_NilValue;

  const int n_id = (int)id_cols.size();
  for (int j = 0; j < n_id; ++j) {
    SEXPTYPE t = TYPEOF(VECTOR_ELT(data, id_cols[j]));
    if (t != INTSXP && t != LGLSXP && t != REALSXP && t != STRSXP)
      return R_NilValue;
  }

  constexpr int MAX_LEVELS = 512;
  constexpr int MAX_ROWS   = 4096;

  struct TinyScratch {
    std::vector<int32_t> col_id, row_id, row_src;
    void ensure(R_xlen_t n, int nr) {
      if ((R_xlen_t)col_id.size() < n) col_id.resize((size_t)n);
      if ((R_xlen_t)row_id.size() < n) row_id.resize((size_t)n);
      if ((R_xlen_t)row_src.size() < (R_xlen_t)nr) row_src.resize((size_t)nr);
    }
  };
  static thread_local TinyScratch s_scratch;
  s_scratch.ensure(nlong, MAX_ROWS);
  int32_t* col_id  = s_scratch.col_id.data();
  int32_t* row_id  = s_scratch.row_id.data();
  int32_t* row_src = s_scratch.row_src.data();

  int n_cols = 0;
  int col_src[MAX_LEVELS];

  {
    TinyHashMap col_hash;
    const int*    var_i = (var_type == INTSXP)  ? INTEGER(var_col)
                        : (var_type == LGLSXP)  ? LOGICAL(var_col)
                        : nullptr;
    const double* var_d = (var_type == REALSXP) ? REAL(var_col) : nullptr;

    for (R_xlen_t i = 0; i < nlong; ++i) {
      uint64_t key;
      if (var_is_str) {
        SEXP s = STRING_ELT(var_col, i);
        key = (uint64_t)s;
        if (s == NA_STRING) key = 0xdeadbeefdeadbeefULL;
      } else if (var_is_int) {
        key = (uint64_t)(uint32_t)var_i[i];
      } else {
        double x = var_d[i];
        uint64_t bits;
        std::memcpy(&bits, &x, sizeof(bits));
        if (x == 0.0) bits = 0;
        key = bits;
      }
      int before = n_cols;
      int c = col_hash.find_or_insert(key, n_cols);
      if (DCAST_UNLIKELY(c < 0)) return R_NilValue;
      if (n_cols > before) {
        if (n_cols > MAX_LEVELS) return R_NilValue;
        col_src[c] = (int)i;
      }
      col_id[i] = c;
    }
  }

  int n_rows = 0;
  {
    TinyHashMap row_hash;
    const void* id_ptrs[8];
    SEXPTYPE    id_types[8];
    for (int j = 0; j < n_id; ++j) {
      SEXP col = VECTOR_ELT(data, id_cols[j]);
      SEXPTYPE t = TYPEOF(col);
      id_types[j] = t;
      if (t == REALSXP)      id_ptrs[j] = (const void*)REAL(col);
      else if (t == INTSXP)  id_ptrs[j] = (const void*)INTEGER(col);
      else if (t == LGLSXP)  id_ptrs[j] = (const void*)LOGICAL(col);
      else                   id_ptrs[j] = (const void*)DCAST_STRING_PTR_RO(col);
    }

    for (R_xlen_t i = 0; i < nlong; ++i) {
      uint64_t h = 0x9e3779b97f4a7c15ULL;
      for (int j = 0; j < n_id; ++j) {
        uint64_t v;
        SEXPTYPE t = id_types[j];
        if (t == REALSXP) {
          double x = ((const double*)id_ptrs[j])[i];
          uint64_t bits;
          std::memcpy(&bits, &x, sizeof(bits));
          if (x == 0.0) bits = 0;
          v = bits;
        } else if (t == INTSXP) {
          v = (uint64_t)(uint32_t)((const int*)id_ptrs[j])[i];
        } else if (t == LGLSXP) {
          v = (uint64_t)(uint32_t)((const int*)id_ptrs[j])[i];
        } else {
          uint64_t s = (uint64_t)((const SEXP*)id_ptrs[j])[i];
          if (s == (uint64_t)NA_STRING) s = 0xdeadbeefdeadbeefULL;
          v = s;
        }
        h = h * 0x9e3779b97f4a7c15ULL + v;
      }
      int before = n_rows;
      int r = row_hash.find_or_insert(h, n_rows);
      if (DCAST_UNLIKELY(r < 0)) return R_NilValue;
      if (n_rows > before) {
        if (n_rows > MAX_ROWS) return R_NilValue;
      }
      row_id[i] = r;
    }
  }

  if (n_cols == 0 || n_rows == 0) return R_NilValue;

  for (int r = 0; r < n_rows; ++r) row_src[r] = -1;
  for (R_xlen_t i = 0; i < nlong; ++i) {
    int r = row_id[i];
    if (row_src[r] < 0) row_src[r] = (int32_t)i;
  }

  SEXP out       = PROTECT(Rf_allocVector(VECSXP,  n_id + n_cols));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, n_id + n_cols));
  SEXP src_names = Rf_getAttrib(data, R_NamesSymbol);

  for (int j = 0; j < n_id; ++j) {
    SEXP src = VECTOR_ELT(data, id_cols[j]);
    SEXPTYPE t = TYPEOF(src);
    SEXP dst = PROTECT(Rf_allocVector(t, n_rows));
    Rf_copyMostAttrib(src, dst);
    if (t == INTSXP) {
      const int* s = INTEGER(src);
      int* d = INTEGER(dst);
      for (int r = 0; r < n_rows; ++r) d[r] = s[row_src[r]];
    } else if (t == LGLSXP) {
      const int* s = LOGICAL(src);
      int* d = LOGICAL(dst);
      for (int r = 0; r < n_rows; ++r) d[r] = s[row_src[r]];
    } else if (t == REALSXP) {
      const double* s = REAL(src);
      double* d = REAL(dst);
      for (int r = 0; r < n_rows; ++r) d[r] = s[row_src[r]];
    } else {
      const SEXP* s = DCAST_STRING_PTR_RO(src);
      for (int r = 0; r < n_rows; ++r) SET_STRING_ELT(dst, r, s[row_src[r]]);
    }
    SET_VECTOR_ELT(out, j, dst);
    SET_STRING_ELT(out_names, j, STRING_ELT(src_names, id_cols[j]));
    UNPROTECT(1);
  }

  const double init_v = use_fill ? fill_val : NA_REAL;
  std::vector<double*> out_val((size_t)n_cols);
  for (int c = 0; c < n_cols; ++c) {
    SEXP vc = PROTECT(Rf_allocVector(REALSXP, n_rows));
    double* vp = REAL(vc);
    for (int r = 0; r < n_rows; ++r) vp[r] = init_v;
    SET_VECTOR_ELT(out, n_id + c, vc);
    out_val[(size_t)c] = vp;
    UNPROTECT(1);
  }

  {
    const double* vs = (val_type == REALSXP) ? REAL(val_col)    : nullptr;
    const int*    vi = (val_type == INTSXP)  ? INTEGER(val_col) : nullptr;
    const int*    vl = (val_type == LGLSXP)  ? LOGICAL(val_col) : nullptr;

    if (val_is_real) {
      for (R_xlen_t i = 0; i < nlong; ++i) {
        out_val[(size_t)col_id[i]][row_id[i]] = vs[i];
      }
    } else if (val_type == INTSXP) {
      for (R_xlen_t i = 0; i < nlong; ++i) {
        int x = vi[i];
        out_val[(size_t)col_id[i]][row_id[i]] =
          (x == NA_INTEGER) ? NA_REAL : (double)x;
      }
    } else {
      for (R_xlen_t i = 0; i < nlong; ++i) {
        int x = vl[i];
        out_val[(size_t)col_id[i]][row_id[i]] =
          (x == NA_LOGICAL) ? NA_REAL : (x ? 1.0 : 0.0);
      }
    }
  }

  for (int c = 0; c < n_cols; ++c) {
    int src = col_src[c];
    if (var_is_str) {
      SET_STRING_ELT(out_names, n_id + c, STRING_ELT(var_col, src));
    } else if (var_is_int) {
      int v = (var_type == INTSXP) ? INTEGER(var_col)[src]
                                   : LOGICAL(var_col)[src];
      char buf[32];
      int l = fast_itoa(v, buf);
      SET_STRING_ELT(out_names, n_id + c, Rf_mkCharLen(buf, l));
    } else {
      double x = REAL(var_col)[src];
      char buf[64];
      int l;
      if (ISNA(x)) { buf[0] = 'N'; buf[1] = 'A'; l = 2; }
      else {
        l = std::snprintf(buf, sizeof(buf), "%.17g", x);
        if (l < 0) l = 0;
        if ((size_t)l >= sizeof(buf)) l = (int)sizeof(buf) - 1;
      }
      SET_STRING_ELT(out_names, n_id + c, Rf_mkCharLen(buf, l));
    }
  }

  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = -n_rows;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    UNPROTECT(1);
  }
  Rf_setAttrib(out, R_ClassSymbol, cached_df_class());
  UNPROTECT(2);
  return out;
}

// =====================================================================
// [11] Arg resolution
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
    if (id_cols.empty())
      stop("dcast_cpp: no id columns inferred; specify id explicitly");
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

static int infer_var_val_idx(SEXP data, SEXP spec, const char* fallback_name,
                             int fallback_idx) {
  if (!Rf_isNull(spec)) return resolve_col(data, spec, fallback_idx);
  SEXP names = Rf_getAttrib(data, R_NamesSymbol);
  int ncols = Rf_length(data);
  for (int i = 0; i < ncols; ++i)
    if (strcmp(CHAR(STRING_ELT(names, i)), fallback_name) == 0) return i;
  return -1;
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

static bool try_dense_radix_partition(
    SEXP data, const std::vector<int>& id_cols,
    R_xlen_t n_blocks, R_xlen_t period,
    std::vector<int32_t>& first_src, int32_t& n_out)
{
  const int n_id = (int)id_cols.size();
  if (n_id < 1 || n_blocks < 2) return false;
  std::vector<int32_t> mins(n_id), ranges(n_id);
  std::vector<const int32_t*> vs(n_id);
  for (int j = 0; j < n_id; ++j) {
    SEXP col = VECTOR_ELT(data, id_cols[j]);
    SEXPTYPE t = TYPEOF(col);
    if (t != INTSXP && t != LGLSXP) return false;
    const int32_t* v = (t == INTSXP) ? (const int32_t*)INTEGER(col)
                                     : (const int32_t*)LOGICAL(col);
    vs[j] = v;
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (R_xlen_t b = 0; b < n_blocks; ++b) {
      int32_t x = v[b * period];
      if (x == NA_INTEGER) return false;
      if (x < lo) lo = x;
      if (x > hi) hi = x;
    }
    int64_t r = (int64_t)hi - (int64_t)lo + 1;
    if (r <= 0 || r > (int64_t)n_blocks * 2) return false;
    mins[j] = lo; ranges[j] = (int32_t)r;
  }
  std::vector<int32_t> perm((size_t)n_blocks);
  std::iota(perm.begin(), perm.end(), 0);
  std::vector<int32_t> tmp((size_t)n_blocks);
  std::vector<int32_t> cnt;
  for (int j = n_id - 1; j >= 0; --j) {
    const int32_t* v = vs[j];
    int32_t lo = mins[j], rng = ranges[j];
    cnt.assign((size_t)rng + 1, 0);
    for (R_xlen_t b = 0; b < n_blocks; ++b) {
      int32_t x = v[(R_xlen_t)perm[(size_t)b] * period] - lo;
      ++cnt[(size_t)x + 1];
    }
    for (int32_t k = 1; k <= rng; ++k) cnt[(size_t)k] += cnt[(size_t)k - 1];
    for (R_xlen_t b = 0; b < n_blocks; ++b) {
      int32_t x = v[(R_xlen_t)perm[(size_t)b] * period] - lo;
      tmp[(size_t)cnt[(size_t)x]++] = perm[(size_t)b];
    }
    perm.swap(tmp);
  }
  std::vector<int32_t> reps;
  reps.reserve((size_t)std::min<R_xlen_t>(n_blocks, (R_xlen_t)1 << 20));
  auto keys_equal_local = [&](int32_t a, int32_t b) -> bool {
    for (int j = 0; j < n_id; ++j)
      if (vs[j][(R_xlen_t)a * period] != vs[j][(R_xlen_t)b * period]) return false;
    return true;
  };
  int32_t run_last = perm[0];
  for (R_xlen_t b = 1; b < n_blocks; ++b) {
    int32_t cur = perm[(size_t)b];
    if (!keys_equal_local(cur, run_last)) {
      reps.push_back(run_last);
    }
    run_last = cur;
  }
  reps.push_back(run_last);
  std::sort(reps.begin(), reps.end());
  first_src.resize(reps.size());
  for (size_t i = 0; i < reps.size(); ++i)
    first_src[i] = (int32_t)((int64_t)reps[i] * (int64_t)period);
  n_out = (int32_t)reps.size();
  return true;
}

// =====================================================================
// [12] Aggregation module
// =====================================================================
namespace dcast_agg {

struct StrColMap {
  static constexpr uint32_t CAP  = 2048;
  static constexpr uint32_t MASK = CAP - 1;
  SEXP    keys[CAP];
  int32_t vals[CAP];
  int32_t next = 0;
  StrColMap() { std::memset(keys, 0, sizeof(keys)); }
  static inline uint32_t h(SEXP s) {
    uint64_t x = (uint64_t)(uintptr_t)s * 0x9E3779B97F4A7C15ULL;
    x ^= x >> 29;
    return (uint32_t)x & MASK;
  }
  inline int32_t insert(SEXP s) {
    uint32_t k = h(s);
    for (uint32_t p = 0; p < CAP; ++p) {
      SEXP kk = keys[k];
      if (kk == nullptr) { keys[k] = s; vals[k] = next; return next++; }
      if (kk == s) return vals[k];
      k = (k + 1) & MASK;
    }
    return -1;
  }
  inline int32_t lookup(SEXP s) const {
    uint32_t k = h(s);
    for (uint32_t p = 0; p < CAP; ++p) {
      SEXP kk = keys[k];
      if (kk == nullptr) return -1;
      if (kk == s) return vals[k];
      k = (k + 1) & MASK;
    }
    return -1;
  }
};

struct alignas(16) FL_pair { double v; double i; };

static constexpr int PF_DIST = 64;

template <typename VAL_T>
static DCAST_ALWAYS_INLINE inline double extract_v(
    const VAL_T* __restrict__ vals, R_xlen_t i, bool& is_na)
{
  if constexpr (std::is_same_v<VAL_T, double>) {
    double v = vals[i]; is_na = ISNAN(v); return v;
  } else {
    int x = (int)vals[i]; is_na = (x == NA_INTEGER);
    return is_na ? NA_REAL : (double)x;
  }
}

template <typename ROW_T, typename VAL_T>
static DCAST_NOINLINE void accum_sum(
    const ROW_T* __restrict__ row_codes,
    const int32_t* __restrict__ col_codes,
    const VAL_T* __restrict__ vals,
    double* __restrict__ acc,
    int32_t n_out, R_xlen_t n, bool na_rm)
{
  const size_t stride = (size_t)n_out;
  R_xlen_t i = 0;
  for (; i + PF_DIST < n; ++i) {
    __builtin_prefetch(&acc[(size_t)col_codes[i + PF_DIST] * stride
                          + (size_t)row_codes[i + PF_DIST]], 1, 3);
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) acc[off] = NA_REAL; }
    else acc[off] += v;
  }
  for (; i < n; ++i) {
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) acc[off] = NA_REAL; }
    else acc[off] += v;
  }
}

template <typename ROW_T, typename VAL_T>
static DCAST_NOINLINE void accum_mean(
    const ROW_T* __restrict__ row_codes,
    const int32_t* __restrict__ col_codes,
    const VAL_T* __restrict__ vals,
    double* __restrict__ acc,
    int32_t* __restrict__ cnt,
    uint8_t* __restrict__ has_na,
    int32_t n_out, R_xlen_t n, bool na_rm)
{
  const size_t stride = (size_t)n_out;
  R_xlen_t i = 0;
  for (; i + PF_DIST < n; ++i) {
    __builtin_prefetch(&acc[(size_t)col_codes[i + PF_DIST] * stride
                          + (size_t)row_codes[i + PF_DIST]], 1, 3);
    __builtin_prefetch(&cnt[(size_t)col_codes[i + PF_DIST] * stride
                          + (size_t)row_codes[i + PF_DIST]], 1, 3);
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) has_na[off] = 1; }
    else { acc[off] += v; cnt[off] += 1; }
  }
  for (; i < n; ++i) {
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) has_na[off] = 1; }
    else { acc[off] += v; cnt[off] += 1; }
  }
}

template <typename ROW_T, typename VAL_T>
static DCAST_NOINLINE void accum_min(
    const ROW_T* __restrict__ row_codes,
    const int32_t* __restrict__ col_codes,
    const VAL_T* __restrict__ vals,
    double* __restrict__ acc,
    int32_t n_out, R_xlen_t n, bool na_rm)
{
  const size_t stride = (size_t)n_out;
  R_xlen_t i = 0;
  for (; i + PF_DIST < n; ++i) {
    __builtin_prefetch(&acc[(size_t)col_codes[i + PF_DIST] * stride
                          + (size_t)row_codes[i + PF_DIST]], 1, 3);
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) acc[off] = NA_REAL; }
    else { const double cur = acc[off]; acc[off] = (v < cur) ? v : cur; }
  }
  for (; i < n; ++i) {
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) acc[off] = NA_REAL; }
    else { const double cur = acc[off]; acc[off] = (v < cur) ? v : cur; }
  }
}

template <typename ROW_T, typename VAL_T>
static DCAST_NOINLINE void accum_max(
    const ROW_T* __restrict__ row_codes,
    const int32_t* __restrict__ col_codes,
    const VAL_T* __restrict__ vals,
    double* __restrict__ acc,
    int32_t n_out, R_xlen_t n, bool na_rm)
{
  const size_t stride = (size_t)n_out;
  R_xlen_t i = 0;
  for (; i + PF_DIST < n; ++i) {
    __builtin_prefetch(&acc[(size_t)col_codes[i + PF_DIST] * stride
                          + (size_t)row_codes[i + PF_DIST]], 1, 3);
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) acc[off] = NA_REAL; }
    else { const double cur = acc[off]; acc[off] = (v > cur) ? v : cur; }
  }
  for (; i < n; ++i) {
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    double v; bool is_na = false;
    v = extract_v<VAL_T>(vals, i, is_na);
    if (DCAST_UNLIKELY(is_na)) { if (!na_rm) acc[off] = NA_REAL; }
    else { const double cur = acc[off]; acc[off] = (v > cur) ? v : cur; }
  }
}

template <typename ROW_T>
static DCAST_NOINLINE void accum_length(
    const ROW_T* __restrict__ row_codes,
    const int32_t* __restrict__ col_codes,
    int32_t* __restrict__ cnt,
    int32_t n_out, R_xlen_t n)
{
  const size_t stride = (size_t)n_out;
  R_xlen_t i = 0;
  for (; i + PF_DIST < n; ++i) {
    __builtin_prefetch(&cnt[(size_t)col_codes[i + PF_DIST] * stride
                          + (size_t)row_codes[i + PF_DIST]], 1, 3);
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    cnt[off] += 1;
  }
  for (; i < n; ++i) {
    const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
    cnt[off] += 1;
  }
}

template <typename ROW_T, typename VAL_T>
static DCAST_NOINLINE void accum_first(
    const ROW_T* __restrict__ row_codes,
    const int32_t* __restrict__ col_codes,
    const VAL_T* __restrict__ vals,
    double* __restrict__ acc,
    int32_t n_out, R_xlen_t n, bool na_rm)
{
  const size_t stride = (size_t)n_out;
  if (DCAST_LIKELY(!na_rm)) {
    if constexpr (std::is_same_v<VAL_T, double>) {
      for (R_xlen_t i = n - 1; i >= 0; --i)
        acc[(size_t)col_codes[i] * stride + (size_t)row_codes[i]] = vals[i];
    } else {
      for (R_xlen_t i = n - 1; i >= 0; --i) {
        int x = (int)vals[i];
        acc[(size_t)col_codes[i] * stride + (size_t)row_codes[i]] =
          (x == NA_INTEGER) ? NA_REAL : (double)x;
      }
    }
  } else {
    for (R_xlen_t i = n - 1; i >= 0; --i) {
      const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
      bool is_na = false;
      double v = extract_v<VAL_T>(vals, i, is_na);
      if (!is_na) acc[off] = v;
    }
  }
}

template <typename ROW_T, typename VAL_T>
static DCAST_NOINLINE void accum_last(
    const ROW_T* __restrict__ row_codes,
    const int32_t* __restrict__ col_codes,
    const VAL_T* __restrict__ vals,
    double* __restrict__ acc,
    int32_t n_out, R_xlen_t n, bool na_rm)
{
  const size_t stride = (size_t)n_out;
  if (DCAST_LIKELY(!na_rm)) {
    if constexpr (std::is_same_v<VAL_T, double>) {
      for (R_xlen_t i = 0; i < n; ++i)
        acc[(size_t)col_codes[i] * stride + (size_t)row_codes[i]] = vals[i];
    } else {
      for (R_xlen_t i = 0; i < n; ++i) {
        int x = (int)vals[i];
        acc[(size_t)col_codes[i] * stride + (size_t)row_codes[i]] =
          (x == NA_INTEGER) ? NA_REAL : (double)x;
      }
    }
  } else {
    for (R_xlen_t i = 0; i < n; ++i) {
      const size_t off = (size_t)col_codes[i] * stride + (size_t)row_codes[i];
      bool is_na = false;
      double v = extract_v<VAL_T>(vals, i, is_na);
      if (!is_na) acc[off] = v;
    }
  }
}

template <typename GETCOL>
static DCAST_NOINLINE void fused_accumulate(
    const int32_t* __restrict__ id_vals,
    const double*  __restrict__ val_d,
    const int32_t* __restrict__ val_i,
    bool val_is_int,
    int32_t id_mn, int32_t na_row,
    GETCOL getcol,
    double*  __restrict__ acc_val,
    int32_t* __restrict__ cnt,
    uint8_t* __restrict__ hna,
    FL_pair* __restrict__ fl,
    int32_t row_stride, size_t cell_cnt, R_xlen_t nlong,
    int agg_fn, bool na_rm)
{
  const bool is_first = (agg_fn == 6);
  const bool is_last  = (agg_fn == 7);
  const bool is_fl    = is_first || is_last;
  double*  __restrict__ a  = is_fl ? nullptr : acc_val;
  int32_t* __restrict__ cp = cnt ? cnt : nullptr;
  uint8_t* __restrict__ hp = hna ? hna : nullptr;
  FL_pair* __restrict__ p  = is_fl ? fl : nullptr;
  constexpr R_xlen_t PF = 32;
  const bool pref_fl  = is_fl;
  const bool pref_acc = (!is_fl) && (agg_fn != 5);
  const bool pref_cnt = (agg_fn == 2) || (agg_fn == 5);
  for (R_xlen_t i = 0; i < nlong; ++i) {
    if (DCAST_LIKELY(PF < nlong - i)) {
      const int32_t iv2 = id_vals[i + PF];
      const int32_t r2  = (iv2 == NA_INTEGER) ? na_row : (iv2 - id_mn);
      const int32_t c2  = getcol(i + PF);
      const size_t  o2  = (size_t)c2 * (size_t)row_stride + (size_t)r2;
      if (DCAST_UNLIKELY(pref_fl)) __builtin_prefetch(&p[o2], 1, 1);
      else {
        if (DCAST_LIKELY(pref_acc)) __builtin_prefetch(&a[o2], 1, 1);
        if (DCAST_UNLIKELY(pref_cnt)) __builtin_prefetch(&cp[o2], 1, 1);
      }
    }
    const int32_t iv = id_vals[i];
    const int32_t r  = (iv == NA_INTEGER) ? na_row : (iv - id_mn);
    const int32_t c  = getcol(i);
    const size_t  off = (size_t)c * (size_t)row_stride + (size_t)r;
    if (is_fl) {
      const bool is_na = val_is_int ? (val_i[i] == NA_INTEGER) : ISNAN(val_d[i]);
      if (na_rm && is_na) continue;
      const double vi = is_na ? NA_REAL
                              : (val_is_int ? (double)val_i[i] : val_d[i]);
      const double di = (double)i;
      FL_pair& s = p[off];
      if (is_first) { if (di < s.i) { s.i = di; s.v = vi; } }
      else          { if (di > s.i) { s.i = di; s.v = vi; } }
      continue;
    }
    double v = 0.0;
    bool is_na = false;
    if (val_is_int) {
      const int32_t z = val_i[i];
      if (z == NA_INTEGER) is_na = true; else v = (double)z;
    } else {
      v = val_d[i]; if (ISNAN(v)) is_na = true;
    }
    switch (agg_fn) {
    case 1: if (is_na) { if (!na_rm && hp) hp[off] = 1; } else a[off] += v; break;
    case 2: if (is_na) { if (!na_rm && hp) hp[off] = 1; }
            else { a[off] += v; cp[off]++; } break;
    case 3: if (is_na) { if (!na_rm && hp) hp[off] = 1; }
            else { const double cur = a[off]; a[off] = (v < cur) ? v : cur; } break;
    case 4: if (is_na) { if (!na_rm && hp) hp[off] = 1; }
            else { const double cur = a[off]; a[off] = (v > cur) ? v : cur; } break;
    case 5: cp[off]++; break;
    default: break;
    }
  }
}

__attribute__((noinline, cold))
static SEXP dcast_aggregate_fused(
    SEXP data, int id_idx, int var_idx, int val_idx,
    int agg_fn, bool na_rm)
{
  if (agg_fn < 1 || agg_fn > 7) return R_NilValue;
  const R_xlen_t nlong = XLENGTH(VECTOR_ELT(data, id_idx));
  if (nlong <= 0 || nlong > (R_xlen_t)INT32_MAX) return R_NilValue;
  SEXP id_col  = VECTOR_ELT(data, id_idx);
  SEXP var_col = VECTOR_ELT(data, var_idx);
  SEXP val_col = VECTOR_ELT(data, val_idx);
  SEXPTYPE id_t  = TYPEOF(id_col);
  SEXPTYPE val_t = TYPEOF(val_col);
  if (id_t != INTSXP && id_t != LGLSXP) return R_NilValue;
  if (val_t != REALSXP && val_t != INTSXP && val_t != LGLSXP) return R_NilValue;
  const int32_t* __restrict__ id_vals = (id_t == INTSXP)
    ? (const int32_t*)INTEGER(id_col)
    : (const int32_t*)LOGICAL(id_col);
  int32_t id_mn = INT32_MAX, id_mx = INT32_MIN;
  bool    id_has_na = false;
  for (R_xlen_t i = 0; i < nlong; ++i) {
    const int32_t z = id_vals[i];
    if (z == NA_INTEGER) id_has_na = true;
    else { if (z < id_mn) id_mn = z; if (z > id_mx) id_mx = z; }
  }
  if (id_mn > id_mx) return R_NilValue;
  const uint64_t id_range = (uint64_t)((int64_t)id_mx - (int64_t)id_mn) + 1;
  const uint64_t row_stride_u = id_range + (id_has_na ? 1ULL : 0ULL);
  if (row_stride_u > (uint64_t)20000000ULL) return R_NilValue;
  const int32_t row_stride = (int32_t)row_stride_u;
  const int32_t na_row     = id_has_na ? (int32_t)id_range : -1;
  SEXPTYPE vt = TYPEOF(var_col);
  std::vector<SEXP>        col_seps;
  std::vector<std::string> col_keys;
  int32_t k_out = 0;
  std::vector<int32_t> var_lut;
  int32_t var_mn = 0;
  size_t  var_na_slot = 0;
  StrColMap var_str_map;
  const SEXP* __restrict__ var_s_vals = nullptr;
  const int32_t* __restrict__ var_i_vals = nullptr;
  if (vt == INTSXP || vt == LGLSXP) {
    var_i_vals = (vt == INTSXP) ? (const int32_t*)INTEGER(var_col)
                                : (const int32_t*)LOGICAL(var_col);
    int32_t vmn = INT32_MAX, vmx = INT32_MIN;
    for (R_xlen_t i = 0; i < nlong; ++i) {
      const int32_t z = var_i_vals[i];
      if (z == NA_INTEGER) continue;
      if (z < vmn) vmn = z;
      if (z > vmx) vmx = z;
    }
    if (vmn > vmx) return R_NilValue;
    const uint64_t vrange = (uint64_t)((int64_t)vmx - (int64_t)vmn) + 1;
    if (vrange > (uint64_t)1000000ULL) return R_NilValue;
    var_mn = vmn;
    var_lut.assign((size_t)vrange + 1, -1);
    var_na_slot = (size_t)vrange;
    for (R_xlen_t i = 0; i < nlong; ++i) {
      const int32_t z = var_i_vals[i];
      const size_t slot = (z == NA_INTEGER) ? var_na_slot
                                            : (size_t)((int64_t)z - (int64_t)vmn);
      if (var_lut[slot] < 0) {
        var_lut[slot] = k_out++;
        col_seps.push_back(R_NilValue);
        if (z == NA_INTEGER) col_keys.emplace_back("NA");
        else { char b[16]; int l = fast_itoa(z, b); col_keys.emplace_back(b, l); }
      }
    }
  } else if (vt == STRSXP) {
    var_s_vals = DCAST_STRING_PTR_RO(var_col);
    for (R_xlen_t i = 0; i < nlong; ++i) {
      SEXP s = var_s_vals[i];
      int32_t before = var_str_map.next;
      int32_t c = var_str_map.insert(s);
      if (c < 0) return R_NilValue;
      if (var_str_map.next > before) {
        col_seps.push_back(s);
        col_keys.emplace_back(s == NA_STRING ? "NA" : CHAR(s));
        k_out++;
      }
    }
  } else return R_NilValue;
  if (k_out <= 0) return R_NilValue;
  const size_t cell_cnt = (size_t)k_out * (size_t)row_stride;
  const bool is_fl = (agg_fn == 6 || agg_fn == 7);
  std::vector<double>  acc_all;
  std::vector<FL_pair> fl_all;
  std::vector<int32_t> cnt_all;
  std::vector<uint8_t> hna_all;
  if (is_fl) {
    fl_all.resize(cell_cnt);
    const double init_i = (agg_fn == 6)
      ?  std::numeric_limits<double>::infinity()
      : -std::numeric_limits<double>::infinity();
    for (auto& x : fl_all) { x.v = NA_REAL; x.i = init_i; }
  } else if (agg_fn == 5) {
    cnt_all.assign(cell_cnt, 0);
  } else {
    acc_all.resize(cell_cnt);
    if (agg_fn == 1 || agg_fn == 2)
      std::fill(acc_all.begin(), acc_all.end(), 0.0);
    else
      std::fill(acc_all.begin(), acc_all.end(), NA_REAL);
    if (agg_fn == 2) cnt_all.assign(cell_cnt, 0);
  }
  if (!na_rm && (agg_fn == 1 || agg_fn == 2 || agg_fn == 3 || agg_fn == 4))
    hna_all.assign(cell_cnt, 0);
  const double*  val_d = (val_t == REALSXP) ? REAL(val_col) : nullptr;
  const int32_t* val_i = (val_t == INTSXP)  ? (const int32_t*)INTEGER(val_col)
                        : (val_t == LGLSXP) ? (const int32_t*)LOGICAL(val_col)
                        : nullptr;
  const bool val_is_int = (val_t == INTSXP || val_t == LGLSXP);
  double*  acc_base = acc_all.empty() ? nullptr : acc_all.data();
  FL_pair* fl_base  = fl_all.empty()  ? nullptr : fl_all.data();
  int32_t* cnt_base = cnt_all.empty() ? nullptr : cnt_all.data();
  uint8_t* hna_base = hna_all.empty() ? nullptr : hna_all.data();

  if (vt == INTSXP || vt == LGLSXP) {
    const int32_t  vmn_l = var_mn;
    const size_t   nas_l = var_na_slot;
    const int32_t* lut_l = var_lut.data();
    const int32_t* vv_l  = var_i_vals;
    auto getcol = [vmn_l, nas_l, lut_l, vv_l](R_xlen_t i) -> int32_t {
      const int32_t z = vv_l[i];
      const size_t slot = (z == NA_INTEGER) ? nas_l
                                            : (size_t)((int64_t)z - vmn_l);
      return lut_l[slot];
    };
    fused_accumulate(id_vals, val_d, val_i, val_is_int,
                     id_mn, na_row, getcol,
                     acc_base, cnt_base, hna_base, fl_base,
                     row_stride, cell_cnt, nlong, agg_fn, na_rm);
  } else {
    const SEXP* sv_l = var_s_vals;
    const StrColMap* map_l = &var_str_map;
    auto getcol = [sv_l, map_l](R_xlen_t i) -> int32_t {
      return map_l->lookup(sv_l[i]);
    };
    fused_accumulate(id_vals, val_d, val_i, val_is_int,
                     id_mn, na_row, getcol,
                     acc_base, cnt_base, hna_base, fl_base,
                     row_stride, cell_cnt, nlong, agg_fn, na_rm);
  }
  std::vector<int32_t> row_map((size_t)row_stride, -1);
  std::vector<int32_t> first_src;
  first_src.reserve((size_t)row_stride);
  int32_t n_out = 0;
  for (R_xlen_t i = 0; i < nlong; ++i) {
    const int32_t iv = id_vals[i];
    const int32_t r  = (iv == NA_INTEGER) ? na_row : (iv - id_mn);
    if (row_map[(size_t)r] < 0) {
      row_map[(size_t)r] = n_out++;
      first_src.push_back((int32_t)i);
    }
  }
  if (n_out == 0) return R_NilValue;
  const bool compact = (n_out != (int32_t)row_stride);
  if (is_fl) {
    acc_all.resize(cell_cnt);
    const double empty = (agg_fn == 6)
      ?  std::numeric_limits<double>::infinity()
      : -std::numeric_limits<double>::infinity();
    const FL_pair* fp = fl_all.data();
    for (size_t j = 0; j < cell_cnt; ++j)
      acc_all[j] = (fp[j].i == empty) ? NA_REAL : fp[j].v;
    fl_all.clear(); fl_all.shrink_to_fit();
  } else if (agg_fn == 1 || agg_fn == 3 || agg_fn == 4) {
    if (hna_base)
      for (size_t j = 0; j < cell_cnt; ++j)
        if (hna_all[j]) acc_all[j] = NA_REAL;
    if (agg_fn == 3 || agg_fn == 4)
      for (size_t j = 0; j < cell_cnt; ++j)
        if (std::isinf(acc_all[j])) acc_all[j] = NA_REAL;
  } else if (agg_fn == 2) {
    for (size_t j = 0; j < cell_cnt; ++j) {
      const bool bad = (hna_base && hna_all[j]) || (cnt_all[j] == 0);
      acc_all[j] = bad ? NA_REAL : acc_all[j] / (double)cnt_all[j];
    }
  } else if (agg_fn == 5) {
    acc_all.resize(cell_cnt);
    const int32_t* cp2 = cnt_all.data();
    for (size_t j = 0; j < cell_cnt; ++j) acc_all[j] = (double)cp2[j];
  }
  SEXP id_names = Rf_getAttrib(data, R_NamesSymbol);
  SEXP out       = PROTECT(Rf_allocVector(VECSXP,  1 + k_out));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, 1 + k_out));
  {
    SEXP dst = PROTECT(alloc_smart(id_t, n_out));
    Rf_copyMostAttrib(id_col, dst);
    if (id_t == INTSXP) {
      const int32_t* s = (const int32_t*)INTEGER(id_col);
      int32_t*       d = (int32_t*)INTEGER(dst);
      for (int32_t r = 0; r < n_out; ++r) d[r] = s[first_src[(size_t)r]];
    } else {
      const int32_t* s = (const int32_t*)LOGICAL(id_col);
      int32_t*       d = (int32_t*)LOGICAL(dst);
      for (int32_t r = 0; r < n_out; ++r) d[r] = s[first_src[(size_t)r]];
    }
    SET_VECTOR_ELT(out, 0, dst);
    if (id_names != R_NilValue)
      SET_STRING_ELT(out_names, 0, STRING_ELT(id_names, id_idx));
    else
      SET_STRING_ELT(out_names, 0, Rf_mkChar("id"));
    UNPROTECT(1);
  }
  for (int32_t c = 0; c < k_out; ++c) {
    SEXP vc = PROTECT(alloc_smart(REALSXP, n_out));
    double*       vp = REAL(vc);
    const double* ap = acc_all.data() + (size_t)c * (size_t)row_stride;
    if (!compact) std::memcpy(vp, ap, (size_t)n_out * sizeof(double));
    else for (int32_t r = 0; r < row_stride; ++r) {
      const int32_t cc = row_map[(size_t)r];
      if (cc >= 0) vp[cc] = ap[r];
    }
    SET_VECTOR_ELT(out, 1 + c, vc);
    if (col_seps[(size_t)c] != R_NilValue) {
      SET_STRING_ELT(out_names, 1 + c, col_seps[(size_t)c]);
      UNPROTECT(1);
    } else {
      SEXP ch = PROTECT(Rf_mkCharLen(col_keys[(size_t)c].data(),
                                     (int)col_keys[(size_t)c].size()));
      SET_STRING_ELT(out_names, 1 + c, ch);
      UNPROTECT(2);
    }
  }
  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = -n_out;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    UNPROTECT(1);
  }
  Rf_setAttrib(out, R_ClassSymbol, cached_df_class());
  UNPROTECT(2);
  return out;
}

} // namespace dcast_agg

__attribute__((noinline, cold))
// [[Rcpp::export]]
SEXP dcast_aggregate_cpp(SEXP data, SEXP id = R_NilValue,
                         SEXP variable = R_NilValue, SEXP value = R_NilValue,
                         int agg_fn = 1, bool na_rm = false, int cores = 0)
{
  (void)cores;
  using namespace dcast_agg;
  if (TYPEOF(data) != VECSXP) stop("data must be a data.frame");
  int ncols = Rf_length(data);
  if (ncols < 2) stop("data must have at least 2 columns");
  init_cpu_features();
  int var_idx = infer_var_val_idx(data, variable, "variable", ncols - 2);
  int val_idx = infer_var_val_idx(data, value,    "value",    ncols - 1);
  if (var_idx < 0) stop("cannot infer 'variable' column");
  if (val_idx < 0) stop("cannot infer 'value' column");
  if (var_idx == val_idx) stop("variable and value must be distinct");
  std::vector<int> id_cols = resolve_id_cols(data, id, var_idx, val_idx);
  int n_id = (int)id_cols.size();
  if (n_id == 0) stop("no id columns");
  SEXP id_names = Rf_getAttrib(data, R_NamesSymbol);
  SEXP var_col  = VECTOR_ELT(data, var_idx);
  SEXP val_col  = VECTOR_ELT(data, val_idx);
  R_xlen_t nlong = XLENGTH(var_col);
  for (int idx : id_cols)
    if (XLENGTH(VECTOR_ELT(data, idx)) != nlong)
      stop("id columns must have same length");
  if (nlong == 0) stop("data has no rows");
  if (nlong > (R_xlen_t)INT32_MAX) stop("input too large");

  const R_xlen_t fused_min_n = (agg_fn <= 4) ? (R_xlen_t)500000
                                             : (R_xlen_t)3000000;
  if (n_id == 1 && agg_fn >= 1 && agg_fn <= 7 && nlong >= fused_min_n) {
    SEXP fused = dcast_aggregate_fused(data, id_cols[0], var_idx, val_idx,
                                       agg_fn, na_rm);
    if (fused != R_NilValue) return fused;
  }

  std::vector<int32_t> col_of((size_t)nlong);
  std::vector<std::string> col_keys;
  std::vector<SEXP> col_seps;
  int32_t k_out = 0;
  {
    SEXPTYPE vt = TYPEOF(var_col);
    if (vt == STRSXP) {
      std::unordered_map<SEXP, int32_t> m; m.reserve(256);
      const SEXP* vs = DCAST_STRING_PTR_RO(var_col);
      for (R_xlen_t i = 0; i < nlong; ++i) {
        SEXP s = vs[i];
        auto it = m.find(s);
        if (it == m.end()) {
          int32_t c = k_out++; m.emplace(s, c);
          col_seps.push_back(s);
          col_keys.emplace_back(s == NA_STRING ? "NA" : CHAR(s));
          col_of[i] = c;
        } else col_of[i] = it->second;
      }
    } else if (vt == INTSXP || vt == LGLSXP) {
      const int* vi = (vt == INTSXP) ? INTEGER(var_col) : LOGICAL(var_col);
      int32_t mn = INT32_MAX, mx = INT32_MIN;
      for (R_xlen_t i = 0; i < nlong; ++i) {
        int v = vi[i];
        if (v == NA_INTEGER) continue;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
      }
      if (mx >= mn && (int64_t)mx - (int64_t)mn < 1000000) {
        std::vector<int32_t> lut((size_t)((int64_t)mx - (int64_t)mn) + 1, -1);
        int32_t na_col = -1;
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int v = vi[i];
          if (v == NA_INTEGER) {
            if (na_col < 0) {
              na_col = k_out++; col_seps.push_back(R_NilValue);
              col_keys.emplace_back("NA");
            }
            col_of[i] = na_col;
          } else {
            int32_t& s = lut[(size_t)((int64_t)v - (int64_t)mn)];
            if (s < 0) {
              s = k_out++; col_seps.push_back(R_NilValue);
              char buf[16]; int l = fast_itoa(v, buf);
              col_keys.emplace_back(buf, l);
            }
            col_of[i] = s;
          }
        }
      } else stop("variable column has too many levels");
    } else stop("unsupported variable column type");
  }
  if (k_out == 0) stop("empty output");

  std::vector<std::vector<int32_t>> owned(n_id);
  std::vector<uint32_t> units(n_id, 1);
  for (int j = 0; j < n_id; ++j) {
    SEXP col = VECTOR_ELT(data, id_cols[j]);
    SEXPTYPE t = TYPEOF(col);
    owned[j].resize((size_t)nlong);
    if (t == INTSXP || t == LGLSXP) {
      const int* v = (t == INTSXP) ? INTEGER(col) : LOGICAL(col);
      int32_t mn = INT32_MAX, mx = INT32_MIN;
      bool has_na = false;
      for (R_xlen_t i = 0; i < nlong; ++i) {
        int x = v[i];
        if (x == NA_INTEGER) { has_na = true; continue; }
        if (x < mn) mn = x;
        if (x > mx) mx = x;
      }
      if (mn > mx) { mn = 0; mx = 0; }
      uint64_t range = (uint64_t)((int64_t)mx - (int64_t)mn) + 1;
      if (range + (has_na ? 1ULL : 0ULL) <= (uint64_t)UINT32_MAX) {
        uint32_t u = (uint32_t)(range + (has_na ? 1ULL : 0ULL));
        units[j] = u;
        int32_t na_code = has_na ? (int32_t)(u - 1) : 0;
        const int32_t mnv = mn;
        int32_t* __restrict__ dst = owned[j].data();
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int32_t x = v[i];
          dst[i] = (x == NA_INTEGER) ? na_code
                                     : (int32_t)((int64_t)x - (int64_t)mnv);
        }
      } else {
        std::unordered_map<int32_t, int32_t> m; m.reserve(4096);
        int32_t card = 0;
        int32_t* __restrict__ dst = owned[j].data();
        for (R_xlen_t i = 0; i < nlong; ++i) {
          int32_t x = v[i];
          auto it = m.find(x);
          if (it == m.end()) { m.emplace(x, card); dst[i] = card++; }
          else               dst[i] = it->second;
        }
        units[j] = (uint32_t)card;
      }
    } else if (t == STRSXP) {
      std::unordered_map<SEXP, int32_t> m; m.reserve(4096);
      int32_t card = 0;
      const SEXP* vs = DCAST_STRING_PTR_RO(col);
      int32_t* __restrict__ dst = owned[j].data();
      for (R_xlen_t i = 0; i < nlong; ++i) {
        auto it = m.find(vs[i]);
        if (it == m.end()) { m.emplace(vs[i], card); dst[i] = card++; }
        else               dst[i] = it->second;
      }
      units[j] = (uint32_t)card;
    } else if (t == REALSXP) {
      std::unordered_map<uint64_t, int32_t> m; m.reserve(4096);
      int32_t card = 0;
      const double* d = REAL(col);
      int32_t* __restrict__ dst = owned[j].data();
      for (R_xlen_t i = 0; i < nlong; ++i) {
        double x = d[i];
        uint64_t bits; std::memcpy(&bits, &x, sizeof(bits));
        if (x == 0.0) bits = 0;
        auto it = m.find(bits);
        if (it == m.end()) { m.emplace(bits, card); dst[i] = card++; }
        else               dst[i] = it->second;
      }
      units[j] = (uint32_t)card;
    } else stop("unsupported id column type");
  }

  uint64_t total_units = 1;
  bool units_ok = true;
  for (int j = 0; j < n_id; ++j) {
    uint64_t u = (uint64_t)units[j];
    if (u == 0 || total_units > (UINT64_MAX / u)) { units_ok = false; break; }
    total_units *= u;
    if (total_units > (uint64_t)20000000ULL) { units_ok = false; break; }
  }
  const bool use_direct = units_ok && (total_units > 0) &&
                          (total_units <= (uint64_t)20000000ULL) &&
                          (total_units <= (uint64_t)nlong * 4ULL);

  std::vector<int32_t> row_of;
  std::vector<int32_t> first_src;
  int32_t n_out = 0;

  if (use_direct) {
    std::vector<int32_t> group_of_dense((size_t)total_units, -1);
    row_of.resize((size_t)nlong);
    first_src.clear();
    first_src.reserve(256);
    int32_t compact = 0;
    if (n_id == 1) {
      const int32_t* __restrict__ o0 = owned[0].data();
      int32_t* __restrict__ rd = row_of.data();
      for (R_xlen_t i = 0; i < nlong; ++i) {
        int32_t& g = group_of_dense[(size_t)o0[i]];
        if (DCAST_UNLIKELY(g < 0)) { g = compact++; first_src.push_back((int32_t)i); }
        rd[i] = g;
      }
    } else {
      int32_t* __restrict__ rd = row_of.data();
      for (R_xlen_t i = 0; i < nlong; ++i) {
        uint64_t r = 0;
        for (int j = 0; j < n_id; ++j)
          r = r * (uint64_t)units[j] + (uint64_t)owned[j][i];
        int32_t& g = group_of_dense[(size_t)r];
        if (DCAST_UNLIKELY(g < 0)) { g = compact++; first_src.push_back((int32_t)i); }
        rd[i] = g;
      }
    }
    n_out = compact;
  } else {
    std::vector<int> shifts(n_id);
    int total_bits = 0;
    bool bits_ok = true;
    {
      int s = 0;
      for (int j = n_id - 1; j >= 0; --j) {
        uint32_t u = units[j];
        int b = (u <= 1) ? 1 : (int)(64 - __builtin_clzll((uint64_t)(u - 1)));
        if (b < 1) b = 1;
        shifts[j] = s; s += b;
        if (s > 64) { bits_ok = false; break; }
      }
      total_bits = s;
    }
    std::vector<uint64_t> keys((size_t)nlong);
    if (bits_ok) {
      if (n_id == 1) {
        const int32_t* __restrict__ o = owned[0].data();
        for (R_xlen_t i = 0; i < nlong; ++i) keys[i] = (uint64_t)o[i];
      } else {
        for (R_xlen_t i = 0; i < nlong; ++i) {
          uint64_t k = 0;
          for (int j = 0; j < n_id; ++j)
            k |= ((uint64_t)owned[j][i]) << shifts[j];
          keys[i] = k;
        }
      }
    } else {
      for (R_xlen_t i = 0; i < nlong; ++i) {
        uint64_t h = 0x9E3779B97F4A7C15ULL;
        for (int j = 0; j < n_id; ++j)
          h = h * 0x9E3779B97F4A7C15ULL + (uint64_t)owned[j][i];
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL; h ^= h >> 33;
        keys[i] = h;
      }
      total_bits = 64;
    }
    std::vector<int32_t> idx((size_t)nlong);
    std::iota(idx.begin(), idx.end(), 0);
    radix_sort_u64_idx_bits(keys.data(), idx, nlong, total_bits);
    row_of.resize((size_t)nlong);
    first_src.reserve(256);
    int32_t run = -1;
    uint64_t prev_key = ~0ULL;
    int32_t prev_src = -1;
    for (R_xlen_t t = 0; t < nlong; ++t) {
      int32_t src = idx[(size_t)t];
      uint64_t k = keys[(size_t)src];
      bool new_group = (t == 0) || (k != prev_key);
      if (!new_group && !bits_ok) {
        for (int j = 0; j < n_id; ++j)
          if (owned[j][src] != owned[j][prev_src]) { new_group = true; break; }
      }
      if (new_group) { ++run; first_src.push_back(src); prev_key = k; }
      row_of[(size_t)src] = run;
      prev_src = src;
    }
    n_out = run + 1;
  }
  if (n_out == 0) stop("empty output");

  SEXPTYPE val_type = TYPEOF(val_col);
  if (val_type != REALSXP && val_type != INTSXP && val_type != LGLSXP)
    stop("aggregation only supports numeric/logical value columns");
  const double* vs = (val_type == REALSXP) ? REAL(val_col) : nullptr;
  const int*    vi = (val_type == INTSXP)  ? INTEGER(val_col) : nullptr;
  const int*    vl = (val_type == LGLSXP)  ? LOGICAL(val_col) : nullptr;

  const bool compress_rows = (n_out <= 65536);
  std::vector<uint16_t> row_codes_u16;
  std::vector<int32_t>  row_codes_i32;
  if (compress_rows) {
    row_codes_u16.resize((size_t)nlong);
    for (R_xlen_t i = 0; i < nlong; ++i) row_codes_u16[i] = (uint16_t)row_of[i];
  } else row_codes_i32 = std::move(row_of);

  const size_t acc_sz = (size_t)k_out * (size_t)n_out;
  std::vector<double>  acc(acc_sz);
  std::vector<int32_t> cnt;
  std::vector<uint8_t> has_na;

  if (agg_fn == 8) {
    std::vector<int32_t> cnt_per_cell(acc_sz, 0);
    std::vector<uint8_t> has_na_cell(acc_sz, 0);

    const uint16_t* rp16_m = compress_rows ? row_codes_u16.data() : nullptr;
    const int32_t*  rp32_m = compress_rows ? nullptr : row_codes_i32.data();
    const int32_t*  cp_m   = col_of.data();

    for (R_xlen_t i = 0; i < nlong; ++i) {
      int32_t c = cp_m[i];
      int32_t r = compress_rows ? (int32_t)rp16_m[i] : rp32_m[i];
      size_t off = (size_t)c * (size_t)n_out + (size_t)r;
      bool is_na = false;
      if (val_type == REALSXP) is_na = ISNAN(vs[i]);
      else if (val_type == INTSXP) is_na = (vi[i] == NA_INTEGER);
      else is_na = (vl[i] == NA_LOGICAL);
      if (is_na) { if (!na_rm) has_na_cell[off] = 1; continue; }
      cnt_per_cell[off]++;
    }

    std::vector<int32_t> offsets(acc_sz + 1, 0);
    for (size_t i = 0; i < acc_sz; ++i)
      offsets[i + 1] = offsets[i] + cnt_per_cell[i];
    const int64_t total_values = (int64_t)offsets[acc_sz];

    std::vector<double> values((size_t)total_values);
    std::vector<int32_t> cursors(offsets.begin(), offsets.end() - 1);
    for (R_xlen_t i = 0; i < nlong; ++i) {
      int32_t c = cp_m[i];
      int32_t r = compress_rows ? (int32_t)rp16_m[i] : rp32_m[i];
      size_t off = (size_t)c * (size_t)n_out + (size_t)r;
      double v; bool is_na = false;
      if (val_type == REALSXP) { v = vs[i]; is_na = ISNAN(v); }
      else if (val_type == INTSXP) {
        int x = vi[i]; is_na = (x == NA_INTEGER);
        v = is_na ? NA_REAL : (double)x;
      } else {
        int x = vl[i]; is_na = (x == NA_LOGICAL);
        v = is_na ? NA_REAL : (x ? 1.0 : 0.0);
      }
      if (is_na && na_rm) continue;
      values[(size_t)cursors[off]++] = v;
    }

    for (size_t off = 0; off < acc_sz; ++off) {
      if (has_na_cell[off]) { acc[off] = NA_REAL; continue; }
      int32_t lo = offsets[off], hi = offsets[off + 1];
      int32_t nn = hi - lo;
      if (nn == 0) { acc[off] = NA_REAL; continue; }
      double* v = values.data() + lo;
      if (nn == 1) { acc[off] = v[0]; continue; }
      if (nn == 2) { acc[off] = 0.5 * (v[0] + v[1]); continue; }
      if (nn <= 16) {
        for (int a = 1; a < nn; ++a) {
          double key = v[a];
          int b = a - 1;
          while (b >= 0 && v[b] > key) { v[b + 1] = v[b]; --b; }
          v[b + 1] = key;
        }
        if (nn & 1) acc[off] = v[nn / 2];
        else acc[off] = 0.5 * (v[nn / 2 - 1] + v[nn / 2]);
      } else {
        if (nn & 1) {
          std::nth_element(v, v + nn / 2, v + nn);
          acc[off] = v[nn / 2];
        } else {
          std::nth_element(v, v + nn / 2 - 1, v + nn);
          double a = v[nn / 2 - 1];
          std::nth_element(v, v + nn / 2, v + nn);
          double b = v[nn / 2];
          acc[off] = 0.5 * (a + b);
        }
      }
    }
  } else {
    const bool need_cnt = (agg_fn == 2) || (agg_fn == 5);
    if (agg_fn == 1 || agg_fn == 2) std::fill(acc.begin(), acc.end(), 0.0);
    else if (agg_fn == 3) std::fill(acc.begin(), acc.end(),
                                    std::numeric_limits<double>::infinity());
    else if (agg_fn == 4) std::fill(acc.begin(), acc.end(),
                                    -std::numeric_limits<double>::infinity());
    else if (agg_fn == 5) std::fill(acc.begin(), acc.end(), 0.0);
    else std::fill(acc.begin(), acc.end(), NA_REAL);
    if (need_cnt) cnt.assign(acc_sz, 0);
    if (agg_fn == 2) has_na.assign(acc_sz, 0);

    const uint16_t* rp16 = compress_rows ? row_codes_u16.data() : nullptr;
    const int32_t*  rp32 = compress_rows ? nullptr : row_codes_i32.data();
    const int32_t*  cp   = col_of.data();

    if (compress_rows) {
      switch (agg_fn) {
      case 1: {
        if (vs) accum_sum<uint16_t, double>(rp16, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_sum<uint16_t, int>(rp16, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_sum<uint16_t, int>(rp16, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 2: {
        if (vs) accum_mean<uint16_t, double>(rp16, cp, vs, acc.data(), cnt.data(), has_na.data(), n_out, nlong, na_rm);
        else if (vi) accum_mean<uint16_t, int>(rp16, cp, vi, acc.data(), cnt.data(), has_na.data(), n_out, nlong, na_rm);
        else accum_mean<uint16_t, int>(rp16, cp, vl, acc.data(), cnt.data(), has_na.data(), n_out, nlong, na_rm);
        break;
      }
      case 3: {
        if (vs) accum_min<uint16_t, double>(rp16, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_min<uint16_t, int>(rp16, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_min<uint16_t, int>(rp16, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 4: {
        if (vs) accum_max<uint16_t, double>(rp16, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_max<uint16_t, int>(rp16, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_max<uint16_t, int>(rp16, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 5: {
        accum_length<uint16_t>(rp16, cp, cnt.data(), n_out, nlong);
        break;
      }
      case 6: {
        if (vs) accum_first<uint16_t, double>(rp16, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_first<uint16_t, int>(rp16, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_first<uint16_t, int>(rp16, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 7: {
        if (vs) accum_last<uint16_t, double>(rp16, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_last<uint16_t, int>(rp16, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_last<uint16_t, int>(rp16, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      default: break;
      }
    } else {
      switch (agg_fn) {
      case 1: {
        if (vs) accum_sum<int32_t, double>(rp32, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_sum<int32_t, int>(rp32, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_sum<int32_t, int>(rp32, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 2: {
        if (vs) accum_mean<int32_t, double>(rp32, cp, vs, acc.data(), cnt.data(), has_na.data(), n_out, nlong, na_rm);
        else if (vi) accum_mean<int32_t, int>(rp32, cp, vi, acc.data(), cnt.data(), has_na.data(), n_out, nlong, na_rm);
        else accum_mean<int32_t, int>(rp32, cp, vl, acc.data(), cnt.data(), has_na.data(), n_out, nlong, na_rm);
        break;
      }
      case 3: {
        if (vs) accum_min<int32_t, double>(rp32, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_min<int32_t, int>(rp32, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_min<int32_t, int>(rp32, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 4: {
        if (vs) accum_max<int32_t, double>(rp32, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_max<int32_t, int>(rp32, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_max<int32_t, int>(rp32, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 5: {
        accum_length<int32_t>(rp32, cp, cnt.data(), n_out, nlong);
        break;
      }
      case 6: {
        if (vs) accum_first<int32_t, double>(rp32, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_first<int32_t, int>(rp32, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_first<int32_t, int>(rp32, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      case 7: {
        if (vs) accum_last<int32_t, double>(rp32, cp, vs, acc.data(), n_out, nlong, na_rm);
        else if (vi) accum_last<int32_t, int>(rp32, cp, vi, acc.data(), n_out, nlong, na_rm);
        else accum_last<int32_t, int>(rp32, cp, vl, acc.data(), n_out, nlong, na_rm);
        break;
      }
      default: break;
      }
    }

    if (agg_fn == 2)
      for (size_t i = 0; i < acc.size(); ++i)
        acc[i] = (has_na[i] || cnt[i] == 0) ? NA_REAL : acc[i] / (double)cnt[i];
    else if (agg_fn == 5)
      for (size_t i = 0; i < acc.size(); ++i) acc[i] = (double)cnt[i];
    else if (agg_fn == 3 || agg_fn == 4)
      for (size_t i = 0; i < acc.size(); ++i)
        if (std::isinf(acc[i])) acc[i] = NA_REAL;
  }

  int total_out_cols = n_id + k_out;
  SEXP out       = PROTECT(Rf_allocVector(VECSXP, total_out_cols));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, total_out_cols));
  for (int j = 0; j < n_id; ++j) {
    SEXP src = VECTOR_ELT(data, id_cols[j]);
    SEXPTYPE t = TYPEOF(src);
    SEXP dst = PROTECT(Rf_allocVector(t, n_out));
    Rf_copyMostAttrib(src, dst);
    if (t == INTSXP) {
      const int* s = INTEGER(src); int* d = INTEGER(dst);
      for (int32_t r = 0; r < n_out; ++r) d[r] = s[first_src[(size_t)r]];
    } else if (t == LGLSXP) {
      const int* s = LOGICAL(src); int* d = LOGICAL(dst);
      for (int32_t r = 0; r < n_out; ++r) d[r] = s[first_src[(size_t)r]];
    } else if (t == REALSXP) {
      const double* s = REAL(src); double* d = REAL(dst);
      for (int32_t r = 0; r < n_out; ++r) d[r] = s[first_src[(size_t)r]];
    } else {
      const SEXP* s = DCAST_STRING_PTR_RO(src);
      for (int32_t r = 0; r < n_out; ++r)
        SET_STRING_ELT(dst, r, s[first_src[(size_t)r]]);
    }
    SET_VECTOR_ELT(out, j, dst);
    SET_STRING_ELT(out_names, j, STRING_ELT(id_names, id_cols[j]));
    UNPROTECT(1);
  }
  for (int32_t c = 0; c < k_out; ++c) {
    SEXP vc = PROTECT(Rf_allocVector(REALSXP, n_out));
    double* vp = REAL(vc);
    const double* ap = acc.data() + (size_t)c * (size_t)n_out;
    for (int32_t r = 0; r < n_out; ++r) vp[r] = ap[r];
    SET_VECTOR_ELT(out, n_id + c, vc);
    if (col_seps[(size_t)c] != R_NilValue) {
      SET_STRING_ELT(out_names, n_id + c, col_seps[(size_t)c]);
      UNPROTECT(1);
    } else {
      SEXP ch = PROTECT(Rf_mkCharLen(col_keys[(size_t)c].data(),
                                     (int)col_keys[(size_t)c].size()));
      SET_STRING_ELT(out_names, n_id + c, ch);
      UNPROTECT(2);
    }
  }
  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = -n_out;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    UNPROTECT(1);
  }
  Rf_setAttrib(out, R_ClassSymbol, cached_df_class());
  UNPROTECT(2);
  return out;
}

// =====================================================================
// [13] Fast-path core: id-gather + transpose
// =====================================================================
static SEXP dcast_core(
    SEXP data, int var_idx, int val_idx,
    std::vector<int>& id_cols,
    double fill_val, bool use_fill, bool na_rm,
    int cores)
{
  init_cpu_features();

  SEXP id_names = Rf_getAttrib(data, R_NamesSymbol);
  SEXP var_col  = VECTOR_ELT(data, var_idx);
  SEXP val_col  = VECTOR_ELT(data, val_idx);
  const int n_id = (int)id_cols.size();
  R_xlen_t nlong = XLENGTH(var_col);

  if (nlong <= 16384) {
    SEXP r = dcast_tiny_simd(data, var_idx, val_idx, id_cols,
                             nlong, use_fill, fill_val, na_rm);
    if (r != R_NilValue) return r;
  }

  hint_readonly(var_col);
  hint_readonly(val_col);
  for (int idx : id_cols) hint_readonly(VECTOR_ELT(data, idx));

  int  actual_threads = 1;
#ifdef _OPENMP
  bool use_parallel = false;
  int hw = omp_get_max_threads();
  int target = 1;
  if (cores > 0) {
    target = std::max(1, std::min(cores, hw));
  } else if (nlong >= (R_xlen_t)200000 && hw > 1) {
    R_xlen_t calc = nlong / 200000;
    target = (int)std::min<R_xlen_t>((R_xlen_t)hw, calc);
    target = std::max(1, target);
  }
  if (const char* e = getenv("OMP_THREAD_LIMIT")) {
    int v = atoi(e);
    if (v > 0 && v < target) target = v;
  }
  if (const char* e = getenv("DATAPREP_THREADS")) {
    int v = atoi(e);
    if (v > 0 && v < target) target = v;
  }
  actual_threads = target;
  use_parallel = (actual_threads > 1) && (nlong > 50000);
  if (use_parallel) {
    omp_set_dynamic(0);
    omp_set_num_threads(actual_threads);
  }
#else
  (void)cores;
#endif

  bool var_is_factor = Rf_isFactor(var_col);
  SEXP var_levels = var_is_factor
                    ? Rf_getAttrib(var_col, R_LevelsSymbol) : R_NilValue;
  SEXPTYPE var_type = TYPEOF(var_col);
  const int*    var_int = (var_type == INTSXP)  ? INTEGER(var_col) : nullptr;
  const int*    var_lgl = (var_type == LGLSXP)  ? LOGICAL(var_col) : nullptr;
  const double* var_dbl = (var_type == REALSXP) ? REAL(var_col)    : nullptr;
  const int*    var_fac = var_is_factor ? INTEGER(var_col) : nullptr;

  R_xlen_t period = 0;
  {
    auto veq = [&](R_xlen_t a, R_xlen_t b) -> bool {
      if (var_type == STRSXP)
        return STRING_ELT(var_col, a) == STRING_ELT(var_col, b);
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
    R_xlen_t lim = std::min<R_xlen_t>(nlong, (R_xlen_t)65536);
    for (R_xlen_t i = 1; i < lim; ++i)
      if (veq(i, 0)) { period = i; break; }
    if (period > 0 && (nlong % period) == 0) {
      R_xlen_t step = std::max<R_xlen_t>(1, nlong / 4096);
      for (R_xlen_t i = 0; i < nlong; i += step)
        if (!veq(i, i % period)) { period = 0; break; }
    } else period = 0;
  }
  bool block_path = (period > 0);

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
        const SEXP* s = DCAST_STRING_PTR_RO(col);
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
      } else period = 0;
    }
    if (period > 0) {
      bool dup = false;
      if (var_type == STRSXP) {
        std::unordered_set<SEXP> seen;
        seen.reserve((size_t)period * 2);
        for (R_xlen_t k = 0; k < period; ++k) {
          SEXP s = STRING_ELT(var_col, k);
          if (!seen.insert(s).second) { dup = true; break; }
        }
      } else if (var_is_factor) {
        std::unordered_set<int> seen;
        seen.reserve((size_t)period * 2);
        for (R_xlen_t k = 0; k < period; ++k) {
          int code = var_fac[k];
          int key = (code == NA_INTEGER) ? NA_INTEGER : code;
          if (!seen.insert(key).second) { dup = true; break; }
        }
      } else if (var_type == INTSXP) {
        std::unordered_set<int> seen;
        seen.reserve((size_t)period * 2);
        for (R_xlen_t k = 0; k < period; ++k) {
          int v = var_int[k];
          if (!seen.insert(v).second) { dup = true; break; }
        }
      } else if (var_type == LGLSXP) {
        std::unordered_set<int> seen;
        seen.reserve((size_t)period * 2);
        for (R_xlen_t k = 0; k < period; ++k) {
          int v = var_lgl[k];
          if (!seen.insert(v).second) { dup = true; break; }
        }
      } else if (var_type == REALSXP) {
        std::unordered_set<uint64_t> seen;
        seen.reserve((size_t)period * 2);
        for (R_xlen_t k = 0; k < period; ++k) {
          double x = var_dbl[k];
          uint64_t bits;
          std::memcpy(&bits, &x, sizeof(bits));
          if (x == 0.0) bits = 0;
          if (!seen.insert(bits).second) { dup = true; break; }
        }
      }
      if (dup) period = 0;
    }
    block_path = (period > 0);
  }

  bool perm_shortcut = false;
  std::vector<int32_t> perm_first_src;
  int32_t perm_n_out = 0;

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
      R_xlen_t b = 0;
      for (; b + 4 <= n_blk; b += 4) {
        int32_t x0 = v[b * period];
        int32_t x1 = v[(b + 1) * period];
        int32_t x2 = v[(b + 2) * period];
        int32_t x3 = v[(b + 3) * period];
        if (DCAST_UNLIKELY(x0 == NA_INTEGER || x1 == NA_INTEGER ||
            x2 == NA_INTEGER || x3 == NA_INTEGER)) {
          has_na = true;
          break;
        }
        mn = std::min(mn, std::min(std::min(x0, x1), std::min(x2, x3)));
        mx = std::max(mx, std::max(std::max(x0, x1), std::max(x2, x3)));
      }
      if (DCAST_UNLIKELY(has_na)) continue;
      for (; b < n_blk; ++b) {
        int32_t x = v[b * period];
        if (DCAST_UNLIKELY(x == NA_INTEGER)) { has_na = true; break; }
        mn = std::min(mn, x);
        mx = std::max(mx, x);
      }
      if (DCAST_UNLIKELY(has_na)) continue;

      int64_t range = (int64_t)mx - (int64_t)mn + 1;
      if (range <= 0 || range > (int64_t)n_blk * 4) continue;

      if (range == (int64_t)n_blk) {
        bool sorted = true;
        for (R_xlen_t b2 = 1; b2 < n_blk; ++b2) {
          if (v[b2 * period] <= v[(b2 - 1) * period]) {
            sorted = false; break;
          }
        }
        if (sorted) {
          perm_first_src.resize((size_t)n_blk);
          for (R_xlen_t b2 = 0; b2 < n_blk; ++b2)
            perm_first_src[(size_t)b2] =
              (int32_t)((int64_t)b2 * (int64_t)period);
          perm_n_out = (int32_t)n_blk;
          perm_shortcut = true;
          continue;
        }
        perm_first_src.assign((size_t)n_blk, 0);
        for (R_xlen_t b2 = 0; b2 < n_blk; ++b2) {
          int32_t code = v[b2 * period] - mn;
          perm_first_src[(size_t)code] =
            (int32_t)((int64_t)b2 * (int64_t)period);
        }
        perm_n_out = (int32_t)n_blk;
        perm_shortcut = true;
        continue;
      }

      std::vector<int32_t> lut((size_t)range, -1);
      std::vector<int32_t> codes((size_t)n_blk);
      int32_t next_code = 0;
      bool ok = true;
      for (R_xlen_t b2 = 0; b2 < n_blk; ++b2) {
        int32_t x = v[b2 * period] - mn;
        if (x < 0 || x >= range) { ok = false; break; }
        int32_t& slot = lut[(size_t)x];
        if (slot < 0) slot = next_code++;
        codes[(size_t)b2] = slot;
      }
      if (!ok || (R_xlen_t)next_code != n_blk) continue;
      perm_first_src.assign((size_t)n_blk, 0);
      for (R_xlen_t b2 = 0; b2 < n_blk; ++b2)
        perm_first_src[(size_t)codes[(size_t)b2]] =
          (int32_t)((int64_t)b2 * (int64_t)period);
      perm_n_out = (int32_t)n_blk;
      perm_shortcut = true;
    }
    if (!perm_shortcut && n_blk >= 1024) {
      if (try_dense_radix_partition(data, id_cols, n_blk, period,
                                    perm_first_src, perm_n_out))
        perm_shortcut = true;
    }
  }

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
          owned[j].resize((size_t)id_n);
          for (R_xlen_t b = 0; b < id_n; ++b) {
            int32_t x = v[id_index(b)];
            int32_t code;
            if (x == NA_INTEGER) code = has_na ? (int32_t)(units - 1) : 0;
            else                 code = (int32_t)((int64_t)x - (int64_t)mn);
            owned[j][b] = code;
          }
          d.code = owned[j].data();
          d.units = (uint32_t)units;
          if (mn == 0 && !has_na &&
              (R_xlen_t)units == id_n) d.dense = true;
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
      } else stop("unsupported id column type");
    }
  }

  std::vector<std::string> col_keys;
  std::vector<SEXP> col_seps;
  col_keys.reserve(256);
  col_seps.reserve(256);
  std::vector<int32_t> col_of;
  int32_t k_out = 0;

  if (block_path) {
    char buf[64];
    for (R_xlen_t k = 0; k < period; ++k) {
      if (var_is_factor) {
        int code = var_fac[k];
        SEXP s = (code == NA_INTEGER)
                   ? NA_STRING : STRING_ELT(var_levels, code - 1);
        col_seps.push_back(s);
        col_keys.emplace_back(code == NA_INTEGER ? "NA" : CHAR(s));
      } else if (var_type == INTSXP) {
        col_seps.push_back(R_NilValue);
        char b[16]; int l = fast_itoa(var_int[k], b);
        col_keys.emplace_back(b, l);
      } else if (var_type == LGLSXP) {
        col_seps.push_back(R_NilValue);
        int v = var_lgl[k];
        col_keys.emplace_back(v == NA_LOGICAL ? "NA" : (v ? "TRUE" : "FALSE"));
      } else if (var_type == REALSXP) {
        col_seps.push_back(R_NilValue);
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
        col_seps.push_back(s);
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
          SEXP s = (code == NA_INTEGER)
                     ? NA_STRING : STRING_ELT(var_levels, code - 1);
          col_seps.push_back(s);
          col_keys.emplace_back(code == NA_INTEGER ? "NA" : CHAR(s));
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
          col_seps.push_back(s);
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
            if (na_col < 0) {
              na_col = k_out++;
              col_seps.push_back(R_NilValue);
              col_keys.emplace_back("NA");
            }
            col_of[i] = na_col;
          } else {
            int32_t& s = lut[(size_t)((int64_t)v - (int64_t)mn)];
            if (s < 0) {
              s = k_out++;
              col_seps.push_back(R_NilValue);
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
            col_seps.push_back(R_NilValue);
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
          col_seps.push_back(R_NilValue);
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
          if (N < 0) {
            N = k_out++;
            col_seps.push_back(R_NilValue);
            col_keys.emplace_back("NA");
          }
          col_of[i] = N;
        } else if (v) {
          if (T < 0) {
            T = k_out++;
            col_seps.push_back(R_NilValue);
            col_keys.emplace_back("TRUE");
          }
          col_of[i] = T;
        } else {
          if (F < 0) {
            F = k_out++;
            col_seps.push_back(R_NilValue);
            col_keys.emplace_back("FALSE");
          }
          col_of[i] = F;
        }
      }
    } else stop("unsupported variable column type");
  }
  if (k_out == 0) stop("empty output");

  std::vector<int32_t> row_of;
  std::vector<int32_t> first_src;
  int32_t n_out = 0;

  bool bits_overflow = false;
  std::vector<int> shifts(n_id);
  int total_bits = 0;
  {
    int s = 0;
    for (int j = n_id - 1; j >= 0; --j) {
      uint32_t u = desc[j].units;
      int b = (u <= 1) ? 1 : (int)(64 - __builtin_clzll((uint64_t)(u - 1)));
      if (b < 1) b = 1;
      shifts[j] = s; s += b;
      if (s > 64) { bits_overflow = true; break; }
    }
    total_bits = s;
    if (s > 64) bits_overflow = true;
  }

  if (block_path) {
    R_xlen_t n_blocks = nlong / period;
    first_src.reserve((size_t)n_blocks);
    if (perm_shortcut) {
      first_src = std::move(perm_first_src);
      n_out = perm_n_out;
    } else if (!bits_overflow) {
      switch (n_id) {
      case 1: {
        bool use_hash = (n_blocks >= 4096) &&
                        (desc[0].units * 4 < (uint32_t)n_blocks);
        if (use_hash) {
          build_block_phase1_hash(desc, 1, n_blocks, period,
                                  first_src, n_out);
        } else {
          build_block_phase1_sorted_n1_fast(desc, n_blocks, period,
                                            actual_threads,
                                            first_src, n_out);
        }
        break;
      }
      case 2: build_block_phase1_sorted(KeyFnN<2>{desc.data(), shifts.data()},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      case 3: build_block_phase1_sorted(KeyFnN<3>{desc.data(), shifts.data()},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      case 4: build_block_phase1_sorted(KeyFnN<4>{desc.data(), shifts.data()},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      case 5: build_block_phase1_sorted(KeyFnN<5>{desc.data(), shifts.data()},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      case 6: build_block_phase1_sorted(KeyFnN<6>{desc.data(), shifts.data()},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      case 7: build_block_phase1_sorted(KeyFnN<7>{desc.data(), shifts.data()},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      case 8: build_block_phase1_sorted(KeyFnN<8>{desc.data(), shifts.data()},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      default: build_block_phase1_sorted(
          KeyFnDyn{desc.data(), shifts.data(), n_id},
          n_blocks, period, actual_threads, total_bits, first_src, n_out); break;
      }
    } else {
      build_block_phase1_radix(desc, n_id, n_blocks, period,
                               first_src, n_out);
    }
  } else {
    row_of.assign((size_t)nlong, -1);
    first_src.reserve((size_t)std::min<R_xlen_t>(nlong, (R_xlen_t)1 << 22));
    if (!bits_overflow) {
      switch (n_id) {
      case 1: build_general_phase1_sorted_n1_fast(
                  desc, nlong, row_of, first_src, n_out); break;
      case 2: build_general_phase1_sorted(KeyFnN<2>{desc.data(), shifts.data()},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      case 3: build_general_phase1_sorted(KeyFnN<3>{desc.data(), shifts.data()},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      case 4: build_general_phase1_sorted(KeyFnN<4>{desc.data(), shifts.data()},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      case 5: build_general_phase1_sorted(KeyFnN<5>{desc.data(), shifts.data()},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      case 6: build_general_phase1_sorted(KeyFnN<6>{desc.data(), shifts.data()},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      case 7: build_general_phase1_sorted(KeyFnN<7>{desc.data(), shifts.data()},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      case 8: build_general_phase1_sorted(KeyFnN<8>{desc.data(), shifts.data()},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      default: build_general_phase1_sorted(
          KeyFnDyn{desc.data(), shifts.data(), n_id},
          nlong, actual_threads, total_bits, row_of, first_src, n_out); break;
      }
    } else {
      build_general_phase1_radix(desc, n_id, nlong,
                                 row_of, first_src, n_out);
    }
  }
  if (n_out == 0) stop("empty output");

  int total_out_cols = n_id + k_out;
  SEXP out       = PROTECT(Rf_allocVector(VECSXP, total_out_cols));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, total_out_cols));
  const int32_t* fs = first_src.data();

  std::vector<SEXP> id_dst_sext(n_id);
  std::vector<const void*> id_src_ptr(n_id);
  std::vector<void*>       id_dst_ptr(n_id);
  for (int j = 0; j < n_id; ++j) {
    SEXP src = VECTOR_ELT(data, id_cols[j]);
    SEXPTYPE t = TYPEOF(src);
    SEXP dst = PROTECT(alloc_smart(t, n_out));
    Rf_copyMostAttrib(src, dst);
    id_dst_sext[j] = dst;
    switch (t) {
    case INTSXP:  id_src_ptr[j] = (const void*)INTEGER(src);
                  id_dst_ptr[j] = (void*)INTEGER(dst); break;
    case LGLSXP:  id_src_ptr[j] = (const void*)LOGICAL(src);
                  id_dst_ptr[j] = (void*)LOGICAL(dst); break;
    case REALSXP: id_src_ptr[j] = (const void*)REAL(src);
                  id_dst_ptr[j] = (void*)REAL(dst); break;
    case STRSXP:  id_src_ptr[j] = (const void*)DCAST_STRING_PTR_RO(src);
                  id_dst_ptr[j] = nullptr; break;
    default:      id_src_ptr[j] = nullptr; id_dst_ptr[j] = nullptr;
    }
    SET_VECTOR_ELT(out, j, dst);
    SET_STRING_ELT(out_names, j, STRING_ELT(id_names, id_cols[j]));
    UNPROTECT(1);
  }

  SEXPTYPE val_type = TYPEOF(val_col);
  if (val_type != REALSXP && val_type != INTSXP && val_type != LGLSXP)
    stop("dcast_cpp only supports numeric/logical value columns");

  const bool dense = ((R_xlen_t)n_out * (R_xlen_t)k_out == nlong);

  std::vector<double*> out_val((size_t)k_out);
  for (int k = 0; k < k_out; ++k) {
    SEXP vc = PROTECT(alloc_smart(REALSXP, n_out));
    out_val[(size_t)k] = REAL(vc);
    SET_VECTOR_ELT(out, n_id + k, vc);

    if (col_seps[(size_t)k] != R_NilValue) {
      SET_STRING_ELT(out_names, n_id + k, col_seps[(size_t)k]);
      UNPROTECT(1);
    } else {
      SEXP ch = PROTECT(Rf_mkCharLen(col_keys[(size_t)k].data(),
                                     (int)col_keys[(size_t)k].size()));
      SET_STRING_ELT(out_names, n_id + k, ch);
      UNPROTECT(2);
    }
  }

  const double* vs = (val_type == REALSXP) ? REAL(val_col) : nullptr;
  const int*    vi = (val_type == INTSXP)  ? INTEGER(val_col) : nullptr;
  const int*    vl = (val_type == LGLSXP)  ? LOGICAL(val_col) : nullptr;

  auto do_id_gather = [&]() {
    if (n_id >= 1 && n_out >= 4096 && g_have_avx512_gather) {
      bool all_int = true;
      for (int j = 0; j < n_id; ++j) {
        SEXPTYPE t = TYPEOF(VECTOR_ELT(data, id_cols[j]));
        if (t != INTSXP && t != LGLSXP) { all_int = false; break; }
      }
      if (all_int) {
        for (int j = 0; j < n_id; ++j) {
          gather_i32_avx512((int*)id_dst_ptr[j],
                            (const int*)id_src_ptr[j],
                            fs, n_out);
        }
        return;
      }
      bool all_dbl = true;
      for (int j = 0; j < n_id; ++j) {
        if (TYPEOF(VECTOR_ELT(data, id_cols[j])) != REALSXP) {
          all_dbl = false; break;
        }
      }
      if (all_dbl && n_id >= 1) {
        for (int j = 0; j < n_id; ++j) {
          gather_f64_avx512((double*)id_dst_ptr[j],
                            (const double*)id_src_ptr[j],
                            fs, n_out);
        }
        return;
      }
    }
    if (n_id >= 2 && n_out >= 20000 && actual_threads > 1 &&
        id_src_ptr[0] &&
        (TYPEOF(VECTOR_ELT(data, id_cols[0])) == INTSXP ||
         TYPEOF(VECTOR_ELT(data, id_cols[0])) == LGLSXP)) {
      std::vector<const int*> s(n_id); std::vector<int*> d(n_id);
      for (int j = 0; j < n_id; ++j) {
        s[j] = (const int*)id_src_ptr[j];
        d[j] = (int*)id_dst_ptr[j];
      }
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads)
#endif
      for (int j = 0; j < n_id; ++j) {
        const int* __restrict__ sj = s[j];
        int* __restrict__ dj = d[j];
        if (!sj || !dj) continue;
        for (int32_t i = 0; i < n_out; ++i) dj[i] = sj[fs[i]];
      }
      return;
    }
    for (int j = 0; j < n_id; ++j) {
      SEXP src = VECTOR_ELT(data, id_cols[j]);
      SEXPTYPE t = TYPEOF(src);
      if (t == STRSXP) {
        const SEXP* s = (const SEXP*)id_src_ptr[j];
        for (int32_t i = 0; i < n_out; ++i)
          SET_STRING_ELT(id_dst_sext[j], i, s[fs[i]]);
      } else if (t == INTSXP || t == LGLSXP) {
        const int* s = (const int*)id_src_ptr[j];
        int* d = (int*)id_dst_ptr[j];
        for (int32_t i = 0; i < n_out; ++i) d[i] = s[fs[i]];
      } else if (t == REALSXP) {
        const double* s = (const double*)id_src_ptr[j];
        double* d = (double*)id_dst_ptr[j];
        for (int32_t i = 0; i < n_out; ++i) d[i] = s[fs[i]];
      }
    }
  };

  do_id_gather();

  if (block_path) {
    const R_xlen_t n_blocks = (R_xlen_t)n_out;
    const int kk = (int)k_out;
    const int period_i = (int)period;
    const bool merged_direct = dense && !na_rm && !use_fill;

    if (merged_direct && val_type == REALSXP) {
      bool fs_arith = false;
      if (n_blocks >= 2) {
        int32_t step = fs[1] - fs[0];
        if (step == (int32_t)period_i) {
          fs_arith = true;
          const R_xlen_t m = std::min<R_xlen_t>(n_blocks, (R_xlen_t)64);
          for (R_xlen_t i = 2; i < m; ++i) {
            if (fs[(size_t)i] - fs[(size_t)(i - 1)] != step) {
              fs_arith = false; break;
            }
          }
        }
      }

      if (fs_arith && period_i <= 64 && kk <= 256) {
        transpose_column_stream(fs, vs, out_val.data(),
                                (int32_t)n_blocks, kk, actual_threads);
      } else if (period_i == 16 && g_have_avx512) {
        DCAST_DISPATCH_PERIOD(period_i, transpose_16row_avx512,
                              fs, vs, out_val.data(),
                              n_blocks, kk, period_i, actual_threads);
      } else if (period_i >= 8 && period_i <= 32 && g_have_avx512) {
        transpose_8x8_rows(fs, vs, out_val.data(), n_blocks, kk,
                           period_i, actual_threads, false, NA_REAL);
      } else if (period_i > 128 && n_blocks > 0) {
        scatter_block_tiled(fs, vs, out_val.data(),
                            (int32_t)n_blocks, period_i, actual_threads);
      } else {
        transpose_tile(fs, (const void*)vs, out_val.data(),
                       n_blocks, kk, period_i, actual_threads, 0,
                       false, NA_REAL);
      }
    } else {
      bool need_fill = (!dense) || na_rm;
      double fv = use_fill ? fill_val : NA_REAL;
      if (need_fill)
        for (int k = 0; k < k_out; ++k)
          fill_double_ptr(out_val[(size_t)k], fv, (size_t)n_out);
      if (val_type == REALSXP) {
        transpose_tile(fs, (const void*)vs, out_val.data(),
                       n_blocks, kk, period_i, actual_threads, 0, na_rm, fv);
      } else if (val_type == INTSXP) {
        transpose_tile(fs, (const void*)vi, out_val.data(),
                       n_blocks, kk, period_i, actual_threads, 1, na_rm, fv);
      } else {
        transpose_tile(fs, (const void*)vl, out_val.data(),
                       n_blocks, kk, period_i, actual_threads, 2, na_rm, fv);
      }
    }
  } else {
    bool need_fill = (!dense) || na_rm;
    double fv = use_fill ? fill_val : NA_REAL;
    if (need_fill)
      for (int k = 0; k < k_out; ++k)
        fill_double_ptr(out_val[(size_t)k], fv, (size_t)n_out);

    const int32_t* rp = row_of.data();
    const int32_t* cp = col_of.data();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
    for (R_xlen_t i = 0; i < nlong; ++i) {
      int32_t c = cp[i];
      int32_t r = rp[i];
      if (val_type == REALSXP) {
        double v = vs[i];
        if (na_rm && ISNAN(v)) continue;
        out_val[(size_t)c][r] = v;
      } else if (val_type == INTSXP) {
        int v = vi[i];
        if (na_rm && v == NA_INTEGER) continue;
        out_val[(size_t)c][r] = (v == NA_INTEGER) ? NA_REAL : (double)v;
      } else {
        int v = vl[i];
        if (na_rm && v == NA_LOGICAL) continue;
        out_val[(size_t)c][r] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
      }
    }
  }

  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = -n_out;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    UNPROTECT(1);
  }
  Rf_setAttrib(out, R_ClassSymbol, cached_df_class());
  UNPROTECT(2);
  return out;
}

// =====================================================================
// [14] Public entry points
// =====================================================================
// [[Rcpp::export]]
SEXP dcast_fast_cpp(SEXP data, int var_idx, int val_idx,
                    SEXP id_idx, SEXP fill, int cores) {
  if (TYPEOF(data) != VECSXP) stop("data must be a data.frame");

  std::vector<int> id_cols;
  if (TYPEOF(id_idx) == INTSXP) {
    R_xlen_t len = XLENGTH(id_idx);
    id_cols.reserve((size_t)len);
    for (R_xlen_t i = 0; i < len; ++i) {
      int a = INTEGER(id_idx)[i] - 1;
      if (a < 0 || a >= Rf_length(data))
        stop("dcast_fast_cpp: id index out of range");
      id_cols.push_back(a);
    }
  } else if (TYPEOF(id_idx) == REALSXP) {
    R_xlen_t len = XLENGTH(id_idx);
    id_cols.reserve((size_t)len);
    for (R_xlen_t i = 0; i < len; ++i) {
      int a = (int)REAL(id_idx)[i] - 1;
      if (a < 0 || a >= Rf_length(data))
        stop("dcast_fast_cpp: id index out of range");
      id_cols.push_back(a);
    }
  } else {
    stop("dcast_fast_cpp: id_idx must be integer or numeric");
  }
  if (id_cols.empty()) stop("dcast_fast_cpp: no id columns");
  if (var_idx < 1 || var_idx > Rf_length(data))
    stop("dcast_fast_cpp: var_idx out of range");
  if (val_idx < 1 || val_idx > Rf_length(data))
    stop("dcast_fast_cpp: val_idx out of range");

  double fill_val = NA_REAL;
  bool use_fill = parse_fill(fill, fill_val);

  return dcast_core(data, var_idx - 1, val_idx - 1,
                    id_cols, fill_val, use_fill, false, cores);
}

// [[Rcpp::export]]
SEXP dcast_cpp(SEXP data, SEXP id = R_NilValue,
               SEXP variable = R_NilValue, SEXP value = R_NilValue,
               SEXP variable_name = R_NilValue,
               int cores = 0,
               SEXP fill = R_NilValue,
               bool na_rm = false) {
  (void)variable_name;

  if (TYPEOF(data) != VECSXP) stop("data must be a data.frame");
  int ncols = Rf_length(data);
  if (ncols < 2) stop("data must have at least 2 columns");

  int var_idx = infer_var_val_idx(data, variable, "variable", ncols - 2);
  int val_idx = infer_var_val_idx(data, value,    "value",    ncols - 1);
  if (var_idx < 0) stop("cannot infer 'variable' column; specify explicitly");
  if (val_idx < 0) stop("cannot infer 'value' column; specify explicitly");
  if (var_idx == val_idx) stop("variable and value must be distinct columns");

  std::vector<int> id_cols = resolve_id_cols(data, id, var_idx, val_idx);
  if (id_cols.empty()) stop("no id columns");

  SEXP var_col  = VECTOR_ELT(data, var_idx);
  R_xlen_t nlong = XLENGTH(var_col);
  for (int idx : id_cols)
    if (XLENGTH(VECTOR_ELT(data, idx)) != nlong)
      stop("id columns must have the same length as variable/value");
  if (nlong == 0) stop("data has no rows");
  if (nlong > (R_xlen_t)INT32_MAX)
    stop("dcast_cpp: input too large (nlong > INT32_MAX)");

  double fill_val = NA_REAL;
  bool use_fill = parse_fill(fill, fill_val);

  return dcast_core(data, var_idx, val_idx, id_cols,
                    fill_val, use_fill, na_rm, cores);
}
