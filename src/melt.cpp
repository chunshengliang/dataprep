// [[Rcpp::plugins(cpp17)]]
// [[Rcpp::plugins(openmp)]]
#include <Rcpp.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <immintrin.h>
#include <string>
#include <cctype>
#include <unordered_map>
#include <atomic>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace Rcpp;

namespace {

// ============================================================
// 0. Cached SEXP + L3
// ============================================================
struct CachedStr {
  SEXP v = R_NilValue;
  SEXP operator()(const char* s) {
    if (__builtin_expect(v == R_NilValue, 0)) {
      v = Rf_mkString(s);
      R_PreserveObject(v);
    }
    return v;
  }
};
static CachedStr g_df_class;
static CachedStr g_fac_class;
static CachedStr g_variable_name;
static CachedStr g_value_name;

static size_t g_l3_size = 0;
static void detect_l3_size_once() __attribute__((cold));
static void detect_l3_size_once() {
  if (g_l3_size != 0) return;
  size_t l3 = 32ULL << 20;
#ifdef __linux__
  FILE* f = fopen("/sys/devices/system/cpu/cpu0/cache/index3/size", "r");
  if (__builtin_expect(f != nullptr, 1)) {
    char buf[32] = {0};
    if (fgets(buf, sizeof(buf), f)) {
      char* p = buf;
      while (*p && !isdigit((unsigned char)*p)) ++p;
      unsigned long long v = strtoull(p, nullptr, 10);
      if (strchr(p, 'M') || strchr(p, 'm')) v <<= 20;
      else if (strchr(p, 'K') || strchr(p, 'k')) v <<= 10;
      if (v >= (1ULL << 20)) l3 = (size_t)v;
    }
    fclose(f);
  }
#endif
  g_l3_size = l3;
}

struct LevelsKey {
  SEXP col_names;
  std::vector<int> idx;
  size_t pre_hash;
  bool operator==(const LevelsKey& o) const {
    return pre_hash == o.pre_hash &&
           col_names == o.col_names &&
           idx == o.idx;
  }
};
struct LevelsKeyHash {
  size_t operator()(const LevelsKey& k) const { return k.pre_hash; }
};

static inline size_t compute_levels_hash(SEXP col_names,
                                         const std::vector<int>& idx) {
  size_t h = std::hash<void*>()((void*)col_names);
  for (int v : idx) h = h * 1000003 ^ (size_t)v;
  return h;
}

static std::unordered_map<LevelsKey, SEXP, LevelsKeyHash> g_levels_cache;

static SEXP get_or_make_levels(SEXP col_names, const std::vector<int>& meas_idx) {
  LevelsKey key{col_names, meas_idx, compute_levels_hash(col_names, meas_idx)};
  auto it = g_levels_cache.find(key);
  if (__builtin_expect(it != g_levels_cache.end(), 1)) return it->second;
  SEXP levels = Rf_allocVector(STRSXP, (R_xlen_t)meas_idx.size());
  for (size_t k = 0; k < meas_idx.size(); ++k)
    SET_STRING_ELT(levels, k, STRING_ELT(col_names, meas_idx[k]));
  R_PreserveObject(levels);
  if (g_levels_cache.size() < 32)
    g_levels_cache.emplace(std::move(key), levels);
  return levels;
}

static std::atomic<int> g_gc_requested{0};

// ============================================================
// 0b. OpenMP thread cap
// ============================================================
static int get_thread_cap() {
#ifdef _OPENMP
  static const int cap = []() __attribute__((cold)) {
    int nprocs = omp_get_num_procs();
    if (nprocs < 1) nprocs = 1;
    if (nprocs > 128) nprocs = 128;
    return nprocs;
  }();
  return cap;
#else
  return 1;
#endif
}

// ============================================================
// SEXP allocation: public R API only
// ============================================================
static inline size_t sizeof_sexp(SEXPTYPE type) {
  switch(type) {
  case INTSXP:  return sizeof(int);
  case REALSXP: return sizeof(double);
  case LGLSXP:  return sizeof(int);
  case STRSXP:  return sizeof(SEXP);
  default:      return 0;
  }
}
static inline SEXP alloc_smart(SEXPTYPE type, R_xlen_t n) {
  return Rf_allocVector(type, n);
}

// ============================================================
// 3. SIMD primitives
// ============================================================
static void nt_memcpy_fallback(void* d, const void* s, size_t n) { memcpy(d, s, n); }

#ifdef __AVX2__
static void nt_memcpy_avx2(void* dst, const void* src, size_t bytes) __attribute__((hot));
static void nt_memcpy_avx2(void* dst, const void* src, size_t bytes) {
  const uint8_t* s = (const uint8_t*)src;
  uint8_t* d = (uint8_t*)dst;
  size_t i = 0;
  size_t align = (32 - ((uintptr_t)d & 31)) & 31;
  if (align && align <= bytes) { memcpy(d, s, align); i += align; }
  for (; i + 128 <= bytes; i += 128) {
    __m256i v0 = _mm256_loadu_si256((const __m256i*)(s+i));
    __m256i v1 = _mm256_loadu_si256((const __m256i*)(s+i+32));
    __m256i v2 = _mm256_loadu_si256((const __m256i*)(s+i+64));
    __m256i v3 = _mm256_loadu_si256((const __m256i*)(s+i+96));
    _mm256_stream_si256((__m256i*)(d+i),      v0);
    _mm256_stream_si256((__m256i*)(d+i+32),   v1);
    _mm256_stream_si256((__m256i*)(d+i+64),   v2);
    _mm256_stream_si256((__m256i*)(d+i+96),   v3);
    _mm_prefetch((const char*)(s+i+256), _MM_HINT_NTA);
  }
  for (; i + 32 <= bytes; i += 32)
    _mm256_stream_si256((__m256i*)(d+i), _mm256_loadu_si256((const __m256i*)(s+i)));
  if (i < bytes) memcpy(d + i, s + i, bytes - i);
}
#endif

#ifdef __AVX512F__
static void nt_memcpy_avx512(void* dst, const void* src, size_t bytes) __attribute__((hot));
static void nt_memcpy_avx512(void* dst, const void* src, size_t bytes) {
  const uint8_t* s = (const uint8_t*)src;
  uint8_t* d = (uint8_t*)dst;
  size_t i = 0;
  size_t align = (64 - ((uintptr_t)d & 63)) & 63;
  if (align && align <= bytes) { memcpy(d, s, align); i += align; }
  for (; i + 256 <= bytes; i += 256) {
    __m512i v0 = _mm512_loadu_si512((const __m512i*)(s+i));
    __m512i v1 = _mm512_loadu_si512((const __m512i*)(s+i+64));
    __m512i v2 = _mm512_loadu_si512((const __m512i*)(s+i+128));
    __m512i v3 = _mm512_loadu_si512((const __m512i*)(s+i+192));
    _mm512_stream_si512((__m512i*)(d+i),       v0);
    _mm512_stream_si512((__m512i*)(d+i+64),    v1);
    _mm512_stream_si512((__m512i*)(d+i+128),   v2);
    _mm512_stream_si512((__m512i*)(d+i+192),   v3);
    _mm_prefetch((const char*)(s+i+512), _MM_HINT_NTA);
  }
  for (; i + 64 <= bytes; i += 64)
    _mm512_stream_si512((__m512i*)(d+i), _mm512_loadu_si512((const __m512i*)(s+i)));
  if (i < bytes) memcpy(d + i, s + i, bytes - i);
}
#endif

#if defined(__AVX512F__)
static inline void transpose8x8_avx512(
    __m512d r0,__m512d r1,__m512d r2,__m512d r3,__m512d r4,__m512d r5,__m512d r6,__m512d r7,
    __m512d& c0,__m512d& c1,__m512d& c2,__m512d& c3,__m512d& c4,__m512d& c5,__m512d& c6,__m512d& c7)
    __attribute__((always_inline, hot));
static inline void transpose8x8_avx512(
    __m512d r0,__m512d r1,__m512d r2,__m512d r3,__m512d r4,__m512d r5,__m512d r6,__m512d r7,
    __m512d& c0,__m512d& c1,__m512d& c2,__m512d& c3,__m512d& c4,__m512d& c5,__m512d& c6,__m512d& c7) {
  __m512d t0=_mm512_unpacklo_pd(r0,r1), t1=_mm512_unpackhi_pd(r0,r1);
  __m512d t2=_mm512_unpacklo_pd(r2,r3), t3=_mm512_unpackhi_pd(r2,r3);
  __m512d t4=_mm512_unpacklo_pd(r4,r5), t5=_mm512_unpackhi_pd(r4,r5);
  __m512d t6=_mm512_unpacklo_pd(r6,r7), t7=_mm512_unpackhi_pd(r6,r7);
  __m512d s0=_mm512_shuffle_f64x2(t0,t2,0x88), s1=_mm512_shuffle_f64x2(t0,t2,0xDD);
  __m512d s2=_mm512_shuffle_f64x2(t1,t3,0x88), s3=_mm512_shuffle_f64x2(t1,t3,0xDD);
  __m512d s4=_mm512_shuffle_f64x2(t4,t6,0x88), s5=_mm512_shuffle_f64x2(t4,t6,0xDD);
  __m512d s6=_mm512_shuffle_f64x2(t5,t7,0x88), s7=_mm512_shuffle_f64x2(t5,t7,0xDD);
  c0=_mm512_shuffle_f64x2(s0,s4,0x88); c1=_mm512_shuffle_f64x2(s2,s6,0x88);
  c2=_mm512_shuffle_f64x2(s1,s5,0x88); c3=_mm512_shuffle_f64x2(s3,s7,0x88);
  c4=_mm512_shuffle_f64x2(s0,s4,0xDD); c5=_mm512_shuffle_f64x2(s2,s6,0xDD);
  c6=_mm512_shuffle_f64x2(s1,s5,0xDD); c7=_mm512_shuffle_f64x2(s3,s7,0xDD);
}
#endif

static inline void fill_int_exact(int* dst, int v, int n) __attribute__((always_inline, hot));
static inline void fill_int_exact(int* dst, int v, int n) {
  if (n <= 0) return;
  int i = 0;
#if defined(__AVX512F__)
  {
    __m512i vv512 = _mm512_set1_epi32(v);
    for (; i + 16 <= n; i += 16) _mm512_storeu_si512((__m512i*)(dst+i), vv512);
    if (i < n) {
      __mmask16 mask = (__mmask16)((1u << (n - i)) - 1u);
      _mm512_mask_storeu_epi32(dst + i, mask, vv512);
    }
    return;
  }
#elif defined(__AVX2__)
  {
    __m256i vv256 = _mm256_set1_epi32(v);
    for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i*)(dst+i), vv256);
  }
