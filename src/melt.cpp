#include <Rcpp.h>
#include <R_ext/Rallocators.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <immintrin.h>
#include <string>
#include <cctype>

#ifdef __linux__
#include <dlfcn.h>
#include <sys/mman.h>
#include <cstdio>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace Rcpp;

// Portable replacement for the non-API DATAPTR().
// Covers INTSXP / REALSXP / LGLSXP only.
// STRSXP is handled separately via STRING_ELT() / SET_STRING_ELT()
// to avoid the non-API STRING_PTR().
static inline void* data_ptr_checked(SEXP x) {
  switch (TYPEOF(x)) {
  case INTSXP:  return (void*)INTEGER(x);
  case REALSXP: return (void*)REAL(x);
  case LGLSXP:  return (void*)LOGICAL(x);
  default:
    Rf_error("data_ptr_checked: unsupported SEXP type %d", (int)TYPEOF(x));
  }
  return nullptr;
}

// ======================================================
// 1. Dynamic jemalloc loading
// ======================================================
typedef void* (*jmalloc_t)(size_t);
typedef void  (*jfree_t)(void*);
static jmalloc_t jm_alloc = malloc;
static jfree_t   jm_free  = free;

static void init_jemalloc() {
  static bool init = false;
  if (init) return;
  init = true;
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

// ======================================================
// 2. Hugepage allocator (magic-header based)
//    Hugepages are DISABLED by default; they are enabled only
//    when the user sets the DATAPREP_HUGEPAGE environment variable.
// ======================================================
enum class HP { None, MB2, GB1 };

static HP detect_hp_mode() {
  const char* env = getenv("DATAPREP_HUGEPAGE");
  if (env) {
    if (strcmp(env, "none") == 0) return HP::None;
    if (strcmp(env, "1gb")  == 0) return HP::GB1;
    if (strcmp(env, "2mb")  == 0) return HP::MB2;
  }
  return HP::None;  // default: no hugepage
}

static constexpr size_t HUGE_THRESHOLD = 1ULL << 20;
static constexpr size_t HUGE_ALIGN     = 2ULL << 20;
static constexpr size_t GB_ALIGN       = 1ULL << 30;
static constexpr size_t POPULATE_LIMIT = 8ULL << 30;
static constexpr size_t HEAD_SIZE      = 64;

static constexpr uint32_t HP_MAGIC       = 0x4D454C54u;
static constexpr uint32_t HP_KIND_MALLOC = 0u;
static constexpr uint32_t HP_KIND_MMAP   = 1u;

struct Header {
  uint32_t magic;
  uint32_t kind;
  size_t   total;
  uint64_t pad0, pad1, pad2, pad3, pad4, pad5;
};
static_assert(sizeof(Header) <= HEAD_SIZE, "Header must fit in HEAD_SIZE");

static HP   g_mode      = HP::None;
static bool g_mode_init = false;

static void* try_mmap_1gb(size_t size, size_t& out_total) {
#if defined(__linux__) && defined(MAP_HUGETLB) && defined(MAP_HUGE_SHIFT)
  constexpr int SHIFT = 30;
  size_t total = (size + GB_ALIGN - 1) & ~(GB_ALIGN - 1);
  void* p = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB |
                   (SHIFT << MAP_HUGE_SHIFT), -1, 0);
  if (p != MAP_FAILED) { out_total = total; return p; }
#else
  (void)size; (void)out_total;
#endif
  return MAP_FAILED;
}

