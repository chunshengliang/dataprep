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
#include <unordered_map>
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
//    Disabled by default; opt in via DATAPREP_HUGEPAGE.
// ======================================================
enum class HP { None, MB2, GB1 };

static HP detect_hp_mode() {
  const char* env = getenv("DATAPREP_HUGEPAGE");
  if (env) {
    if (strcmp(env, "none") == 0) return HP::None;
    if (strcmp(env, "1gb")  == 0) return HP::GB1;
    if (strcmp(env, "2mb")  == 0) return HP::MB2;
  }
  return HP::None;   // default: no hugepage
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
// 3. SIMD primitives (dcast subset)
// ======================================================
static void fill_double_fallback(double* d, double v, size_t n) {
  for (size_t i = 0; i < n; ++i) d[i] = v;
}

#ifdef __AVX2__
static void fill_double_avx2(double* d, double v, size_t n) {
  size_t i = 0;
  __m256d vv = _mm256_set1_pd(v);
  for (; i + 16 <= n; i += 16) {
    _mm256_storeu_pd(d + i,     vv);
    _mm256_storeu_pd(d + i + 4, vv);
    _mm256_storeu_pd(d + i + 8, vv);
    _mm256_storeu_pd(d + i + 12,vv);
  }
  for (; i + 4 <= n; i += 4) _mm256_storeu_pd(d + i, vv);
  for (; i < n; ++i) d[i] = v;
}
#endif

#ifdef __AVX512F__
static void fill_double_avx512(double* d, double v, size_t n) {
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

static void (*fill_double_ptr)(double*, double, size_t) = fill_double_fallback;

static void init_cpu_features() {
  static bool init = false;
  if (init) return;
  init = true;
  if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl")) {
#ifdef __AVX512F__
    fill_double_ptr = fill_double_avx512;
    return;
#endif
  }
  if (__builtin_cpu_supports("avx2")) {
#ifdef __AVX2__
    fill_double_ptr = fill_double_avx2;
#endif
  }
}

// ======================================================
// 4. Element-to-string conversion for key building
// ======================================================
static inline std::string int_to_string(int v) {
  if (v == NA_INTEGER) return "NA";
  char buf[16];
  char* p = buf + sizeof(buf);
  bool neg = (v < 0);
  unsigned int u = neg ? (unsigned)(-(int64_t)v) : (unsigned)v;
  do { *--p = char('0' + (u % 10)); u /= 10; } while (u);
  if (neg) *--p = '-';
  return std::string(p, buf + sizeof(buf) - p);
}

static inline std::string real_to_string(double v) {
  if (ISNA(v)) return "NA";
  char buf[32];
  snprintf(buf, sizeof(buf), "%.17g", v);
  return std::string(buf);
}

static inline void append_element_string(SEXP vec, R_xlen_t i, std::string& out) {
  switch (TYPEOF(vec)) {
  case INTSXP:  out += int_to_string(INTEGER(vec)[i]); break;
  case REALSXP: out += real_to_string(REAL(vec)[i]);   break;
  case LGLSXP: {
    int v = LOGICAL(vec)[i];
    out += (v == NA_LOGICAL) ? "NA" : (v ? "TRUE" : "FALSE");
    break;
  }
  case STRSXP:  out += CHAR(STRING_ELT(vec, i)); break;
  default: Rf_error("Unsupported column type in dcast");
  }
}

// ======================================================
// 5. Column spec resolution
// ======================================================
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
      for (int j = 0; j < ncols; ++j) {
        if (strcmp(CHAR(STRING_ELT(names, j)), tgt) == 0) {
          if (!seen[j]) { seen[j] = 1; id_cols.push_back(j); }
          found = true;
          break;
        }
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
  if (Rf_isNull(variable)) {
    for (int i = 0; i < ncols; ++i)
      if (strcmp(CHAR(STRING_ELT(names, i)), "variable") == 0) {
        variable = Rf_ScalarInteger(i + 1); break;
      }
  }
  if (Rf_isNull(value)) {
    for (int i = 0; i < ncols; ++i)
      if (strcmp(CHAR(STRING_ELT(names, i)), "value") == 0) {
        value = Rf_ScalarInteger(i + 1); break;
      }
  }
}

// ======================================================
// 6. Fill value parsing
// ======================================================
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

// ======================================================
// 7. Main entry point
// ======================================================
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

  // ---- parse fill ----
  double fill_val = NA_REAL;
  bool use_fill = parse_fill(fill, fill_val);

  // ---- thread count ----
  bool use_parallel = false;
  int actual_threads = 1;
#ifdef _OPENMP
  int hw = omp_get_max_threads();
  constexpr int MAX_THREADS_CAP = 64;
  if (cores > 0) {
    actual_threads = std::max(1, std::min(cores, hw));
  } else if (nlong >= 2000000) {
    int64_t calc = (int64_t)(nlong / 500000);
    actual_threads = (int)std::max<int64_t>(1, std::min<int64_t>(MAX_THREADS_CAP, calc));
  }
  actual_threads = std::min(actual_threads, hw);
  use_parallel = (actual_threads > 1) && (nlong > 50000);
  if (use_parallel) {
    omp_set_dynamic(0);
    omp_set_num_threads(actual_threads);
  }
#endif

  // ---- build keys ----
  bool var_is_factor = Rf_isFactor(var_col);
  SEXP var_levels = var_is_factor ? Rf_getAttrib(var_col, R_LevelsSymbol) : R_NilValue;

  std::unordered_map<std::string, R_xlen_t> row_map;
  std::unordered_map<std::string, int>      col_map;
  row_map.reserve((size_t)std::min<R_xlen_t>(nlong, (R_xlen_t)2e6));
  col_map.reserve(256);

  std::vector<std::string> row_keys;
  std::vector<std::string> col_keys;
  std::vector<R_xlen_t>    row_first_src;

  std::string key_buf;
  key_buf.reserve(64);

  for (R_xlen_t i = 0; i < nlong; ++i) {
    key_buf.clear();
    for (int j = 0; j < n_id; ++j) {
      if (j > 0) key_buf += '\r';
      append_element_string(VECTOR_ELT(data, id_cols[j]), i, key_buf);
    }
    if (row_map.find(key_buf) == row_map.end()) {
      R_xlen_t new_row = (R_xlen_t)row_keys.size();
      row_map.emplace(key_buf, new_row);
      row_keys.push_back(key_buf);
      row_first_src.push_back(i);
    }

    if (var_is_factor) {
      int code = INTEGER(var_col)[i];
      if (code == NA_INTEGER) key_buf.assign("NA");
      else                    key_buf.assign(CHAR(STRING_ELT(var_levels, code - 1)));
    } else {
      key_buf.clear();
      append_element_string(var_col, i, key_buf);
    }
    if (col_map.find(key_buf) == col_map.end()) {
      int new_col = (int)col_keys.size();
      col_map.emplace(key_buf, new_col);
      col_keys.push_back(key_buf);
    }
  }

  const R_xlen_t n_out = (R_xlen_t)row_keys.size();
  const int      k_out = (int)col_keys.size();
  if (n_out == 0 || k_out == 0) stop("empty output");

  // ---- allocate output ----
  int total_out_cols = n_id + k_out;
  SEXP out       = PROTECT(Rf_allocVector(VECSXP, total_out_cols));
  SEXP out_names = PROTECT(Rf_allocVector(STRSXP, total_out_cols));

  for (int j = 0; j < n_id; ++j) {
    int src_idx = id_cols[j];
    SEXP src = VECTOR_ELT(data, src_idx);
    SEXPTYPE t = TYPEOF(src);
    SEXP dst = PROTECT(alloc_smart(t, n_out));
    Rf_copyMostAttrib(src, dst);

    switch (t) {
    case INTSXP: {
      int* d = INTEGER(dst);
      const int* s = INTEGER(src);
      for (R_xlen_t i = 0; i < n_out; ++i) d[i] = s[row_first_src[i]];
      break;
    }
    case LGLSXP: {
      int* d = LOGICAL(dst);
      const int* s = LOGICAL(src);
      for (R_xlen_t i = 0; i < n_out; ++i) d[i] = s[row_first_src[i]];
      break;
    }
    case REALSXP: {
      double* d = REAL(dst);
      const double* s = REAL(src);
      for (R_xlen_t i = 0; i < n_out; ++i) d[i] = s[row_first_src[i]];
      break;
    }
    case STRSXP: {
      for (R_xlen_t i = 0; i < n_out; ++i)
        SET_STRING_ELT(dst, i, STRING_ELT(src, row_first_src[i]));
      break;
    }
    case VECSXP: {
      for (R_xlen_t i = 0; i < n_out; ++i)
        SET_VECTOR_ELT(dst, i, VECTOR_ELT(src, row_first_src[i]));
      break;
    }
    default:
      stop("unsupported id column type");
    }

    SET_VECTOR_ELT(out, j, dst);
    SET_STRING_ELT(out_names, j, STRING_ELT(id_names, src_idx));
    UNPROTECT(1);
  }

  SEXPTYPE val_type = TYPEOF(val_col);
  if (val_type != REALSXP && val_type != INTSXP && val_type != LGLSXP)
    stop("dcast_cpp only supports numeric/logical value columns");

  for (int k = 0; k < k_out; ++k) {
    SEXP vc = PROTECT(alloc_smart(REALSXP, n_out));
    fill_double_ptr(REAL(vc), use_fill ? fill_val : NA_REAL, (size_t)n_out);
    SET_VECTOR_ELT(out, n_id + k, vc);
    SET_STRING_ELT(out_names, n_id + k, Rf_mkChar(col_keys[k].c_str()));
    UNPROTECT(1);
  }

  // ---- precompute per-row (row_idx, col_idx) ----
  std::vector<int32_t> row_of(nlong);
  std::vector<int32_t> col_of(nlong);

  for (R_xlen_t i = 0; i < nlong; ++i) {
    key_buf.clear();
    for (int j = 0; j < n_id; ++j) {
      if (j > 0) key_buf += '\r';
      append_element_string(VECTOR_ELT(data, id_cols[j]), i, key_buf);
    }
    row_of[i] = (int32_t)row_map[key_buf];

    if (var_is_factor) {
      int code = INTEGER(var_col)[i];
      if (code == NA_INTEGER) key_buf.assign("NA");
      else                    key_buf.assign(CHAR(STRING_ELT(var_levels, code - 1)));
    } else {
      key_buf.clear();
      append_element_string(var_col, i, key_buf);
    }
    col_of[i] = (int32_t)col_map[key_buf];
  }

  // ---- scatter-write ----
  if (val_type == REALSXP) {
    const double* vs = REAL(val_col);
    std::vector<double*> out_val_ptrs(k_out);
    for (int k = 0; k < k_out; ++k)
      out_val_ptrs[k] = REAL(VECTOR_ELT(out, n_id + k));

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
    for (R_xlen_t i = 0; i < nlong; ++i) {
      double v = vs[i];
      if (na_rm && ISNAN(v)) continue;
      out_val_ptrs[col_of[i]][row_of[i]] = v;
    }
  } else if (val_type == INTSXP) {
    const int* vs = INTEGER(val_col);
    std::vector<double*> out_val_ptrs(k_out);
    for (int k = 0; k < k_out; ++k)
      out_val_ptrs[k] = REAL(VECTOR_ELT(out, n_id + k));

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
    for (R_xlen_t i = 0; i < nlong; ++i) {
      int v = vs[i];
      if (na_rm && v == NA_INTEGER) continue;
      out_val_ptrs[col_of[i]][row_of[i]] = (v == NA_INTEGER) ? NA_REAL : (double)v;
    }
  } else { // LGLSXP
    const int* vs = LOGICAL(val_col);
    std::vector<double*> out_val_ptrs(k_out);
    for (int k = 0; k < k_out; ++k)
      out_val_ptrs[k] = REAL(VECTOR_ELT(out, n_id + k));

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(actual_threads) if(use_parallel)
#endif
    for (R_xlen_t i = 0; i < nlong; ++i) {
      int v = vs[i];
      if (na_rm && v == NA_LOGICAL) continue;
      out_val_ptrs[col_of[i]][row_of[i]] = (v == NA_LOGICAL) ? NA_REAL : (v ? 1.0 : 0.0);
    }
  }

  Rf_setAttrib(out, R_NamesSymbol, out_names);
  {
    SEXP rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = -(int)n_out;
    Rf_setAttrib(out, R_RowNamesSymbol, rn);
    UNPROTECT(1);
  }
  Rf_setAttrib(out, R_ClassSymbol, Rf_mkString("data.frame"));

  UNPROTECT(2);
  return out;
}