#endif
  for (; i < n; ++i) dst[i] = v;
}

static inline void fill_double_exact(double* dst, double v, int n) __attribute__((always_inline, hot));
static inline void fill_double_exact(double* dst, double v, int n) {
  if (n <= 0) return;
  int i = 0;
#if defined(__AVX512F__)
  {
    __m512d vv512 = _mm512_set1_pd(v);
    for (; i + 8 <= n; i += 8) _mm512_storeu_pd(dst+i, vv512);
    if (i < n) {
      __mmask8 mask = (__mmask8)((1u << (n - i)) - 1u);
      _mm512_mask_storeu_pd(dst + i, mask, vv512);
    }
    return;
  }
#elif defined(__AVX2__)
  {
    __m256d vv256 = _mm256_set1_pd(v);
    for (; i + 4 <= n; i += 4) _mm256_storeu_pd(dst+i, vv256);
  }
#endif
  for (; i < n; ++i) dst[i] = v;
}

static void nt_fill_int_fallback(int* d, int v, size_t n) {
  for (size_t i = 0; i < n; ++i) d[i] = v;
}
#ifdef __AVX2__
static void nt_fill_int_avx2(int* dst, int val, size_t n) __attribute__((hot));
static void nt_fill_int_avx2(int* dst, int val, size_t n) {
  size_t i = 0; __m256i v = _mm256_set1_epi32(val);
  size_t align = (32 - ((uintptr_t)dst & 31)) & 31;
  size_t cnt = align / sizeof(int); if (cnt > n) cnt = n;
  for (size_t j = 0; j < cnt; ++j) dst[j] = val;
  i = cnt;
  for (; i + 32 <= n; i += 32) {
    _mm256_stream_si256((__m256i*)(dst+i),      v);
    _mm256_stream_si256((__m256i*)(dst+i+8),    v);
    _mm256_stream_si256((__m256i*)(dst+i+16),   v);
    _mm256_stream_si256((__m256i*)(dst+i+24),   v);
  }
  for (; i + 8 <= n; i += 8) _mm256_stream_si256((__m256i*)(dst+i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif
#ifdef __AVX512F__
static void nt_fill_int_avx512(int* dst, int val, size_t n) __attribute__((hot));
static void nt_fill_int_avx512(int* dst, int val, size_t n) {
  size_t i = 0; __m512i v = _mm512_set1_epi32(val);
  size_t align = (64 - ((uintptr_t)dst & 63)) & 63;
  size_t cnt = align / sizeof(int); if (cnt > n) cnt = n;
  for (size_t j = 0; j < cnt; ++j) dst[j] = val;
  i = cnt;
  for (; i + 64 <= n; i += 64) {
    _mm512_stream_si512((__m512i*)(dst+i),      v);
    _mm512_stream_si512((__m512i*)(dst+i+16),   v);
    _mm512_stream_si512((__m512i*)(dst+i+32),   v);
    _mm512_stream_si512((__m512i*)(dst+i+48),   v);
  }
  for (; i + 16 <= n; i += 16) _mm512_stream_si512((__m512i*)(dst+i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

static inline void fill_i32_helper(int* p, int v, size_t n) __attribute__((always_inline, hot));
static inline void fill_i32_helper(int* p, int v, size_t n) {
  size_t i = 0;
#ifdef __AVX2__
  __m256i vv = _mm256_set1_epi32(v);
  for (; i + 32 <= n; i += 32) {
    _mm256_storeu_si256((__m256i*)(p+i),      vv);
    _mm256_storeu_si256((__m256i*)(p+i+8),    vv);
    _mm256_storeu_si256((__m256i*)(p+i+16),   vv);
    _mm256_storeu_si256((__m256i*)(p+i+24),   vv);
  }
  for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i*)(p+i), vv);
#endif
  for (; i < n; ++i) p[i] = v;
}

static void broadcast_int_fallback(int* dst, int val, size_t n) {
  for (size_t i = 0; i < n; ++i) dst[i] = val;
}
#ifdef __AVX2__
static void broadcast_int_avx2(int* dst, int val, size_t n) __attribute__((hot));
static void broadcast_int_avx2(int* dst, int val, size_t n) {
  size_t i = 0; __m256i v = _mm256_set1_epi32(val);
  for (; i + 32 <= n; i += 32) {
    _mm256_storeu_si256((__m256i*)(dst+i),      v);
    _mm256_storeu_si256((__m256i*)(dst+i+8),    v);
    _mm256_storeu_si256((__m256i*)(dst+i+16),   v);
    _mm256_storeu_si256((__m256i*)(dst+i+24),   v);
  }
  for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i*)(dst+i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif
#ifdef __AVX512F__
static void broadcast_int_avx512(int* dst, int val, size_t n) __attribute__((hot));
static void broadcast_int_avx512(int* dst, int val, size_t n) {
  size_t i = 0; __m512i v = _mm512_set1_epi32(val);
  for (; i + 64 <= n; i += 64) {
    _mm512_storeu_si512((__m512i*)(dst+i),      v);
    _mm512_storeu_si512((__m512i*)(dst+i+16),   v);
    _mm512_storeu_si512((__m512i*)(dst+i+32),   v);
    _mm512_storeu_si512((__m512i*)(dst+i+48),   v);
  }
  for (; i + 16 <= n; i += 16) _mm512_storeu_si512((__m512i*)(dst+i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

static void broadcast_double_fallback(double* dst, double val, size_t n) {
  for (size_t i = 0; i < n; ++i) dst[i] = val;
}
#ifdef __AVX2__
static void broadcast_double_avx2(double* dst, double val, size_t n) __attribute__((hot));
static void broadcast_double_avx2(double* dst, double val, size_t n) {
  size_t i = 0; __m256d v = _mm256_set1_pd(val);
  for (; i + 16 <= n; i += 16) {
    _mm256_storeu_pd(dst+i,      v);
    _mm256_storeu_pd(dst+i+4,    v);
    _mm256_storeu_pd(dst+i+8,    v);
    _mm256_storeu_pd(dst+i+12,   v);
  }
  for (; i + 4 <= n; i += 4) _mm256_storeu_pd(dst+i, v);
  for (; i < n; ++i) dst[i] = val;
}
#endif
#ifdef __AVX512F__
static void broadcast_double_avx512(double* dst, double val, size_t n) __attribute__((hot));
static void broadcast_double_avx512(double* dst, double val, size_t n) {
  size_t i = 0; __m512d v = _mm512_set1_pd(val);
  for (; i + 32 <= n; i += 32) {
    _mm512_storeu_pd(dst+i,      v);
    _mm512_storeu_pd(dst+i+8,    v);
    _mm512_storeu_pd(dst+i+16,   v);
    _mm512_storeu_pd(dst+i+24,   v);
  }
  for (; i + 8 <= n; i += 8) _mm512_storeu_pd(dst+i, v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

static void int2double_fallback(const int* s, double* d, size_t n, bool is_logical) {
  for (size_t i = 0; i < n; ++i) {
    int v = s[i];
    if (is_logical) d[i] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
    else            d[i] = (v == NA_INTEGER) ? NA_REAL : (double)v;
  }
}
#ifdef __AVX2__
static void int2double_avx2(const int* src, double* dst, size_t n, bool is_logical) __attribute__((hot));
static void int2double_avx2(const int* src, double* dst, size_t n, bool is_logical) {
  size_t i = 0;
  const __m128i na_int = _mm_set1_epi32(is_logical ? NA_LOGICAL : NA_INTEGER);
  const __m256d na_real = _mm256_set1_pd(NA_REAL);
  const __m256d zero = _mm256_setzero_pd();
  const __m256d one  = _mm256_set1_pd(1.0);
  for (; i + 8 <= n; i += 8) {
    __m256i vi = _mm256_loadu_si256((const __m256i*)(src + i));
    __m128i lo = _mm256_castsi256_si128(vi);
    __m128i hi = _mm256_extracti128_si256(vi, 1);
    __m256i lo_b = _mm256_broadcastsi128_si256(lo);
    __m256i hi_b = _mm256_broadcastsi128_si256(hi);
    __m256i na_lo = _mm256_cmpeq_epi32(lo_b, _mm256_broadcastsi128_si256(na_int));
    __m256i na_hi = _mm256_cmpeq_epi32(hi_b, _mm256_broadcastsi128_si256(na_int));
    __m256d r_lo, r_hi;
    if (is_logical) {
      r_lo = _mm256_blendv_pd(zero, one, _mm256_castsi256_pd(_mm256_cmpeq_epi32(lo_b, _mm256_set1_epi32(1))));
      r_hi = _mm256_blendv_pd(zero, one, _mm256_castsi256_pd(_mm256_cmpeq_epi32(hi_b, _mm256_set1_epi32(1))));
    } else {
      r_lo = _mm256_cvtepi32_pd(lo);
      r_hi = _mm256_cvtepi32_pd(hi);
    }
    r_lo = _mm256_blendv_pd(r_lo, na_real, _mm256_castsi256_pd(na_lo));
    r_hi = _mm256_blendv_pd(r_hi, na_real, _mm256_castsi256_pd(na_hi));
    _mm256_stream_pd(dst+i,     r_lo);
    _mm256_stream_pd(dst+i+4,   r_hi);
    _mm_prefetch((const char*)(src+i+32), _MM_HINT_NTA);
  }
  for (; i < n; ++i) {
    int v = src[i];
    if (is_logical) dst[i] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
    else            dst[i] = (v == NA_INTEGER) ? NA_REAL : (double)v;
  }
}
#endif
#ifdef __AVX512F__
static void int2double_avx512(const int* src, double* dst, size_t n, bool is_logical) __attribute__((hot));
static void int2double_avx512(const int* src, double* dst, size_t n, bool is_logical) {
  size_t i = 0;
  const __m256i na_int = _mm256_set1_epi32(is_logical ? NA_LOGICAL : NA_INTEGER);
  const __m512d na_real = _mm512_set1_pd(NA_REAL);
  const __m512d zero = _mm512_setzero_pd();
  const __m512d one  = _mm512_set1_pd(1.0);
  for (; i + 16 <= n; i += 16) {
    __m512i vi = _mm512_loadu_si512((const __m512i*)(src + i));
    __m256i lo = _mm512_extracti32x8_epi32(vi, 0);
    __m256i hi = _mm512_extracti32x8_epi32(vi, 1);
    __mmask8 na_lo = _mm256_cmpeq_epi32_mask(lo, na_int);
    __mmask8 na_hi = _mm256_cmpeq_epi32_mask(hi, na_int);
    __m512d r_lo, r_hi;
    if (is_logical) {
      r_lo = _mm512_mask_blend_pd(_mm256_cmpneq_epi32_mask(lo, _mm256_setzero_si256()), zero, one);
      r_hi = _mm512_mask_blend_pd(_mm256_cmpneq_epi32_mask(hi, _mm256_setzero_si256()), zero, one);
    } else {
      r_lo = _mm512_cvtepi32_pd(lo);
      r_hi = _mm512_cvtepi32_pd(hi);
    }
    r_lo = _mm512_mask_blend_pd(na_lo, r_lo, na_real);
    r_hi = _mm512_mask_blend_pd(na_hi, r_hi, na_real);
    _mm512_stream_pd(dst+i,     r_lo);
    _mm512_stream_pd(dst+i+8,   r_hi);
    _mm_prefetch((const char*)(src+i+64), _MM_HINT_NTA);
  }
  for (; i < n; ++i) {
    int v = src[i];
    if (is_logical) dst[i] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
    else            dst[i] = (v == NA_INTEGER) ? NA_REAL : (double)v;
  }
}
#endif

// ---------- NA counting (SIMD) ----------
static R_xlen_t count_keep_double_fallback(const double* src, R_xlen_t n) {
  R_xlen_t cnt = 0;
  for (R_xlen_t i = 0; i < n; ++i) cnt += !ISNAN(src[i]);
  return cnt;
}
#ifdef __AVX2__
static R_xlen_t count_keep_double_avx2(const double* src, R_xlen_t n) __attribute__((hot));
static R_xlen_t count_keep_double_avx2(const double* src, R_xlen_t n) {
  if (n <= 0) return 0;
  R_xlen_t cnt = 0;
  size_t i = 0;
  const __m256d na = _mm256_set1_pd(NA_REAL);
  const size_t main = (size_t)n - (size_t)n % 4;
  for (; i < main; i += 4) {
    __m256d v = _mm256_loadu_pd(src + i);
    __m256d cmp = _mm256_cmp_pd(v, na, _CMP_NEQ_UQ);
    cnt += __builtin_popcount((unsigned)_mm256_movemask_pd(cmp));
  }
  for (; i < (size_t)n; ++i) cnt += !ISNAN(src[i]);
  return cnt;
}
#endif
#ifdef __AVX512F__
static R_xlen_t count_keep_double_avx512(const double* src, R_xlen_t n) __attribute__((hot));
static R_xlen_t count_keep_double_avx512(const double* src, R_xlen_t n) {
  if (n <= 0) return 0;
  R_xlen_t cnt = 0;
  size_t i = 0;
  const __m512d na = _mm512_set1_pd(NA_REAL);
  const size_t main = (size_t)n - (size_t)n % 8;
  for (; i < main; i += 8) {
    __m512d v = _mm512_loadu_pd(src + i);
    __mmask8 mask = _mm512_cmp_pd_mask(v, na, _CMP_NEQ_UQ);
    cnt += __builtin_popcount((unsigned)mask);
  }
  for (; i < (size_t)n; ++i) cnt += !ISNAN(src[i]);
  return cnt;
}
#endif

static R_xlen_t count_keep_int_fallback(const int* src, R_xlen_t n, bool is_logical) {
  R_xlen_t cnt = 0;
  int na = is_logical ? NA_LOGICAL : NA_INTEGER;
  for (R_xlen_t i = 0; i < n; ++i) cnt += (src[i] != na);
  return cnt;
}
#ifdef __AVX2__
static R_xlen_t count_keep_int_avx2(const int* src, R_xlen_t n, bool is_logical) __attribute__((hot));
static R_xlen_t count_keep_int_avx2(const int* src, R_xlen_t n, bool is_logical) {
  if (n <= 0) return 0;
  R_xlen_t cnt = 0;
  size_t i = 0;
  int na_val = is_logical ? NA_LOGICAL : NA_INTEGER;
  const __m256i na = _mm256_set1_epi32(na_val);
  const size_t main = (size_t)n - (size_t)n % 8;
  for (; i < main; i += 8) {
    __m256i v = _mm256_loadu_si256((const __m256i*)(src + i));
    __m256i cmp = _mm256_cmpeq_epi32(v, na);
    int mask = _mm256_movemask_epi8(cmp);
    int eq = __builtin_popcount((unsigned)mask) / 4;
    cnt += 8 - eq;
  }
  for (; i < (size_t)n; ++i) cnt += (src[i] != na_val);
  return cnt;
}
#endif
#ifdef __AVX512F__
static R_xlen_t count_keep_int_avx512(const int* src, R_xlen_t n, bool is_logical) __attribute__((hot));
static R_xlen_t count_keep_int_avx512(const int* src, R_xlen_t n, bool is_logical) {
  if (n <= 0) return 0;
  R_xlen_t cnt = 0;
  size_t i = 0;
  int na_val = is_logical ? NA_LOGICAL : NA_INTEGER;
  const __m512i na = _mm512_set1_epi32(na_val);
  const size_t main = (size_t)n - (size_t)n % 16;
  for (; i < main; i += 16) {
    __m512i v = _mm512_loadu_si512((const __m512i*)(src + i));
    __mmask16 mask = _mm512_cmpeq_epi32_mask(v, na);
    cnt += 16 - __builtin_popcount((unsigned)mask);
  }
  for (; i < (size_t)n; ++i) cnt += (src[i] != na_val);
  return cnt;
}
#endif

static void (*nt_memcpy_ptr)(void*, const void*, size_t) = nt_memcpy_fallback;
static void (*nt_fill_int_ptr)(int*, int, size_t) = nt_fill_int_fallback;
static void (*broadcast_int_ptr)(int*, int, size_t) = broadcast_int_fallback;
static void (*broadcast_double_ptr)(double*, double, size_t) = broadcast_double_fallback;
static void (*int2double_ptr)(const int*, double*, size_t, bool) = int2double_fallback;
static R_xlen_t (*count_keep_double_ptr)(const double*, R_xlen_t) = count_keep_double_fallback;
static R_xlen_t (*count_keep_int_ptr)(const int*, R_xlen_t, bool) = count_keep_int_fallback;

static void init_cpu_features() __attribute__((cold));
static void init_cpu_features() {
  static bool init = false;
  if (init) return;
  init = true;
  if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl")) {
#ifdef __AVX512F__
    nt_memcpy_ptr = nt_memcpy_avx512;
    nt_fill_int_ptr = nt_fill_int_avx512;
    broadcast_int_ptr = broadcast_int_avx512;
    broadcast_double_ptr = broadcast_double_avx512;
    int2double_ptr = int2double_avx512;
    count_keep_double_ptr = count_keep_double_avx512;
    count_keep_int_ptr = count_keep_int_avx512;
    return;
#endif
  }
  if (__builtin_cpu_supports("avx2")) {
#ifdef __AVX2__
    nt_memcpy_ptr = nt_memcpy_avx2;
    nt_fill_int_ptr = nt_fill_int_avx2;
    broadcast_int_ptr = broadcast_int_avx2;
    broadcast_double_ptr = broadcast_double_avx2;
    int2double_ptr = int2double_avx2;
    count_keep_double_ptr = count_keep_double_avx2;
    count_keep_int_ptr = count_keep_int_avx2;
#endif
  }
}

static void melt_lazy_init() {
  init_cpu_features();
  detect_l3_size_once();
#ifdef _OPENMP
  static bool omp_pool_fixed = false;
  if (__builtin_expect(!omp_pool_fixed, 0)) {
    omp_set_dynamic(0);
    omp_set_num_threads(get_thread_cap());
    omp_pool_fixed = true;
  }
#endif
}

} // anonymous namespace

// ============================================================
// 4. Column index parsing
// ============================================================
static void infer_id_cols(SEXP df, std::vector<int>& id_idx, std::vector<int>& meas_idx) {
  int ncols = Rf_length(df);
  id_idx.clear(); meas_idx.clear();
  for (int i = 0; i < ncols; ++i) {
    SEXP col = VECTOR_ELT(df, i);
    SEXPTYPE t = TYPEOF(col);
    if (t != REALSXP && t != INTSXP && t != LGLSXP) id_idx.push_back(i);
    else if (t == INTSXP && Rf_isFactor(col))       id_idx.push_back(i);
    else                                            meas_idx.push_back(i);
  }
}

static void parse_id_arg(SEXP df, SEXP id_spec,
                         std::vector<int>& id_idx, std::vector<int>& meas_idx) {
  int ncols = Rf_length(df);
  SEXP names = Rf_getAttrib(df, R_NamesSymbol);
  id_idx.clear(); meas_idx.clear();
  if (Rf_isNull(id_spec)) { infer_id_cols(df, id_idx, meas_idx); return; }
  if (TYPEOF(id_spec) == INTSXP) {
    R_xlen_t len = XLENGTH(id_spec);
    const int* ptr = INTEGER(id_spec);
    std::vector<char> is_id(ncols, 0);
    bool has_neg = false;
    for (R_xlen_t i = 0; i < len; ++i) if (ptr[i] < 0) { has_neg = true; break; }
    if (has_neg) {
      std::vector<char> is_meas(ncols, 0);
      for (R_xlen_t i = 0; i < len; ++i) {
        int a = std::abs(ptr[i]) - 1;
        if (a >= 0 && a < ncols) is_meas[a] = 1;
      }
      for (int i = 0; i < ncols; ++i) if (!is_meas[i]) is_id[i] = 1;
    } else {
      for (R_xlen_t i = 0; i < len; ++i) {
        int a = ptr[i] - 1;
        if (a >= 0 && a < ncols) is_id[a] = 1;
      }
    }
    for (int i = 0; i < ncols; ++i) (is_id[i]?id_idx:meas_idx).push_back(i);
    return;
  }
  if (TYPEOF(id_spec) == STRSXP) {
    R_xlen_t len = XLENGTH(id_spec);
    std::vector<char> is_id(ncols, 0);
    for (R_xlen_t i = 0; i < len; ++i) {
      const char* tgt = CHAR(STRING_ELT(id_spec, i));
      for (int j = 0; j < ncols; ++j)
        if (strcmp(CHAR(STRING_ELT(names, j)), tgt) == 0) { is_id[j] = 1; break; }
    }
    for (int i = 0; i < ncols; ++i) (is_id[i]?id_idx:meas_idx).push_back(i);
    return;
  }
  stop("invalid id argument");
}

// ============================================================
// 5. NA helpers
// ============================================================
static inline R_xlen_t count_keep_col(const void* ptr, SEXPTYPE t, bool fac, R_xlen_t n) {
  if (__builtin_expect(fac, 0)) return 0;
  if (t == REALSXP) {
    return count_keep_double_ptr((const double*)ptr, n);
  } else if (t == INTSXP || t == LGLSXP) {
    return count_keep_int_ptr((const int*)ptr, n, t == LGLSXP);
  }
  return 0;
}
static inline bool is_na_meas_at(const void* ptr, SEXPTYPE t, bool fac, R_xlen_t i) {
  if (t == REALSXP)        return ISNAN(((const double*)ptr)[i]);
  if (t == INTSXP && !fac) return ((const int*)ptr)[i] == NA_INTEGER;
  if (t == LGLSXP)         return ((const int*)ptr)[i] == NA_LOGICAL;
  return true;
}
static inline double meas_at_as_double(const void* ptr, SEXPTYPE t, bool fac, R_xlen_t i) {
  if (t == REALSXP) return ((const double*)ptr)[i];
  if (t == INTSXP && !fac) { int v=((const int*)ptr)[i]; return (v==NA_INTEGER)?NA_REAL:(double)v; }
  if (t == LGLSXP) { int v=((const int*)ptr)[i]; return (v==NA_LOGICAL)?NA_REAL:(v?1.0:0.0); }
  return NA_REAL;
}

// ============================================================
// 6. Small-data fast path
// ============================================================
__attribute__((hot))
static SEXP melt_small_cpp(SEXP df,
                           const std::vector<int>& id_idx,
                           const std::vector<int>& meas_idx,
                           SEXP col_names,
                           SEXP variable_name, SEXP value_name,
                           bool row_major) {
  int n_id   = (int)id_idx.size();
  int n_meas = (int)meas_idx.size();
  R_xlen_t n = XLENGTH(VECTOR_ELT(df, 0));
  R_xlen_t total_out = n * (R_xlen_t)n_meas;

  SEXP out       = PROTECT(Rf_allocVector(VECSXP, n_id + 2));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, n_id + 2));

  for (int i = 0; i < n_id; ++i) {
    SEXP src = VECTOR_ELT(df, id_idx[i]);
    SEXPTYPE t = TYPEOF(src);
    SEXP dst = PROTECT(Rf_allocVector(t, total_out));
    if (ATTRIB(src) != R_NilValue) Rf_copyMostAttrib(src, dst);
    SET_VECTOR_ELT(out, i, dst);
    SET_STRING_ELT(out_names, i, STRING_ELT(col_names, id_idx[i]));
    UNPROTECT(1);
  }

  SEXP var_col = PROTECT(Rf_allocVector(INTSXP, total_out));
  SET_VECTOR_ELT(out, n_id, var_col);
  SET_STRING_ELT(out_names, n_id,
                 (!Rf_isNull(variable_name) && TYPEOF(variable_name) == STRSXP) ?
                   STRING_ELT(variable_name, 0) : g_variable_name("variable"));
  {
    SEXP levels = get_or_make_levels(col_names, meas_idx);
    Rf_setAttrib(var_col, R_LevelsSymbol, levels);
    Rf_setAttrib(var_col, R_ClassSymbol, g_fac_class("factor"));
  }

  SEXP val_col = PROTECT(Rf_allocVector(REALSXP, total_out));
  SET_VECTOR_ELT(out, n_id + 1, val_col);
  SET_STRING_ELT(out_names, n_id + 1,
                 (!Rf_isNull(value_name) && TYPEOF(value_name) == STRSXP) ?
                   STRING_ELT(value_name, 0) : g_value_name("value"));

  int* pvar = INTEGER(var_col);
  double* pval = REAL(val_col);

  std::vector<const void*>   src_ptrs(n_meas);
  std::vector<SEXPTYPE>      src_types(n_meas);
  std::vector<const void*>   id_src(n_id);
  std::vector<void*>         id_dst(n_id);
  std::vector<size_t>        id_es(n_id);
  std::vector<SEXPTYPE>      id_type(n_id);

  for (int k = 0; k < n_meas; ++k) {
    SEXP col = VECTOR_ELT(df, meas_idx[k]);
    src_ptrs[k]  = (const void*)DATAPTR_RO(col);
    src_types[k] = TYPEOF(col);
  }
  for (int i = 0; i < n_id; ++i) {
    SEXP s = VECTOR_ELT(df, id_idx[i]);
    SEXP d = VECTOR_ELT(out, i);
    id_src[i]  = (const void*)DATAPTR_RO(s);
    SEXPTYPE t = TYPEOF(s);
    if (t == INTSXP || t == LGLSXP)      id_dst[i] = (void*)INTEGER(d);
    else if (t == REALSXP)               id_dst[i] = (void*)REAL(d);
    else if (t == STRSXP)                id_dst[i] = (void*)STRING_PTR(d);
    else                                  id_dst[i] = (void*)DATAPTR_RO(d);
    id_es[i]   = sizeof_sexp(t);
    id_type[i] = t;
  }

  if (!row_major) {
    {
      double* pv2 = pval;
      int*    pv  = pvar;
      for (int k = 0; k < n_meas; ++k, pv2 += n, pv += n) {
        SEXPTYPE t  = src_types[k];
        int kk      = k + 1;
        fill_i32_helper(pv, kk, (size_t)n);
        if (t == REALSXP) {
          memcpy(pv2, src_ptrs[k], (size_t)n * sizeof(double));
        } else if (t == INTSXP) {
          const int* s = (const int*)src_ptrs[k];
          for (R_xlen_t r = 0; r < n; ++r) {
            int v = s[r];
            pv2[r] = (v == NA_INTEGER) ? NA_REAL : (double)v;
          }
        } else if (t == LGLSXP) {
          const int* s = (const int*)src_ptrs[k];
          for (R_xlen_t r = 0; r < n; ++r) {
            int v = s[r];
            pv2[r] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
          }
        } else {
          for (R_xlen_t r = 0; r < n; ++r) pv2[r] = NA_REAL;
        }
      }
    }

    for (int i = 0; i < n_id; ++i) {
      size_t es = id_es[i];
      if (!es) continue;
      size_t seg_bytes = (size_t)n * es;
      if (seg_bytes == 0) continue;
      char* dst = (char*)id_dst[i];
      const char* src = (const char*)id_src[i];
      memcpy(dst, src, seg_bytes);
      size_t filled = 1;
      while (filled < (size_t)n_meas) {
        size_t to_copy = filled;
        if (to_copy > (size_t)n_meas - filled) to_copy = (size_t)n_meas - filled;
        memcpy(dst + filled * seg_bytes, dst, to_copy * seg_bytes);
        filled += to_copy;
      }
    }
  } else {
    for (R_xlen_t r = 0; r < n; ++r) {
      double* vr = pval + r * n_meas;
      int*    kr = pvar + r * n_meas;
      for (int k = 0; k < n_meas; ++k) {
        SEXPTYPE t = src_types[k];
        double v = NA_REAL;
        if (t == REALSXP) v = ((const double*)src_ptrs[k])[r];
        else if (t == INTSXP) {
          int x = ((const int*)src_ptrs[k])[r];
          v = (x == NA_INTEGER) ? NA_REAL : (double)x;
        } else if (t == LGLSXP) {
          int x = ((const int*)src_ptrs[k])[r];
          v = (x == NA_LOGICAL) ? NA_REAL : (x ? 1.0 : 0.0);
        }
        vr[k] = v;
        kr[k] = k + 1;
      }
      for (int i = 0; i < n_id; ++i) {
        SEXPTYPE t = id_type[i];
        if (t == INTSXP || t == LGLSXP) {
          int x = ((const int*)id_src[i])[r];
          int* d = (int*)id_dst[i] + r * n_meas;
          for (int k = 0; k < n_meas; ++k) d[k] = x;
        } else if (t == REALSXP) {
          double x = ((const double*)id_src[i])[r];
          double* d = (double*)id_dst[i] + r * n_meas;
          for (int k = 0; k < n_meas; ++k) d[k] = x;
        } else if (t == STRSXP) {
          SEXP x = ((const SEXP*)id_src[i])[r];
          SEXP* d = (SEXP*)id_dst[i] + r * n_meas;
          for (int k = 0; k < n_meas; ++k) d[k] = x;
        }
      }
    }
  }

  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = (total_out <= (R_xlen_t)INT_MAX) ? -(int)total_out : NA_INTEGER;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    Rf_setAttrib(out, R_ClassSymbol, g_df_class("data.frame"));
    UNPROTECT(1);
  }
  UNPROTECT(4);
  return out;
}

// ============================================================
// 7. Main entry point
// ============================================================
// [[Rcpp::export]]
SEXP melt_cpp(SEXP df, SEXP id = R_NilValue,
              SEXP variable_name = R_NilValue, SEXP value_name = R_NilValue,
              SEXP major = R_NilValue, int n_threads = 0,
              bool na_rm = false) {
  melt_lazy_init();

  const size_t l3_size = g_l3_size;

  std::vector<int> id_idx, meas_idx;
  parse_id_arg(df, id, id_idx, meas_idx);
  int n_id   = (int)id_idx.size();
  int n_meas = (int)meas_idx.size();
  if (__builtin_expect(n_meas == 0, 0)) stop("no measure variables");

  SEXP col_names = Rf_getAttrib(df, R_NamesSymbol);
  R_xlen_t n     = XLENGTH(VECTOR_ELT(df, 0));
  R_xlen_t total = n * n_meas;

  bool row_major;
  if (!Rf_isNull(major) && TYPEOF(major) == STRSXP && XLENGTH(major) > 0) {
    std::string s = CHAR(STRING_ELT(major, 0));
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    row_major = !(s == "col" || s == "variable");
  } else {
    row_major = (n_id <= 2) && (n >= 3000000LL) && (n_meas <= 16);
  }

  if (!na_rm && total <= 200000 && n_meas <= 128) {
    return melt_small_cpp(df, id_idx, meas_idx, col_names,
                          variable_name, value_name, row_major);
  }

  std::vector<const void*> meas_ptrs;
  std::vector<SEXPTYPE>    meas_types;
  std::vector<bool>        meas_is_factor;
  meas_ptrs.reserve(n_meas);
  meas_types.reserve(n_meas);
  meas_is_factor.reserve(n_meas);
  for (int k = 0; k < n_meas; ++k) {
    SEXP col = VECTOR_ELT(df, meas_idx[k]);
    meas_ptrs.push_back((const void*)DATAPTR_RO(col));
    meas_types.push_back(TYPEOF(col));
    meas_is_factor.push_back(Rf_isFactor(col));
  }

  bool all_real_meas = true;
  for (int k = 0; k < n_meas; ++k)
    if (meas_types[k] != REALSXP) { all_real_meas = false; break; }

  R_xlen_t total_out = total;
  std::vector<R_xlen_t> col_off, row_off;

  if (na_rm) {
    const R_xlen_t work = (R_xlen_t)n * (R_xlen_t)n_meas;
    const bool par_count = (work >= 500000LL);
    int cnt_threads = 1;
    if (par_count) {
#ifdef _OPENMP
      cnt_threads = std::min(omp_get_max_threads(), get_thread_cap());
      if (cnt_threads < 1) cnt_threads = 1;
#endif
    }

    if (!row_major) {
      col_off.assign(n_meas + 1, 0);
      if (par_count && cnt_threads > 1) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(cnt_threads)
#endif
        for (int k = 0; k < n_meas; ++k)
          col_off[k+1] = count_keep_col(meas_ptrs[k], meas_types[k], meas_is_factor[k], n);
        for (int k = 1; k <= n_meas; ++k) col_off[k] += col_off[k-1];
      } else {
        for (int k = 0; k < n_meas; ++k)
          col_off[k+1] = col_off[k] + count_keep_col(meas_ptrs[k], meas_types[k], meas_is_factor[k], n);
      }
      total_out = col_off[n_meas];
    } else {
      row_off.assign(n + 1, 0);
      if (par_count && cnt_threads > 1) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(cnt_threads)
#endif
        for (R_xlen_t r = 0; r < n; ++r) {
          R_xlen_t cnt = 0;
          for (int k = 0; k < n_meas; ++k)
            if (!is_na_meas_at(meas_ptrs[k], meas_types[k], meas_is_factor[k], r)) ++cnt;
          row_off[r+1] = cnt;
        }
        for (R_xlen_t r = 0; r < n; ++r) row_off[r+1] += row_off[r];
      } else {
        for (R_xlen_t r = 0; r < n; ++r) {
          R_xlen_t cnt = 0;
          for (int k = 0; k < n_meas; ++k)
            if (!is_na_meas_at(meas_ptrs[k], meas_types[k], meas_is_factor[k], r)) ++cnt;
          row_off[r+1] = row_off[r] + cnt;
        }
      }
      total_out = row_off[n];
    }
  }

  {
    size_t est = (size_t)total_out * 12 + (size_t)total_out * 4 * (size_t)n_id;
    if (est >= (2ULL << 30)) R_gc();
  }

  SEXP out       = PROTECT(alloc_smart(VECSXP, n_id + 2));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, n_id + 2));

  std::vector<const void*> id_ptrs;
  std::vector<SEXPTYPE>    id_types;
  std::vector<void*>       out_id_ptrs;
  std::vector<size_t>      id_elem_sizes;
  id_ptrs.reserve(n_id);
  id_types.reserve(n_id);
  out_id_ptrs.reserve(n_id);
  id_elem_sizes.reserve(n_id);

  for (int i = 0; i < n_id; ++i) {
    SEXP src = VECTOR_ELT(df, id_idx[i]);
    SEXPTYPE t = TYPEOF(src);
    SEXP dst = PROTECT(alloc_smart(t, total_out));
    Rf_copyMostAttrib(src, dst);
    SET_VECTOR_ELT(out, i, dst);
    SET_STRING_ELT(out_names, i, STRING_ELT(col_names, id_idx[i]));
    id_ptrs.push_back((const void*)DATAPTR_RO(src));
    id_types.push_back(t);
    if (t == INTSXP || t == LGLSXP)      out_id_ptrs.push_back((void*)INTEGER(dst));
    else if (t == REALSXP)               out_id_ptrs.push_back((void*)REAL(dst));
    else if (t == STRSXP)                out_id_ptrs.push_back((void*)STRING_PTR(dst));
    else                                  out_id_ptrs.push_back((void*)DATAPTR_RO(dst));
    id_elem_sizes.push_back(sizeof_sexp(t));
    UNPROTECT(1);
  }

  SEXP var_col = PROTECT(alloc_smart(INTSXP, total_out));
  int* pvar = INTEGER(var_col);
  SET_VECTOR_ELT(out, n_id, var_col);
  SET_STRING_ELT(out_names, n_id,
                 (!Rf_isNull(variable_name) && TYPEOF(variable_name) == STRSXP) ?
                   STRING_ELT(variable_name, 0) : g_variable_name("variable"));
  {
    SEXP levels = get_or_make_levels(col_names, meas_idx);
    Rf_setAttrib(var_col, R_LevelsSymbol, levels);
    Rf_setAttrib(var_col, R_ClassSymbol, g_fac_class("factor"));
  }

  SEXP val_col = PROTECT(alloc_smart(REALSXP, total_out));
  double* pval = REAL(val_col);
  SET_VECTOR_ELT(out, n_id + 1, val_col);
  SET_STRING_ELT(out_names, n_id + 1,
                 (!Rf_isNull(value_name) && TYPEOF(value_name) == STRSXP) ?
                   STRING_ELT(value_name, 0) : g_value_name("value"));

  bool use_parallel = false;
  int actual_threads = 1;
#ifdef _OPENMP
  constexpr R_xlen_t MIN_PARALLEL_WORK = 1500000LL;
  constexpr R_xlen_t ELEMS_PER_THREAD  = 200000LL;
  const int thread_cap = get_thread_cap();
  if (n_threads == 1) {
    use_parallel = false;
    actual_threads = 1;
  } else if (n_threads > 1) {
    int hw = omp_get_max_threads();
    actual_threads = std::max(1, std::min(n_threads, hw));
    use_parallel = (actual_threads > 1);
  } else {
    R_xlen_t effective_work = (R_xlen_t)total_out +
                              (R_xlen_t)n * (R_xlen_t)n_id;
    if (effective_work >= MIN_PARALLEL_WORK) {
      int hw = omp_get_max_threads();
      R_xlen_t calc = (effective_work + ELEMS_PER_THREAD - 1) / ELEMS_PER_THREAD;
      R_xlen_t cap_x = (R_xlen_t)thread_cap;
      actual_threads = (int)std::max<R_xlen_t>(1,
                       std::min<R_xlen_t>(cap_x, calc));
      actual_threads = std::min(actual_threads, hw);
      use_parallel = (actual_threads > 1);
    }
  }
#else
  (void)n_threads;
#endif

  if (g_gc_requested.load(std::memory_order_relaxed)) {
    g_gc_requested.store(0, std::memory_order_relaxed);
    if (!use_parallel) R_gc();
  }

  bool used_nt_store = false;

  if (!row_major) {
    const size_t val_bytes_total = (size_t)total * sizeof(double);
    const size_t var_bytes_total = (size_t)total * sizeof(int);
    size_t id_bytes_total = 0;
    for (size_t es : id_elem_sizes) id_bytes_total += (size_t)total * es;
    const size_t nt_thresh = l3_size / 2;
    const bool nt_val_col = val_bytes_total >= nt_thresh;
    const bool nt_var_col = var_bytes_total >= nt_thresh;
    const bool nt_id_col  = id_bytes_total  >= nt_thresh;

    if (!na_rm) {
      int SUBDIV = 1;
      if (use_parallel) {
        int64_t want_tasks = (int64_t)actual_threads * 4;
        int64_t desired = (want_tasks + n_meas - 1) / n_meas;
        int64_t by_size = (int64_t)std::max<R_xlen_t>(1, n / 16384);
        SUBDIV = (int)std::max<int64_t>(1,
                  std::min<int64_t>(desired,
                                    std::min<int64_t>(32, by_size)));
      }
      const bool var_split = ((size_t)n_meas * n > (1ULL << 26));

      auto process_sub = [&](int k, R_xlen_t r0, R_xlen_t r1) {
        R_xlen_t len = r1 - r0;
        if (len <= 0) return;
        R_xlen_t out_off = (R_xlen_t)k * n + r0;

        if (n_id > 0) {
          for (int i = 0; i < n_id; ++i) {
            size_t es = id_elem_sizes[i];
            if (!es) continue;
            size_t bytes = (size_t)len * es;
            char* dst = (char*)out_id_ptrs[i] + (R_xlen_t)out_off * es;
            const char* src = (const char*)id_ptrs[i] + (size_t)r0 * es;
            if (nt_id_col && bytes >= (1ULL << 20)) nt_memcpy_ptr(dst, src, bytes);
            else                                     memcpy(dst, src, bytes);
          }
        }

        if (!var_split) {
          size_t bytes = (size_t)len * sizeof(int);
          if (nt_var_col && bytes >= (1ULL << 20)) nt_fill_int_ptr(pvar + out_off, k + 1, (size_t)len);
          else                                      fill_i32_helper(pvar + out_off, k + 1, (size_t)len);
        }

        double* val_dst = pval + out_off;
        SEXPTYPE type = meas_types[k];
        if (type == REALSXP) {
          const double* src = (const double*)meas_ptrs[k] + r0;
          size_t bytes = (size_t)len * sizeof(double);
          if (nt_val_col && bytes >= (1ULL << 20)) nt_memcpy_ptr(val_dst, src, bytes);
          else                                      memcpy(val_dst, src, bytes);
        } else if (type == INTSXP && !meas_is_factor[k]) {
          int2double_ptr((const int*)meas_ptrs[k] + r0, val_dst, (size_t)len, false);
        } else if (type == LGLSXP) {
          int2double_ptr((const int*)meas_ptrs[k] + r0, val_dst, (size_t)len, true);
        } else {
          std::fill(val_dst, val_dst + len, NA_REAL);
        }
      };

      if (var_split) {
        if (use_parallel) {
#pragma omp parallel for schedule(static) num_threads(actual_threads)
          for (int k = 0; k < n_meas; ++k) fill_i32_helper(pvar + (R_xlen_t)k * n, k+1, (size_t)n);
        } else {
          for (int k = 0; k < n_meas; ++k) fill_i32_helper(pvar + (R_xlen_t)k * n, k+1, (size_t)n);
        }
      }
      if (use_parallel && SUBDIV > 1) {
        const int total_tasks = n_meas * SUBDIV;
#pragma omp parallel for schedule(static) num_threads(actual_threads)
        for (int t = 0; t < total_tasks; ++t) {
          int k = t / SUBDIV;
          int s = t % SUBDIV;
          R_xlen_t r0 = (R_xlen_t)s       * n / SUBDIV;
          R_xlen_t r1 = (R_xlen_t)(s + 1) * n / SUBDIV;
          process_sub(k, r0, r1);
        }
      } else if (use_parallel) {
#pragma omp parallel for schedule(static) num_threads(actual_threads)
        for (int k = 0; k < n_meas; ++k) process_sub(k, 0, n);
      } else {
        for (int k = 0; k < n_meas; ++k) process_sub(k, 0, n);
      }
    } else {
      thread_local std::vector<R_xlen_t> tl_keep_idx;

      auto process_col_na_rm = [&](int k) {
        R_xlen_t off = col_off[k];
        R_xlen_t cnt = col_off[k+1] - off;
        if (cnt == 0) return;
        SEXPTYPE t = meas_types[k];
        bool is_fac = meas_is_factor[k];
        const void* mp = meas_ptrs[k];
        int* pv = pvar + off;
        double* pd = pval + off;

        tl_keep_idx.clear();
        tl_keep_idx.reserve((size_t)cnt);

        R_xlen_t j = 0;
        for (R_xlen_t i = 0; i < n; ++i) {
          if (is_na_meas_at(mp, t, is_fac, i)) continue;
          tl_keep_idx.push_back(i);
          pd[j] = meas_at_as_double(mp, t, is_fac, i);
          pv[j] = k + 1;
          ++j;
        }
        const R_xlen_t m = j;

        const R_xlen_t* kidx = tl_keep_idx.data();
        for (int ii = 0; ii < n_id; ++ii) {
          SEXPTYPE it = id_types[ii];
          if (it == INTSXP || it == LGLSXP) {
            const int* s = (const int*)id_ptrs[ii];
            int* d = (int*)out_id_ptrs[ii] + off;
            for (R_xlen_t jj = 0; jj < m; ++jj) d[jj] = s[kidx[jj]];
          } else if (it == REALSXP) {
            const double* s = (const double*)id_ptrs[ii];
            double* d = (double*)out_id_ptrs[ii] + off;
            for (R_xlen_t jj = 0; jj < m; ++jj) d[jj] = s[kidx[jj]];
          } else {
            const SEXP* s = (const SEXP*)id_ptrs[ii];
            SEXP* d = (SEXP*)out_id_ptrs[ii] + off;
            for (R_xlen_t jj = 0; jj < m; ++jj) d[jj] = s[kidx[jj]];
          }
        }
      };

      if (use_parallel && n_meas > 1) {
#pragma omp parallel for schedule(static) num_threads(actual_threads)
        for (int k = 0; k < n_meas; ++k) process_col_na_rm(k);
      } else {
        for (int k = 0; k < n_meas; ++k) process_col_na_rm(k);
      }
    }
  }
  else {
    size_t bytes_per_row = (size_t)n_meas * (sizeof(double) + sizeof(int));
    for (size_t es : id_elem_sizes) bytes_per_row += es;
    if (bytes_per_row == 0) bytes_per_row = 1;
    size_t target_block_bytes = l3_size / 2;
    R_xlen_t calc_block = (R_xlen_t)(target_block_bytes / bytes_per_row);
    calc_block = std::max<R_xlen_t>(256, std::min<R_xlen_t>(65536, calc_block));

    R_xlen_t BLOCK_SIZE;
    if (use_parallel) {
      R_xlen_t min_per_thread = calc_block / 4;
      R_xlen_t by_t = n / (R_xlen_t)(4 * actual_threads);
      BLOCK_SIZE = std::max<R_xlen_t>(min_per_thread, by_t);
      BLOCK_SIZE = std::min<R_xlen_t>(BLOCK_SIZE, calc_block);
    } else {
      BLOCK_SIZE = calc_block;
    }

    thread_local std::vector<int> tl_var_pat8;
    if ((int)tl_var_pat8.size() < 8 * n_meas) tl_var_pat8.resize((size_t)8 * n_meas);
    for (int p = 0; p < 8; ++p)
      for (int k = 0; k < n_meas; ++k)
        tl_var_pat8[(size_t)p * n_meas + k] = k + 1;
    const int* var_pat8 = tl_var_pat8.data();

    const bool nt_store =
      (n_meas % 8 == 0) &&
      ((size_t)total_out * sizeof(double) >= l3_size / 2) &&
      (((uintptr_t)pval & 63) == 0);
    if (nt_store) used_nt_store = true;

    if (!na_rm) {
      if (all_real_meas && n_meas >= 8) {
        auto process_row_block = [&](R_xlen_t ib, R_xlen_t iend) {
          R_xlen_t full_end = ib + ((iend - ib) / 8) * 8;
          for (R_xlen_t rb = ib; rb < full_end; rb += 8) {
            if (rb + 8 < full_end) {
              const R_xlen_t pfr = rb + 8;
              for (int k = 0; k < n_meas; ++k)
                _mm_prefetch((const char*)meas_ptrs[k] + pfr * (ptrdiff_t)sizeof(double), _MM_HINT_T0);
              for (int j = 0; j < n_id; ++j) {
                SEXPTYPE t = id_types[j];
                const char* p = (const char*)id_ptrs[j];
                if (t == REALSXP) _mm_prefetch(p + pfr * (ptrdiff_t)sizeof(double), _MM_HINT_T1);
                else if (t == INTSXP || t == LGLSXP) _mm_prefetch(p + pfr * (ptrdiff_t)sizeof(int), _MM_HINT_T1);
                else if (t == STRSXP) _mm_prefetch(p + pfr * (ptrdiff_t)sizeof(SEXP), _MM_HINT_T1);
              }
            }
#if defined(__AVX512F__)
            for (int kb = 0; kb < n_meas; kb += 8) {
              int nc = (n_meas - kb < 8) ? (n_meas - kb) : 8;
              if (nc == 8) {
                __m512d c0 = _mm512_loadu_pd((const double*)meas_ptrs[kb+0] + rb);
                __m512d c1 = _mm512_loadu_pd((const double*)meas_ptrs[kb+1] + rb);
                __m512d c2 = _mm512_loadu_pd((const double*)meas_ptrs[kb+2] + rb);
                __m512d c3 = _mm512_loadu_pd((const double*)meas_ptrs[kb+3] + rb);
                __m512d c4 = _mm512_loadu_pd((const double*)meas_ptrs[kb+4] + rb);
                __m512d c5 = _mm512_loadu_pd((const double*)meas_ptrs[kb+5] + rb);
                __m512d c6 = _mm512_loadu_pd((const double*)meas_ptrs[kb+6] + rb);
                __m512d c7 = _mm512_loadu_pd((const double*)meas_ptrs[kb+7] + rb);
                __m512d o0,o1,o2,o3,o4,o5,o6,o7;
                transpose8x8_avx512(c0,c1,c2,c3,c4,c5,c6,c7,o0,o1,o2,o3,o4,o5,o6,o7);
                if (nt_store) {
                  _mm512_stream_pd(pval + (rb+0)*n_meas + kb, o0);
                  _mm512_stream_pd(pval + (rb+1)*n_meas + kb, o1);
                  _mm512_stream_pd(pval + (rb+2)*n_meas + kb, o2);
                  _mm512_stream_pd(pval + (rb+3)*n_meas + kb, o3);
                  _mm512_stream_pd(pval + (rb+4)*n_meas + kb, o4);
                  _mm512_stream_pd(pval + (rb+5)*n_meas + kb, o5);
                  _mm512_stream_pd(pval + (rb+6)*n_meas + kb, o6);
                  _mm512_stream_pd(pval + (rb+7)*n_meas + kb, o7);
                } else {
                  _mm512_storeu_pd(pval + (rb+0)*n_meas + kb, o0);
                  _mm512_storeu_pd(pval + (rb+1)*n_meas + kb, o1);
                  _mm512_storeu_pd(pval + (rb+2)*n_meas + kb, o2);
                  _mm512_storeu_pd(pval + (rb+3)*n_meas + kb, o3);
                  _mm512_storeu_pd(pval + (rb+4)*n_meas + kb, o4);
                  _mm512_storeu_pd(pval + (rb+5)*n_meas + kb, o5);
                  _mm512_storeu_pd(pval + (rb+6)*n_meas + kb, o6);
                  _mm512_storeu_pd(pval + (rb+7)*n_meas + kb, o7);
                }
              } else {
                for (int kk = 0; kk < nc; ++kk) {
                  const double* s = (const double*)meas_ptrs[kb+kk] + rb;
                  for (int i = 0; i < 8; ++i)
                    pval[(rb+i)*n_meas + kb + kk] = s[i];
                }
              }
            }
#else
            for (int k = 0; k < n_meas; ++k) {
              const double* s = (const double*)meas_ptrs[k] + rb;
              for (int i = 0; i < 8; ++i) pval[(rb+i)*n_meas + k] = s[i];
            }
#endif
            memcpy(pvar + (size_t)rb * n_meas, var_pat8, (size_t)8 * n_meas * sizeof(int));
            for (int j = 0; j < n_id; ++j) {
              SEXPTYPE t = id_types[j];
              if (t == INTSXP || t == LGLSXP) {
                const int* s = (const int*)id_ptrs[j];
                int* d = (int*)out_id_ptrs[j];
                for (int i = 0; i < 8; ++i)
                  fill_int_exact(d + (rb+i)*n_meas, s[rb+i], n_meas);
              } else if (t == REALSXP) {
                const double* s = (const double*)id_ptrs[j];
                double* d = (double*)out_id_ptrs[j];
                for (int i = 0; i < 8; ++i)
                  fill_double_exact(d + (rb+i)*n_meas, s[rb+i], n_meas);
              } else if (t == STRSXP) {
                const SEXP* s = (const SEXP*)id_ptrs[j];
                SEXP* d = (SEXP*)out_id_ptrs[j];
                for (int i = 0; i < 8; ++i) {
                  SEXP v = s[rb+i];
                  SEXP* dd = d + (rb+i)*n_meas;
                  for (int k = 0; k < n_meas; ++k) dd[k] = v;
                }
              }
            }
          }
          for (R_xlen_t r = full_end; r < iend; ++r) {
            for (int k = 0; k < n_meas; ++k)
              pval[r*n_meas + k] = ((const double*)meas_ptrs[k])[r];
            memcpy(pvar + (size_t)r * n_meas, var_pat8, (size_t)n_meas * sizeof(int));
            for (int j = 0; j < n_id; ++j) {
              SEXPTYPE t = id_types[j];
              if (t == INTSXP || t == LGLSXP)
                fill_int_exact((int*)out_id_ptrs[j] + r*n_meas, ((const int*)id_ptrs[j])[r], n_meas);
              else if (t == REALSXP)
                fill_double_exact((double*)out_id_ptrs[j] + r*n_meas, ((const double*)id_ptrs[j])[r], n_meas);
              else if (t == STRSXP) {
                SEXP v = ((const SEXP*)id_ptrs[j])[r];
                SEXP* dd = (SEXP*)out_id_ptrs[j] + r*n_meas;
                for (int k = 0; k < n_meas; ++k) dd[k] = v;
              }
            }
          }
        };
        if (use_parallel) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads)
#endif
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_row_block(i, iend);
          }
        } else {
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_row_block(i, iend);
          }
        }
      } else {
        auto process_general_block = [&](R_xlen_t ib, R_xlen_t iend) {
          R_xlen_t full_end = ib + ((iend - ib) / 8) * 8;
          for (R_xlen_t rb = ib; rb < full_end; rb += 8) {
            for (int k = 0; k < n_meas; ++k) {
              SEXPTYPE t = meas_types[k];
              const void* mp = meas_ptrs[k];
              double* dk = pval + rb*n_meas + k;
              if (t == REALSXP) {
                const double* s = (const double*)mp + rb;
                for (int i = 0; i < 8; ++i) dk[i*n_meas] = s[i];
              } else if (t == INTSXP && !meas_is_factor[k]) {
                const int* s = (const int*)mp + rb;
                for (int i = 0; i < 8; ++i) {
                  int v = s[i];
                  dk[i*n_meas] = (v == NA_INTEGER) ? NA_REAL : (double)v;
                }
              } else if (t == LGLSXP) {
                const int* s = (const int*)mp + rb;
                for (int i = 0; i < 8; ++i) {
                  int v = s[i];
                  dk[i*n_meas] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
                }
              } else {
                for (int i = 0; i < 8; ++i) dk[i*n_meas] = NA_REAL;
              }
            }
            memcpy(pvar + (size_t)rb * n_meas, var_pat8, (size_t)8 * n_meas * sizeof(int));
            for (int j = 0; j < n_id; ++j) {
              SEXPTYPE t = id_types[j];
              if (t == INTSXP || t == LGLSXP) {
                const int* s = (const int*)id_ptrs[j];
                int* d = (int*)out_id_ptrs[j];
                for (int i = 0; i < 8; ++i)
                  fill_int_exact(d + (rb+i)*n_meas, s[rb+i], n_meas);
              } else if (t == REALSXP) {
                const double* s = (const double*)id_ptrs[j];
                double* d = (double*)out_id_ptrs[j];
                for (int i = 0; i < 8; ++i)
                  fill_double_exact(d + (rb+i)*n_meas, s[rb+i], n_meas);
              } else if (t == STRSXP) {
                const SEXP* s = (const SEXP*)id_ptrs[j];
                SEXP* d = (SEXP*)out_id_ptrs[j];
                for (int i = 0; i < 8; ++i) {
                  SEXP v = s[rb+i];
                  SEXP* dd = d + (rb+i)*n_meas;
                  for (int k = 0; k < n_meas; ++k) dd[k] = v;
                }
              }
            }
          }
          for (R_xlen_t r = full_end; r < iend; ++r) {
            for (int k = 0; k < n_meas; ++k) {
              SEXPTYPE t = meas_types[k];
              const void* mp = meas_ptrs[k];
              double v = NA_REAL;
              if (t == REALSXP) v = ((const double*)mp)[r];
              else if (t == INTSXP && !meas_is_factor[k]) {
                int x = ((const int*)mp)[r];
                v = (x == NA_INTEGER) ? NA_REAL : (double)x;
              } else if (t == LGLSXP) {
                int x = ((const int*)mp)[r];
                v = (x == NA_LOGICAL) ? NA_REAL : (x ? 1.0 : 0.0);
              }
              pval[r*n_meas + k] = v;
            }
            memcpy(pvar + (size_t)r * n_meas, var_pat8, (size_t)n_meas * sizeof(int));
            for (int j = 0; j < n_id; ++j) {
              SEXPTYPE t = id_types[j];
              if (t == INTSXP || t == LGLSXP)
                fill_int_exact((int*)out_id_ptrs[j] + r*n_meas, ((const int*)id_ptrs[j])[r], n_meas);
              else if (t == REALSXP)
                fill_double_exact((double*)out_id_ptrs[j] + r*n_meas, ((const double*)id_ptrs[j])[r], n_meas);
              else if (t == STRSXP) {
                SEXP v = ((const SEXP*)id_ptrs[j])[r];
                SEXP* dd = (SEXP*)out_id_ptrs[j] + r*n_meas;
                for (int k = 0; k < n_meas; ++k) dd[k] = v;
              }
            }
          }
        };
        if (use_parallel) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads)