static void* huge_alloc(R_allocator_t*, size_t size) {
  if (!g_mode_init) { g_mode = detect_hp_mode(); g_mode_init = true; }

  if (__builtin_expect(size < HUGE_THRESHOLD || g_mode == HP::None, 1)) {
    void* p = jm_alloc(size + HEAD_SIZE);
    if (__builtin_expect(!p, 0)) Rf_error("allocator: OOM (size=%zu)", size);
    Header* h = (Header*)p;
    h->magic = HP_MAGIC; h->kind = HP_KIND_MALLOC; h->total = size;
    return (char*)p + HEAD_SIZE;
  }

  void*  raw   = MAP_FAILED;
  size_t total = 0;
  size_t alloc_size = size + HEAD_SIZE;

  if (g_mode == HP::GB1) raw = try_mmap_1gb(alloc_size, total);

  if (raw == MAP_FAILED) {
    total = (alloc_size + HUGE_ALIGN - 1) & ~(HUGE_ALIGN - 1);
    raw = mmap(nullptr, total, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (__builtin_expect(raw == MAP_FAILED, 0)) {
      void* p = jm_alloc(size + HEAD_SIZE);
      if (!p) Rf_error("allocator: OOM (mmap fallback size=%zu)", size);
      Header* h = (Header*)p;
      h->magic = HP_MAGIC; h->kind = HP_KIND_MALLOC; h->total = size;
      return (char*)p + HEAD_SIZE;
    }
#ifdef MADV_HUGEPAGE
    madvise(raw, total, MADV_HUGEPAGE);
#endif
  }

  Header* h = (Header*)raw;
  h->magic = HP_MAGIC; h->kind = HP_KIND_MMAP; h->total = total;

  void* user_ptr = (char*)raw + HEAD_SIZE;

  static int want_populate = -1;
  if (want_populate < 0) {
    const char* e = getenv("DATAPREP_POPULATE");
    want_populate = (e && e[0] == '1') ? 1 : 0;
  }
#ifdef MADV_POPULATE_WRITE
  if (want_populate && total <= POPULATE_LIMIT)
    madvise(user_ptr, size, MADV_POPULATE_WRITE);
#endif
  return user_ptr;
}

static void huge_free(R_allocator_t*, void* p) {
  if (__builtin_expect(!p || p == MAP_FAILED, 1)) return;
  Header* h = (Header*)((char*)p - HEAD_SIZE);
  if (__builtin_expect(h->magic != HP_MAGIC, 0)) { jm_free(h); return; }
  if (h->kind == HP_KIND_MALLOC) { jm_free(h); return; }
  size_t sz = h->total;
#ifdef MADV_DONTNEED
  madvise(h, sz, MADV_DONTNEED);
#endif
  munmap(h, sz);
}

static R_allocator_t huge_allocator = { huge_alloc, huge_free };

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
  size_t bytes = (size_t)n * sizeof_sexp(type);
  if (__builtin_expect(bytes < HUGE_THRESHOLD, 1)) return Rf_allocVector(type, n);
  return Rf_allocVector3(type, n, &huge_allocator);
}

// ======================================================
// 3. SIMD primitives
// ======================================================
static void nt_memcpy_fallback(void* d, const void* s, size_t n) { memcpy(d, s, n); }

#ifdef __AVX2__
static void nt_memcpy_avx2(void* dst, const void* src, size_t bytes) {
  const uint8_t* s = (const uint8_t*)src;
  uint8_t* d = (uint8_t*)dst;
  size_t i = 0;
  size_t align = (32 - ((uintptr_t)d & 31)) & 31;
  if (align && align <= bytes) { memcpy(d, s, align); i += align; }
  for (; i + 128 <= bytes; i += 128) {
    __m256i v0 = _mm256_loadu_si256((const __m256i*)(s + i));
    __m256i v1 = _mm256_loadu_si256((const __m256i*)(s + i + 32));
    __m256i v2 = _mm256_loadu_si256((const __m256i*)(s + i + 64));
    __m256i v3 = _mm256_loadu_si256((const __m256i*)(s + i + 96));
    _mm256_stream_si256((__m256i*)(d + i),      v0);
    _mm256_stream_si256((__m256i*)(d + i + 32), v1);
    _mm256_stream_si256((__m256i*)(d + i + 64), v2);
    _mm256_stream_si256((__m256i*)(d + i + 96), v3);
    _mm_prefetch((const char*)(s + i + 256), _MM_HINT_NTA);
  }
  for (; i + 32 <= bytes; i += 32) {
    __m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
    _mm256_stream_si256((__m256i*)(d + i), v);
  }
  if (i < bytes) memcpy(d + i, s + i, bytes - i);
}
#endif

#ifdef __AVX512F__
static void nt_memcpy_avx512(void* dst, const void* src, size_t bytes) {
  const uint8_t* s = (const uint8_t*)src;
  uint8_t* d = (uint8_t*)dst;
  size_t i = 0;
  size_t align = (64 - ((uintptr_t)d & 63)) & 63;
  if (align && align <= bytes) { memcpy(d, s, align); i += align; }
  for (; i + 256 <= bytes; i += 256) {
    __m512i v0 = _mm512_loadu_si512((const __m512i*)(s + i));
    __m512i v1 = _mm512_loadu_si512((const __m512i*)(s + i + 64));
    __m512i v2 = _mm512_loadu_si512((const __m512i*)(s + i + 128));
    __m512i v3 = _mm512_loadu_si512((const __m512i*)(s + i + 192));
    _mm512_stream_si512((__m512i*)(d + i),       v0);
    _mm512_stream_si512((__m512i*)(d + i + 64),  v1);
    _mm512_stream_si512((__m512i*)(d + i + 128), v2);
    _mm512_stream_si512((__m512i*)(d + i + 192), v3);
    _mm_prefetch((const char*)(s + i + 512), _MM_HINT_NTA);
  }
  for (; i + 64 <= bytes; i += 64) {
    __m512i v = _mm512_loadu_si512((const __m512i*)(s + i));
    _mm512_stream_si512((__m512i*)(d + i), v);
  }
  if (i < bytes) memcpy(d + i, s + i, bytes - i);
}
#endif

static void nt_fill_int_fallback(int* d, int v, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = v; }

#ifdef __AVX2__
static void nt_fill_int_avx2(int* dst, int val, size_t n) {
  size_t i = 0; __m256i v = _mm256_set1_epi32(val);
  size_t align = (32 - ((uintptr_t)dst & 31)) & 31;
  size_t cnt = align / sizeof(int); if (cnt > n) cnt = n;
  for (size_t j = 0; j < cnt; ++j) dst[j] = val;
  i = cnt;
  for (; i + 32 <= n; i += 32) {
    _mm256_stream_si256((__m256i*)(dst + i),      v);
    _mm256_stream_si256((__m256i*)(dst + i + 8),  v);
    _mm256_stream_si256((__m256i*)(dst + i + 16), v);
    _mm256_stream_si256((__m256i*)(dst + i + 24), v);
  }
  for (; i + 8 <= n; i += 8) _mm256_stream_si256((__m256i*)(dst + i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

#ifdef __AVX512F__
static void nt_fill_int_avx512(int* dst, int val, size_t n) {
  size_t i = 0; __m512i v = _mm512_set1_epi32(val);
  size_t align = (64 - ((uintptr_t)dst & 63)) & 63;
  size_t cnt = align / sizeof(int); if (cnt > n) cnt = n;
  for (size_t j = 0; j < cnt; ++j) dst[j] = val;
  i = cnt;
  for (; i + 64 <= n; i += 64) {
    _mm512_stream_si512((__m512i*)(dst + i),      v);
    _mm512_stream_si512((__m512i*)(dst + i + 16), v);
    _mm512_stream_si512((__m512i*)(dst + i + 32), v);
    _mm512_stream_si512((__m512i*)(dst + i + 48), v);
  }
  for (; i + 16 <= n; i += 16) _mm512_stream_si512((__m512i*)(dst + i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

static inline void fill_i32_helper(int* p, int v, size_t n) {
  size_t i = 0;
#ifdef __AVX2__
  __m256i vv = _mm256_set1_epi32(v);
  for (; i + 32 <= n; i += 32) {
    _mm256_storeu_si256((__m256i*)(p + i),      vv);
    _mm256_storeu_si256((__m256i*)(p + i + 8),  vv);
    _mm256_storeu_si256((__m256i*)(p + i + 16), vv);
    _mm256_storeu_si256((__m256i*)(p + i + 24), vv);
  }
  for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i*)(p + i), vv);
#endif
  for (; i < n; ++i) p[i] = v;
}

static void broadcast_int_fallback(int* dst, int val, size_t n) { for (size_t i = 0; i < n; ++i) dst[i] = val; }

#ifdef __AVX2__
static void broadcast_int_avx2(int* dst, int val, size_t n) {
  size_t i = 0; __m256i v = _mm256_set1_epi32(val);
  for (; i + 32 <= n; i += 32) {
    _mm256_storeu_si256((__m256i*)(dst + i),      v);
    _mm256_storeu_si256((__m256i*)(dst + i + 8),  v);
    _mm256_storeu_si256((__m256i*)(dst + i + 16), v);
    _mm256_storeu_si256((__m256i*)(dst + i + 24), v);
  }
  for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i*)(dst + i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

#ifdef __AVX512F__
static void broadcast_int_avx512(int* dst, int val, size_t n) {
  size_t i = 0; __m512i v = _mm512_set1_epi32(val);
  for (; i + 64 <= n; i += 64) {
    _mm512_storeu_si512((__m512i*)(dst + i),      v);
    _mm512_storeu_si512((__m512i*)(dst + i + 16), v);
    _mm512_storeu_si512((__m512i*)(dst + i + 32), v);
    _mm512_storeu_si512((__m512i*)(dst + i + 48), v);
  }
  for (; i + 16 <= n; i += 16) _mm512_storeu_si512((__m512i*)(dst + i), v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

static void broadcast_double_fallback(double* dst, double val, size_t n) { for (size_t i = 0; i < n; ++i) dst[i] = val; }

#ifdef __AVX2__
static void broadcast_double_avx2(double* dst, double val, size_t n) {
  size_t i = 0; __m256d v = _mm256_set1_pd(val);
  for (; i + 16 <= n; i += 16) {
    _mm256_storeu_pd(dst + i,      v);
    _mm256_storeu_pd(dst + i + 4,  v);
    _mm256_storeu_pd(dst + i + 8,  v);
    _mm256_storeu_pd(dst + i + 12, v);
  }
  for (; i + 4 <= n; i += 4) _mm256_storeu_pd(dst + i, v);
  for (; i < n; ++i) dst[i] = val;
}
#endif

#ifdef __AVX512F__
static void broadcast_double_avx512(double* dst, double val, size_t n) {
  size_t i = 0; __m512d v = _mm512_set1_pd(val);
  for (; i + 32 <= n; i += 32) {
    _mm512_storeu_pd(dst + i,      v);
    _mm512_storeu_pd(dst + i + 8,  v);
    _mm512_storeu_pd(dst + i + 16, v);
    _mm512_storeu_pd(dst + i + 24, v);
  }
  for (; i + 8 <= n; i += 8) _mm512_storeu_pd(dst + i, v);
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
      __m256i tm_lo = _mm256_cmpeq_epi32(lo_b, _mm256_set1_epi32(1));
      __m256i tm_hi = _mm256_cmpeq_epi32(hi_b, _mm256_set1_epi32(1));
      r_lo = _mm256_blendv_pd(zero, one, _mm256_castsi256_pd(tm_lo));
      r_hi = _mm256_blendv_pd(zero, one, _mm256_castsi256_pd(tm_hi));
    } else {
      r_lo = _mm256_cvtepi32_pd(lo);
      r_hi = _mm256_cvtepi32_pd(hi);
    }
    r_lo = _mm256_blendv_pd(r_lo, na_real, _mm256_castsi256_pd(na_lo));
    r_hi = _mm256_blendv_pd(r_hi, na_real, _mm256_castsi256_pd(na_hi));
    _mm256_stream_pd(dst + i,     r_lo);
    _mm256_stream_pd(dst + i + 4, r_hi);
    _mm_prefetch((const char*)(src + i + 32), _MM_HINT_NTA);
  }
  for (; i < n; ++i) {
    int v = src[i];
    if (is_logical) dst[i] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
    else            dst[i] = (v == NA_INTEGER) ? NA_REAL : (double)v;
  }
}
#endif

#ifdef __AVX512F__
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
      __mmask8 tm_lo = _mm256_cmpneq_epi32_mask(lo, _mm256_setzero_si256());
      __mmask8 tm_hi = _mm256_cmpneq_epi32_mask(hi, _mm256_setzero_si256());
      r_lo = _mm512_mask_blend_pd(tm_lo, zero, one);
      r_hi = _mm512_mask_blend_pd(tm_hi, zero, one);
    } else {
      r_lo = _mm512_cvtepi32_pd(lo);
      r_hi = _mm512_cvtepi32_pd(hi);
    }
    r_lo = _mm512_mask_blend_pd(na_lo, r_lo, na_real);
    r_hi = _mm512_mask_blend_pd(na_hi, r_hi, na_real);
    _mm512_stream_pd(dst + i,     r_lo);
    _mm512_stream_pd(dst + i + 8, r_hi);
    _mm_prefetch((const char*)(src + i + 64), _MM_HINT_NTA);
  }
  for (; i < n; ++i) {
    int v = src[i];
    if (is_logical) dst[i] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
    else            dst[i] = (v == NA_INTEGER) ? NA_REAL : (double)v;
  }
}
#endif

static void (*nt_memcpy_ptr)(void*, const void*, size_t) = nt_memcpy_fallback;
static void (*nt_fill_int_ptr)(int*, int, size_t) = nt_fill_int_fallback;
static void (*broadcast_int_ptr)(int*, int, size_t) = broadcast_int_fallback;
static void (*broadcast_double_ptr)(double*, double, size_t) = broadcast_double_fallback;
static void (*int2double_ptr)(const int*, double*, size_t, bool) = int2double_fallback;

static void init_cpu_features() {
  static bool init = false;
  if (init) return;
  init = true;
  if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl")) {
#ifdef __AVX512F__
    nt_memcpy_ptr   = nt_memcpy_avx512;
    nt_fill_int_ptr = nt_fill_int_avx512;
    broadcast_int_ptr    = broadcast_int_avx512;
    broadcast_double_ptr = broadcast_double_avx512;
    int2double_ptr  = int2double_avx512;
    return;
#endif
  }
  if (__builtin_cpu_supports("avx2")) {
#ifdef __AVX2__
    nt_memcpy_ptr   = nt_memcpy_avx2;
    nt_fill_int_ptr = nt_fill_int_avx2;
    broadcast_int_ptr    = broadcast_int_avx2;
    broadcast_double_ptr = broadcast_double_avx2;
    int2double_ptr  = int2double_avx2;
#endif
  }
}

// ======================================================
// 4. Column index parsing
// ======================================================
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
    for (int i = 0; i < ncols; ++i)
      (is_id[i] ? id_idx : meas_idx).push_back(i);
    return;
  }
  if (TYPEOF(id_spec) == STRSXP) {
    R_xlen_t len = XLENGTH(id_spec);
    std::vector<char> is_id(ncols, 0);
    for (R_xlen_t i = 0; i < len; ++i) {
      const char* target = CHAR(STRING_ELT(id_spec, i));
      for (int j = 0; j < ncols; ++j)
        if (strcmp(CHAR(STRING_ELT(names, j)), target) == 0) { is_id[j] = 1; break; }
    }
    for (int i = 0; i < ncols; ++i)
      (is_id[i] ? id_idx : meas_idx).push_back(i);
    return;
  }
  stop("invalid id argument");
}