#endif
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_general_block(i, iend);
          }
        } else {
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_general_block(i, iend);
          }
        }
      }
    } else {
#if defined(__AVX512F__)
      if (all_real_meas && n_meas >= 8) {
        alignas(64) int var_vec16[16] = {0};
        auto process_narm_block = [&](R_xlen_t ib, R_xlen_t iend) {
          R_xlen_t full_end = ib + ((iend - ib) / 8) * 8;
          for (R_xlen_t rb = ib; rb < full_end; rb += 8) {
            alignas(64) int written[8] = {0,0,0,0,0,0,0,0};
            for (int kb = 0; kb < n_meas; kb += 8) {
              int nc = (n_meas - kb < 8) ? (n_meas - kb) : 8;
              __m512d c[8];
              for (int kk = 0; kk < 8; ++kk)
                c[kk] = (kk < nc) ? _mm512_loadu_pd((const double*)meas_ptrs[kb+kk] + rb)
                                  : _mm512_set1_pd(NA_REAL);
              __m512d o0,o1,o2,o3,o4,o5,o6,o7;
              transpose8x8_avx512(c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7],o0,o1,o2,o3,o4,o5,o6,o7);
              __m512d ovec[8] = {o0,o1,o2,o3,o4,o5,o6,o7};
              for (int t = 0; t < 16; ++t) var_vec16[t] = 0;
              for (int kk = 0; kk < nc; ++kk) var_vec16[kk] = kb + kk + 1;
              __m512i kvec = _mm512_load_si512((const __m512i*)var_vec16);
              __mmask16 full = (nc == 8) ? 0xFF : (__mmask16)((1u << nc) - 1u);
              for (int i = 0; i < 8; ++i) {
                __m512d v = ovec[i];
                __mmask8 mask = _mm512_cmp_pd_mask(v, v, _CMP_EQ_OQ) & (__mmask8)full;
                if (mask == 0) continue;
                int cnt = __builtin_popcount((unsigned)mask);
                R_xlen_t dst = row_off[rb+i] + written[i];
                _mm512_mask_compressstoreu_pd(pval + dst, mask, v);
                _mm512_mask_compressstoreu_epi32(pvar + dst, (__mmask16)mask, kvec);
                written[i] += cnt;
              }
            }
            for (int i = 0; i < 8; ++i) {
              R_xlen_t r = rb + i;
              R_xlen_t o_start = row_off[r];
              int cnt = written[i];
              if (cnt == 0) continue;
              for (int j = 0; j < n_id; ++j) {
                SEXPTYPE t = id_types[j];
                if (t == INTSXP || t == LGLSXP)
                  fill_int_exact((int*)out_id_ptrs[j] + o_start, ((const int*)id_ptrs[j])[r], cnt);
                else if (t == REALSXP)
                  fill_double_exact((double*)out_id_ptrs[j] + o_start, ((const double*)id_ptrs[j])[r], cnt);
                else if (t == STRSXP) {
                  SEXP v = ((const SEXP*)id_ptrs[j])[r];
                  SEXP* d = (SEXP*)out_id_ptrs[j] + o_start;
                  for (int k = 0; k < cnt; ++k) d[k] = v;
                }
              }
            }
          }
          for (R_xlen_t r = full_end; r < iend; ++r) {
            R_xlen_t o = row_off[r];
            R_xlen_t cnt = row_off[r+1] - o;
            if (cnt == 0) continue;
            R_xlen_t j = o;
            for (int k = 0; k < n_meas; ++k) {
              double v = ((const double*)meas_ptrs[k])[r];
              if (ISNAN(v)) continue;
              pval[j] = v; pvar[j] = k + 1; ++j;
            }
            for (int ii = 0; ii < n_id; ++ii) {
              SEXPTYPE it = id_types[ii];
              if (it == INTSXP || it == LGLSXP)
                fill_int_exact((int*)out_id_ptrs[ii] + o, ((const int*)id_ptrs[ii])[r], (int)cnt);
              else if (it == REALSXP)
                fill_double_exact((double*)out_id_ptrs[ii] + o, ((const double*)id_ptrs[ii])[r], (int)cnt);
              else if (it == STRSXP) {
                SEXP v = ((const SEXP*)id_ptrs[ii])[r];
                SEXP* d = (SEXP*)out_id_ptrs[ii] + o;
                for (R_xlen_t jj = 0; jj < cnt; ++jj) d[jj] = v;
              }
            }
          }
        };
        if (use_parallel) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads)