// ======================================================
// 5. Main entry point
// ======================================================
// [[Rcpp::export]]
SEXP melt_cpp(SEXP df, SEXP id = R_NilValue,
              SEXP variable_name = R_NilValue, SEXP value_name = R_NilValue,
              SEXP major = R_NilValue, int n_threads = 0) {
  init_jemalloc();
  init_cpu_features();

  bool row_major = true;
  if (!Rf_isNull(major) && TYPEOF(major) == STRSXP && XLENGTH(major) > 0) {
    std::string s = CHAR(STRING_ELT(major, 0));
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "col" || s == "variable") row_major = false;
  }

  std::vector<int> id_idx, meas_idx;
  parse_id_arg(df, id, id_idx, meas_idx);
  int n_id   = (int)id_idx.size();
  int n_meas = (int)meas_idx.size();
  if (n_meas == 0) stop("no measure variables");

  SEXP col_names = Rf_getAttrib(df, R_NamesSymbol);
  R_xlen_t n     = XLENGTH(VECTOR_ELT(df, 0));
  R_xlen_t total = n * n_meas;

  {
    size_t est = (size_t)total * 12 + (size_t)total * 4 * (size_t)n_id;
    if (est >= (2ULL << 30)) R_gc();
  }

  SEXP out = PROTECT(alloc_smart(VECSXP, n_id + 2));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, n_id + 2));

  std::vector<void*>    id_ptrs;
  std::vector<SEXPTYPE> id_types;
  std::vector<void*>    out_id_ptrs;
  std::vector<size_t>   id_elem_sizes;

  for (int i = 0; i < n_id; ++i) {
    SEXP src = VECTOR_ELT(df, id_idx[i]);
    SEXP dst = PROTECT(alloc_smart(TYPEOF(src), total));
    Rf_copyMostAttrib(src, dst);
    SET_VECTOR_ELT(out, i, dst);
    SET_STRING_ELT(out_names, i, STRING_ELT(col_names, id_idx[i]));
    SEXPTYPE st = TYPEOF(src);
    // STRSXP is handled via STRING_ELT / SET_STRING_ELT later; no raw pointer.
    id_ptrs.push_back(st == STRSXP ? (void*)nullptr : data_ptr_checked(src));
    id_types.push_back(st);
    out_id_ptrs.push_back(st == STRSXP ? (void*)nullptr : data_ptr_checked(dst));
    id_elem_sizes.push_back(sizeof_sexp(st));
    UNPROTECT(1);
  }

  SEXP var_col = PROTECT(alloc_smart(INTSXP, total));
  int* pvar = INTEGER(var_col);
  SET_VECTOR_ELT(out, n_id, var_col);
  SET_STRING_ELT(out_names, n_id,
                 (!Rf_isNull(variable_name) && TYPEOF(variable_name) == STRSXP) ?
                   STRING_ELT(variable_name, 0) : Rf_mkChar("variable"));
  {
    SEXP levels = PROTECT(Rf_allocVector(STRSXP, n_meas));
    for (int k = 0; k < n_meas; ++k)
      SET_STRING_ELT(levels, k, STRING_ELT(col_names, meas_idx[k]));
    Rf_setAttrib(var_col, R_LevelsSymbol, levels);
    Rf_setAttrib(var_col, R_ClassSymbol, Rf_mkString("factor"));
    UNPROTECT(1);
  }

  SEXP val_col = PROTECT(alloc_smart(REALSXP, total));
  double* pval = REAL(val_col);
  SET_VECTOR_ELT(out, n_id + 1, val_col);
  SET_STRING_ELT(out_names, n_id + 1,
                 (!Rf_isNull(value_name) && TYPEOF(value_name) == STRSXP) ?
                   STRING_ELT(value_name, 0) : Rf_mkChar("value"));

  std::vector<void*>    meas_ptrs;
  std::vector<SEXPTYPE> meas_types;
  std::vector<bool>     meas_is_factor;
  for (int k = 0; k < n_meas; ++k) {
    SEXP col = VECTOR_ELT(df, meas_idx[k]);
    SEXPTYPE st = TYPEOF(col);
    meas_ptrs.push_back(st == STRSXP ? (void*)nullptr : data_ptr_checked(col));
    meas_types.push_back(st);
    meas_is_factor.push_back(Rf_isFactor(col));
  }

  // ============================================================
  // Parallelism strategy
  // ============================================================
  bool use_parallel = false;
  int actual_threads = 1;
#ifdef _OPENMP
  int hw = omp_get_max_threads();
  constexpr R_xlen_t ELEMS_PER_THREAD = 2000000LL;
  constexpr int MAX_THREADS_CAP = 64;
  constexpr R_xlen_t MIN_PARALLEL = 50000LL;

  int target;
  if (n_threads > 0) {
    target = std::min(n_threads, hw);
    target = std::max(1, target);
  } else if (total < ELEMS_PER_THREAD) {
    target = 1;
  } else {
    R_xlen_t calc = total / ELEMS_PER_THREAD;
    target = (int)std::min<R_xlen_t>(MAX_THREADS_CAP, calc);
    target = std::max(1, target);
  }
  target = std::min(target, hw);
  actual_threads = target;
  use_parallel = (actual_threads > 1) && (total > MIN_PARALLEL);

  if (use_parallel) {
    omp_set_dynamic(0);
    omp_set_num_threads(actual_threads);
  }
#endif

  // ============================================================
  // Column-major path (reshape2 style)
  // ============================================================
  if (!row_major) {
    size_t l3_size = 32ULL << 20;
#ifdef __linux__
    {
      FILE* f = fopen("/sys/devices/system/cpu/cpu0/cache/index3/size", "r");
      if (f) {
        char buf[32] = {0};
        if (fgets(buf, sizeof(buf), f)) {
          char* p = buf;
          while (*p && !isdigit((unsigned char)*p)) ++p;
          unsigned long long v = strtoull(p, nullptr, 10);
          if (strchr(p, 'M') || strchr(p, 'm')) v <<= 20;
          else if (strchr(p, 'K') || strchr(p, 'k')) v <<= 10;
          if (v >= (1ULL << 20)) l3_size = (size_t)v;
        }
        fclose(f);
      }
    }
#endif

    const size_t val_bytes_total = (size_t)total * sizeof(double);
    const size_t var_bytes_total = (size_t)total * sizeof(int);
    size_t id_bytes_total = 0;
    for (size_t es : id_elem_sizes) id_bytes_total += (size_t)total * es;

    const bool nt_val = val_bytes_total >= 2 * l3_size;
    const bool nt_var = var_bytes_total >= 2 * l3_size;
    const bool nt_id  = id_bytes_total >= 2 * l3_size;

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

      if (nt_id && len >= 8192) {
        for (int i = 0; i < n_id; ++i) {
          size_t es = id_elem_sizes[i];
          if (!es || id_ptrs[i] == nullptr) continue;
          char*       dst = (char*)out_id_ptrs[i] + (R_xlen_t)out_off * es;
          const char* src = (const char*)id_ptrs[i] + (size_t)r0 * es;
          nt_memcpy_ptr(dst, src, (size_t)len * es);
        }
      } else {
        for (int i = 0; i < n_id; ++i) {
          size_t es = id_elem_sizes[i];
          if (!es || id_ptrs[i] == nullptr) continue;
          char*       dst = (char*)out_id_ptrs[i] + (R_xlen_t)out_off * es;
          const char* src = (const char*)id_ptrs[i] + (size_t)r0 * es;
          memcpy(dst, src, (size_t)len * es);
        }
      }

      if (!var_split) {
        if (nt_var && len >= 8192) {
          nt_fill_int_ptr(pvar + out_off, k + 1, (size_t)len);
        } else {
          fill_i32_helper(pvar + out_off, k + 1, (size_t)len);
        }
      }

      double* val_dst = pval + out_off;
      SEXPTYPE type = meas_types[k];
      if (type == REALSXP) {
        const double* src = (const double*)meas_ptrs[k] + r0;
        size_t bytes = (size_t)len * sizeof(double);
        if (nt_val && bytes >= 8192 * sizeof(double))
          nt_memcpy_ptr(val_dst, src, bytes);
        else
          memcpy(val_dst, src, bytes);
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
        for (int k = 0; k < n_meas; ++k) {
          fill_i32_helper(pvar + (R_xlen_t)k * n, k + 1, (size_t)n);
        }
      } else {
        for (int k = 0; k < n_meas; ++k)
          fill_i32_helper(pvar + (R_xlen_t)k * n, k + 1, (size_t)n);
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
  }
  // ============================================================
  // Row-major path (tidyr style)
  // ============================================================
  else {
    std::vector<int> var_pat(n_meas);
    for (int k = 0; k < n_meas; ++k) var_pat[k] = k + 1;
    const int* var_pat_ptr = var_pat.data();

    R_xlen_t BLOCK_SIZE;
    if (use_parallel) {
      R_xlen_t by_threads = n / (R_xlen_t)(4 * actual_threads);
      BLOCK_SIZE = std::max<R_xlen_t>(512, by_threads);
      if (BLOCK_SIZE > 32768) BLOCK_SIZE = 32768;
    } else {
      BLOCK_SIZE = 8192;
    }

    constexpr int TR = 8;
    constexpr int TC = 8;

    // ---- Narrow path: n_meas <= 32 ----
    if (n_meas <= 32) {
#ifdef _OPENMP
#pragma omp parallel num_threads(actual_threads) if(use_parallel)
#endif
{
  alignas(64) double val_tile[TR * 32];
  alignas(64) int    var_tile[TR * 32];

  for (int r = 0; r < TR; ++r)
    memcpy(var_tile + (size_t)r * n_meas,
           var_pat_ptr, (size_t)n_meas * sizeof(int));

#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
  for (R_xlen_t ib = 0; ib < n; ib += BLOCK_SIZE) {
    R_xlen_t iend = std::min(ib + BLOCK_SIZE, n);

    for (R_xlen_t rb = ib; rb < iend; rb += TR) {
      int rr = (iend - rb < (R_xlen_t)TR) ? (int)(iend - rb) : TR;

      for (int k = 0; k < n_meas; ++k) {
        SEXPTYPE t = meas_types[k];
        const void* mp = meas_ptrs[k];
        if (t == REALSXP) {
          const double* s = (const double*)mp + rb;
          for (int r = 0; r < rr; ++r)
            val_tile[(size_t)r * n_meas + k] = s[r];
        } else if (t == INTSXP && !meas_is_factor[k]) {
          const int* s = (const int*)mp + rb;
          for (int r = 0; r < rr; ++r) {
            int v = s[r];
            val_tile[(size_t)r * n_meas + k] =
              (v == NA_INTEGER) ? NA_REAL : (double)v;
          }
        } else if (t == LGLSXP) {
          const int* s = (const int*)mp + rb;
          for (int r = 0; r < rr; ++r) {
            int v = s[r];
            val_tile[(size_t)r * n_meas + k] =
              (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
          }
        } else {
          for (int r = 0; r < rr; ++r)
            val_tile[(size_t)r * n_meas + k] = NA_REAL;
        }
      }

      size_t vbytes = (size_t)rr * n_meas * sizeof(double);
      size_t ibytes = (size_t)rr * n_meas * sizeof(int);
      double* vd = pval + (size_t)rb * n_meas;

      if (vbytes >= (256ULL << 10))
        nt_memcpy_ptr(vd, val_tile, vbytes);
      else
        memcpy(vd, val_tile, vbytes);

      memcpy(pvar + (size_t)rb * n_meas, var_tile, ibytes);

      for (int j = 0; j < n_id; ++j) {
        SEXPTYPE t = id_types[j];
        if (t == INTSXP || t == LGLSXP) {
          const int* s = (const int*)id_ptrs[j] + rb;
          int* dst = (int*)out_id_ptrs[j] + (size_t)rb * n_meas;
          for (int r = 0; r < rr; ++r)
            broadcast_int_ptr(dst + (size_t)r * n_meas, s[r], (size_t)n_meas);
        } else if (t == REALSXP) {
          const double* s = (const double*)id_ptrs[j] + rb;
          double* dst = (double*)out_id_ptrs[j] + (size_t)rb * n_meas;
          for (int r = 0; r < rr; ++r)
            broadcast_double_ptr(dst + (size_t)r * n_meas, s[r], (size_t)n_meas);
        } else if (t == STRSXP) {
          SEXP src_vec = VECTOR_ELT(df, id_idx[j]);
          SEXP dst_vec = VECTOR_ELT(out, j);
          for (int r = 0; r < rr; ++r) {
            SEXP v = STRING_ELT(src_vec, (R_xlen_t)(rb + r));
            for (int k = 0; k < n_meas; ++k) {
              SET_STRING_ELT(dst_vec,
                             (R_xlen_t)(rb + r) * n_meas + k, v);
            }
          }
        }
      }
    }
  }
}
    }
    // ---- Wide path: n_meas > 32 ----
    else {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
      for (R_xlen_t ib = 0; ib < n; ib += BLOCK_SIZE) {
        R_xlen_t iend = std::min(ib + BLOCK_SIZE, n);
        R_xlen_t nr   = iend - ib;

        for (int j = 0; j < n_id; ++j) {
          SEXPTYPE t = id_types[j];
          if (t == INTSXP || t == LGLSXP) {
            const int* s = (const int*)id_ptrs[j] + ib;
            int* dst0 = (int*)out_id_ptrs[j] + (R_xlen_t)ib * n_meas;
            for (R_xlen_t r = 0; r < nr; ++r)
              broadcast_int_ptr(dst0 + r * n_meas, s[r], (size_t)n_meas);
          } else if (t == REALSXP) {
            const double* s = (const double*)id_ptrs[j] + ib;
            double* dst0 = (double*)out_id_ptrs[j] + (R_xlen_t)ib * n_meas;
            for (R_xlen_t r = 0; r < nr; ++r)
              broadcast_double_ptr(dst0 + r * n_meas, s[r], (size_t)n_meas);
          } else if (t == STRSXP) {
            SEXP src_vec = VECTOR_ELT(df, id_idx[j]);
            SEXP dst_vec = VECTOR_ELT(out, j);
            for (R_xlen_t r = 0; r < nr; ++r) {
              SEXP v = STRING_ELT(src_vec, ib + r);
              for (int k = 0; k < n_meas; ++k) {
                SET_STRING_ELT(dst_vec,
                               (R_xlen_t)(ib + r) * n_meas + k, v);
              }
            }
          }
        }

        for (R_xlen_t r = 0; r < nr; ++r) {
          memcpy(pvar + (R_xlen_t)(ib + r) * n_meas, var_pat_ptr,
                 (size_t)n_meas * sizeof(int));
        }

        {
          alignas(64) double tr_buf[TR][TC];
          for (int kb = 0; kb < n_meas; kb += TC) {
            int nc = (n_meas - kb < TC) ? (n_meas - kb) : TC;
            for (R_xlen_t rb = 0; rb < nr; rb += TR) {
              int rr = (nr - rb < TR) ? (int)(nr - rb) : TR;
              for (int kk = 0; kk < nc; ++kk) {
                int k = kb + kk;
                SEXPTYPE t = meas_types[k];
                const void* mp = meas_ptrs[k];
                if (t == REALSXP) {
                  const double* s = (const double*)mp + ib + rb;
                  for (int r = 0; r < rr; ++r) tr_buf[r][kk] = s[r];
                } else if (t == INTSXP && !meas_is_factor[k]) {
                  const int* s = (const int*)mp + ib + rb;
                  for (int r = 0; r < rr; ++r) {
                    int v = s[r];
                    tr_buf[r][kk] = (v == NA_INTEGER) ? NA_REAL : (double)v;
                  }
                } else if (t == LGLSXP) {
                  const int* s = (const int*)mp + ib + rb;
                  for (int r = 0; r < rr; ++r) {
                    int v = s[r];
                    tr_buf[r][kk] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
                  }
                } else {
                  for (int r = 0; r < rr; ++r) tr_buf[r][kk] = NA_REAL;
                }
              }
              for (int r = 0; r < rr; ++r) {
                memcpy(pval + (R_xlen_t)(ib + rb + r) * n_meas + kb,
                       tr_buf[r], (size_t)nc * sizeof(double));
              }
            }
          }
        }
      }
    }
  }

  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = -(int)total;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    Rf_setAttrib(out, R_ClassSymbol, Rf_mkString("data.frame"));
    UNPROTECT(1);
  }

  UNPROTECT(4);
  return out;
}