#endif
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_narm_block(i, iend);
          }
        } else {
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_narm_block(i, iend);
          }
        }
      } else
#endif
      {
        auto process_scalar_block = [&](R_xlen_t ib, R_xlen_t iend) {
          for (R_xlen_t r = ib; r < iend; ++r) {
            R_xlen_t o   = row_off[r];
            R_xlen_t cnt = row_off[r+1] - o;
            if (cnt == 0) continue;
            R_xlen_t j = o;
            for (int k = 0; k < n_meas; ++k) {
              if (is_na_meas_at(meas_ptrs[k], meas_types[k], meas_is_factor[k], r)) continue;
              pval[j] = meas_at_as_double(meas_ptrs[k], meas_types[k], meas_is_factor[k], r);
              pvar[j] = k + 1;
              ++j;
            }
            for (int ii = 0; ii < n_id; ++ii) {
              SEXPTYPE it = id_types[ii];
              if (it == INTSXP || it == LGLSXP)
                fill_int_exact((int*)out_id_ptrs[ii] + o, ((const int*)id_ptrs[ii])[r], (int)cnt);
              else if (it == REALSXP)
                fill_double_exact((double*)out_id_ptrs[ii] + o, ((const double*)id_ptrs[ii])[r], (int)cnt);
              else {
                SEXP v = ((const SEXP*)id_ptrs[ii])[r];
                SEXP* d = (SEXP*)out_id_ptrs[ii] + o;
                for (R_xlen_t jj = 0; jj < cnt; ++jj) d[jj] = v;
              }
            }
          }
        };
        if (use_parallel) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads)
#endif
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_scalar_block(i, iend);
          }
        } else {
          for (R_xlen_t i = 0; i < n; i += BLOCK_SIZE) {
            R_xlen_t iend = std::min(i + BLOCK_SIZE, n);
            process_scalar_block(i, iend);
          }
        }
      }
    }
  }

  if (used_nt_store) {
#if defined(__AVX512F__) || defined(__AVX2__)
    _mm_sfence();
#endif
  }

  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = (total_out <= (R_xlen_t)INT_MAX) ? -(int)total_out : NA_INTEGER;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    Rf_setAttrib(out, R_ClassSymbol, g_df_class("data.frame"));
    UNPROTECT(1);
  }
  UNPROTECT(4);
  return out;
}