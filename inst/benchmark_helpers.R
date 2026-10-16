# ============================================================================
# Shared helpers for adaptive per-tool benchmarks (melt / dcast / agg dcast).
#
# Design notes (v15):
#   * bench_one runs the first call as a warmup and EXCLUDES it from the
#     reported statistics. The first call absorbs OpenMP thread-pool
#     creation, kernel THP initialisation, and glibc malloc pool growth.
#     All of these are one-time costs that every engine pays.
#   * Sentinel: a large numeric vector kept alive for the whole cell
#     forces R's GC trigger above any single tool's working set.
#   * Mixed-type inputs: id columns alternate between integer and
#     character for n_id >= 2, matching real composite keys.
#   * make_agg_input builds a long table where every (id, variable) pair
#     appears exactly K times. Any tool that silently takes "first"
#     instead of aggregating will differ from the base-R reference.
# ============================================================================

# ---- persistent helper state (survives re-source of this file) --------------
if (!exists(".bench_helpers_env", envir = globalenv(), inherits = FALSE)) {
  .bench_helpers_env <- new.env(parent = emptyenv())
}

# ---- Python bootstrap (strict) ---------------------------------------------
ensure_python <- function() {
  if (!requireNamespace("reticulate", quietly = TRUE))
    stop("Package 'reticulate' is required.", call. = FALSE)

  if (!reticulate::py_available(initialize = TRUE)) {
    stop(
      "Python is not available to reticulate.\n",
      "  * Set RETICULATE_PYTHON to a Python interpreter, e.g.\n",
      "      Sys.setenv(RETICULATE_PYTHON =\n",
      "        \"C:/path/to/python.exe\")\n",
      "    or write it into ~/.Renviron as:\n",
      "      RETICULATE_PYTHON=C:/path/to/python.exe\n",
      "  * Then restart R and re-run the benchmark.\n",
      "The benchmark harness will not install Python for you.",
      call. = FALSE
    )
  }

  cfg <- reticulate::py_config()
  # Announce the interpreter only once per R session, even if this file
  # is sourced several times by the driver script.
  if (!isTRUE(.bench_helpers_env$py_announced)) {
    message("Using Python: ", cfg$python)
    .bench_helpers_env$py_announced <- TRUE
  }
  invisible(TRUE)
}

# ---- Python dependencies (strict) -------------------------------------------
PY_PKGS <- list(
  list(module = "pandas",         pip = "pandas"),
  list(module = "polars",         pip = "polars"),
  list(module = "duckdb",         pip = "duckdb"),
  list(module = "pyarrow",        pip = "pyarrow"),
  list(module = "dask",           pip = "dask[dataframe]"),
  list(module = "dask.dataframe", pip = "dask[dataframe]")
)

ensure_py_pkgs <- function(pkgs = PY_PKGS) {
  module_ok <- vapply(pkgs, function(p) {
    reticulate::py_module_available(p$module)
  }, logical(1))

  missing <- pkgs[!module_ok]
  if (length(missing) == 0) return(invisible(TRUE))

  mod_names <- vapply(missing, function(p) p$module, character(1))
  pip_specs <- unique(vapply(missing, function(p) p$pip, character(1)))

  cfg_py <- tryCatch(reticulate::py_config()$python,
                     error = function(e) "<unknown>")

  stop(
    "Missing Python modules: ", paste(mod_names, collapse = ", "), "\n",
    "  Interpreter: ", cfg_py, "\n",
    "  Install them manually, e.g.:\n",
    "    reticulate::py_install(c(",
    paste(sprintf('"%s"', pip_specs), collapse = ", "),
    "), pip = TRUE)\n",
    "  or from a shell:\n",
    "    \"", cfg_py, "\" -m pip install ",
    paste(pip_specs, collapse = " "), "\n",
    "The benchmark harness will not install Python packages for you.",
    call. = FALSE
  )
}

# ---- high-resolution timer --------------------------------------------------
if (!exists("now_ns", mode = "function", inherits = FALSE)) {
  if (!requireNamespace("Rcpp", quietly = TRUE))
    stop("The benchmark helpers require the Rcpp package.")
  Rcpp::cppFunction(env = globalenv(), code = '
    #include <chrono>
    double now_ns() {
      return (double) std::chrono::duration_cast<
               std::chrono::nanoseconds>(
               std::chrono::steady_clock::now()
                 .time_since_epoch()).count();
    }
  ')
}
if (!exists("now_ns", mode = "function", inherits = FALSE))
  stop("Failed to define now_ns(); check for a C++ compiler.")


# ---- Python GC helper -------------------------------------------------------
py_gc_collect <- function() {
  if (!requireNamespace("reticulate", quietly = TRUE)) return(invisible(NULL))
  if (!reticulate::py_available(FALSE)) return(invisible(NULL))
  reticulate::py_eval("__import__('gc').collect()", convert = FALSE)
  invisible(NULL)
}


# ---- enable / disable gate --------------------------------------------------
.run_bench <- nzchar(Sys.getenv("DATAPREP_RUN_BENCHMARK"))
if (!.run_bench) {
  message("Benchmark scripts are disabled. ",
          "Set DATAPREP_RUN_BENCHMARK=1 to enable.")
}

if (.run_bench) {
  required <- c("Rcpp", "reticulate", "collapse",
                "data.table", "reshape2", "tidyr", "dataprep")
  missing_pkgs <- required[!vapply(required, requireNamespace,
                                   logical(1), quietly = TRUE)]
  if (length(missing_pkgs) > 0)
    stop("Missing R packages: ", paste(missing_pkgs, collapse = ", "))

  suppressPackageStartupMessages({
    library(Rcpp); library(reticulate); library(collapse)
    library(data.table); library(reshape2); library(tidyr); library(dataprep)
  })

  ensure_python()
  ensure_py_pkgs()

  pandas         <- import("pandas")
  polars         <- import("polars")
  dask_dataframe <- import("dask.dataframe")
  duckdb         <- import("duckdb")

  py_run_string("import warnings; warnings.filterwarnings('ignore')")
  options(scipen = 999)

  # ------------------------------------------------------------------
  # Python entry points.
  #
  # NOTE on dask: dask.dataframe.compute() returns a pandas DataFrame
  # whose index is the concatenation of per-partition 0..k ranges, so
  # the index contains duplicates.  reticulate's automatic pandas ->
  # data.frame conversion then emits "index contains duplicated values:
  # row names not set" on every such call.
  #
  # For melt (long output) we drop the index; there is no meaningful
  # index to keep.
  #
  # For pivot/pivot_table (wide output) the id column is the *index*
  # of the pivot_table result, so `.reset_index(drop=True)` would drop
  # it.  We must use plain `.reset_index()`: that moves id back into a
  # column and yields a fresh, unique 0..n-1 row index, which is what
  # reticulate wants.
  # ------------------------------------------------------------------
  reticulate::py_run_string("
def bench_melt_pandas():
    return bench_pd_df.melt(id_vars=bench_id_cols,
                            var_name='variable', value_name='value')
def bench_melt_polars():
    return bench_pl_df.unpivot(index=bench_id_cols,
                               variable_name='variable',
                               value_name='value')
def bench_melt_dask():
    return bench_ddf.melt(id_vars=bench_id_cols,
                          var_name='variable',
                          value_name='value').compute().reset_index(drop=True)
def bench_melt_duckdb():
    return bench_con.sql(bench_sql).df()
def bench_dcast_pandas():
    return bench_long_pd.pivot(index=bench_id_cols, columns='variable',
                               values='value').reset_index()
def bench_dcast_polars():
    try:
        return bench_long_pl.pivot(index=bench_id_cols, on='variable',
                                   values='value').to_pandas()
    except TypeError:
        return bench_long_pl.pivot(index=bench_id_cols, columns='variable',
                                   values='value').to_pandas()
def bench_dcast_dask():
    pdf = bench_ddf.compute()
    return pdf.pivot_table(index=bench_id_cols, columns='variable',
                           values='value', aggfunc='first').reset_index()
def bench_dcast_duckdb():
    return bench_con.sql(bench_sql).df()
")

  py_nc <- function(code) reticulate::py_eval(code, convert = FALSE)
  main <- reticulate::import_main()
}


# ---- per-family disable state ----------------------------------------------
bench_state <- new.env(parent = emptyenv())
bench_state$disabled <- list(melt = character(0), dcast = character(0))

bench_is_disabled <- function(family, tool)
  tool %in% bench_state$disabled[[family]]

bench_disable <- function(family, tool) {
  if (!bench_is_disabled(family, tool))
    bench_state$disabled[[family]] <-
      c(bench_state$disabled[[family]], tool)
  invisible(NULL)
}

bench_reset_disabled <- function(family = NULL) {
  if (is.null(family))
    bench_state$disabled <- list(melt = character(0), dcast = character(0))
  else
    bench_state$disabled[[family]] <- character(0)
  invisible(NULL)
}


# ---- sentinel ---------------------------------------------------------------
sentinel_mult <- function() {
  v <- suppressWarnings(as.numeric(Sys.getenv("DATAPREP_SENTINEL_MULT")))
  if (!is.finite(v) || v < 1) v <- 4
  v
}

sentinel_alloc <- function(bytes) {
  if (!is.finite(bytes) || bytes <= 0) return(NULL)
  bytes <- min(bytes, 4.0e9)
  if (bytes < 4e6) return(NULL)
  n_elems <- as.integer(bytes / 8)
  if (n_elems < 1L) return(NULL)
  x <- numeric(n_elems)
  x[1L]      <- 0
  x[n_elems] <- 0
  x
}


# ---- adaptive iteration count ----------------------------------------------
choose_times <- function(t_sec) {
  if (!is.finite(t_sec) || t_sec <= 0) return(20L)
  if (t_sec >  10)   return(1L)
  if (t_sec >   1)   return(5L)
  if (t_sec >   0.1) return(10L)
  if (t_sec >  0.01) return(15L)
  20L
}

# ---- skipped-row constructor ------------------------------------------------
bench_skipped_row <- function(tool, first_sec = NA_real_, times = 0L)
  data.frame(tool = tool, times = times, first_run_sec = first_sec,
             skipped = TRUE,
             min = NA_real_, lq = NA_real_, mean = NA_real_,
             median = NA_real_, uq = NA_real_, max = NA_real_,
             neval = NA_integer_, gc_sec = NA_real_,
             stringsAsFactors = FALSE)


# ---- single-tool timed run --------------------------------------------------
bench_one <- function(fn, tool, family = "melt", unit = "ms",
                      verbose = TRUE, pre_gc = TRUE) {
  if (bench_is_disabled(family, tool)) {
    if (verbose) cat(sprintf("    %-10s disabled (from previous cell)\n",
                             tool))
    return(bench_skipped_row(tool))
  }

  if (pre_gc) { gc(verbose = FALSE, full = TRUE); py_gc_collect() }

  t_ns <- now_ns()
  ok <- TRUE; msg <- ""
  tryCatch(invisible(fn()),
           error = function(e) { ok <<- FALSE; msg <<- conditionMessage(e) })
  warmup_sec <- (now_ns() - t_ns) * 1e-9

  if (!ok) {
    bench_disable(family, tool)
    if (verbose) cat(sprintf("    %-10s ERROR after %12.8fs -- %s\n",
                             tool, warmup_sec, msg))
    return(bench_skipped_row(tool, first_sec = warmup_sec, times = 0L))
  }

  batch <- 1L
  if (warmup_sec < 1e-6) {
    batch <- max(2L, as.integer(ceiling(1e-6 / max(warmup_sec, 1e-9))))
    batch <- min(batch, 1000000L)
    fn_inner <- fn
    fn <- local({
      b <- batch; f <- fn_inner
      function() for (i in seq_len(b)) f()
    })
    if (pre_gc) { gc(verbose = FALSE, full = TRUE); py_gc_collect() }
    t_ns <- now_ns()
    tryCatch(invisible(fn()), error = function(e) NULL)
    warmup_sec <- ((now_ns() - t_ns) * 1e-9) / batch
  }

  times <- max(1L, as.integer(choose_times(warmup_sec)))

  if (verbose)
    cat(sprintf("    %-10s warmup=%12.8fs  times=%3d (warmup excluded)\n",
                tool, warmup_sec, times))

  conv <- switch(unit, "s" = 1, "ms" = 1e3, "us" = 1e6, "ns" = 1e9, 1e3)

  rest <- numeric(times); gc_acc <- 0
  for (i in seq_len(times)) {
    if (pre_gc) {
      g_ns <- now_ns()
      gc(verbose = FALSE, full = TRUE)
      py_gc_collect()
      gc_acc <- gc_acc + (now_ns() - g_ns) * 1e-9
    }
    t_ns <- now_ns()
    tryCatch(invisible(fn()), error = function(e) NULL)
    rest[i] <- ((now_ns() - t_ns) * 1e-9) / batch
  }

  all_u <- rest * conv
  qs <- stats::quantile(all_u, probs = c(0.25, 0.50, 0.75),
                        names = FALSE, type = 7)

  sm <- data.frame(min = min(all_u), lq = qs[1L], mean = mean(all_u),
                   median = qs[2L], uq = qs[3L], max = max(all_u),
                   neval = length(all_u), stringsAsFactors = FALSE)
  sm$tool <- tool; sm$times <- times
  sm$first_run_sec <- warmup_sec
  sm$skipped <- FALSE; sm$gc_sec <- gc_acc
  sm
}


# ---- write one cell's results to a CSV --------------------------------------
write_result <- function(sm, path, overwrite = FALSE) {
  if (overwrite || !file.exists(path)) {
    write.csv(sm, path, row.names = FALSE)
    return(invisible(NULL))
  }

  header <- names(read.csv(path, nrows = 0L, check.names = FALSE,
                           stringsAsFactors = FALSE))
  if (!identical(names(sm), header)) {
    missing <- setdiff(header, names(sm))
    if (length(missing) > 0L)
      stop("Cannot append to ", path, ": missing column(s) ",
           paste(missing, collapse = ", "), call. = FALSE)
    sm <- sm[, header, drop = FALSE]
  }

  write.table(sm, path, sep = ",", row.names = FALSE, col.names = FALSE,
              append = TRUE, qmethod = "double", na = "NA", eol = "\n")
  invisible(NULL)
}


# ============================================================================
# Mixed-type input construction
# ============================================================================

mixed_types_enabled <- function() {
  v <- Sys.getenv("DATAPREP_MIXED_TYPES", unset = "TRUE")
  !identical(toupper(v), "FALSE")
}

id_desc <- function(n_id) {
  if (n_id <= 1L) return("1 id")
  if (mixed_types_enabled()) {
    n_int <- (n_id + 1L) %/% 2L
    n_chr <- n_id        %/% 2L
    return(sprintf("%d id (%d int + %d chr)", n_id, n_int, n_chr))
  }
  sprintf("%d id", n_id)
}

# Build a wide data.frame with mixed id types.
make_wide_input <- function(n_rows, n_id, n_val,
                            int_card   = 100L,
                            chr_levels = c("alpha", "beta", "gamma",
                                           "delta", "epsilon")) {
  id_cols    <- paste0("id", seq_len(n_id))
  value_cols <- paste0("v",  seq_len(n_val))
  use_mixed  <- mixed_types_enabled() && (n_id >= 2L)

  cols <- vector("list", n_id + n_val)
  names(cols) <- c(id_cols, value_cols)

  if (use_mixed) {
    for (j in seq_len(n_id)) {
      if (j %% 2L == 0L) {
        cols[[j]] <- sample(chr_levels, n_rows, replace = TRUE)
      } else {
        cols[[j]] <- sample.int(int_card, n_rows, replace = TRUE)
      }
    }
  } else {
    for (j in seq_len(n_id)) {
      cols[[j]] <- sample.int(int_card, n_rows, replace = TRUE)
    }
  }
  for (k in seq_len(n_val)) {
    cols[[n_id + k]] <- rnorm(n_rows)
  }
  as.data.frame(cols, stringsAsFactors = FALSE)
}

# Build a canonical long table for dcast. Every (id, variable) pair
# appears exactly once.
make_long <- function(n_long, n_id, n_levels,
                      chr_levels = c("alpha", "beta", "gamma",
                                     "delta", "epsilon")) {
  n_comb <- max(1L, as.integer(n_long %/% n_levels))
  n_long <- n_comb * n_levels

  use_mixed <- mixed_types_enabled() && (n_id >= 2L)
  id_cols   <- paste0("id", seq_len(n_id))

  ids <- vector("list", n_id)
  names(ids) <- id_cols

  if (use_mixed) {
    for (j in seq_len(n_id)) {
      if (j %% 2L == 0L) {
        ids[[j]] <- chr_levels[((seq_len(n_comb) - 1L) %%
                                  length(chr_levels)) + 1L]
      } else {
        ids[[j]] <- seq_len(n_comb)
      }
    }
  } else {
    for (j in seq_len(n_id)) {
      ids[[j]] <- seq_len(n_comb)
    }
  }

  idx  <- rep(seq_len(n_comb), each = n_levels)
  long <- as.data.frame(lapply(ids, `[`, idx), stringsAsFactors = FALSE)
  colnames(long) <- id_cols

  long$variable <- rep(paste0("v", seq_len(n_levels)), times = n_comb)
  long$value    <- rnorm(n_long)
  long
}

# ============================================================================
# Aggregation input / registry
# ============================================================================

# Build a long table suitable for testing aggregation semantics.
#
#   nlong    : desired input row count (approximate; rounded to the
#              nearest multiple of n_levels * K)
#   n_levels : number of distinct `variable` values
#   K        : number of times each (id, variable) pair appears
#
# Every (id, variable) pair appears exactly K times, so any tool that
# silently takes "first" instead of aggregating will differ from the
# base-R reference computed by agg_reference().
make_agg_input <- function(nlong, n_levels = 10L, K = 3L) {
  n_levels  <- as.integer(n_levels)
  K         <- as.integer(K)
  n_groups  <- max(1L, as.integer(nlong %/% (n_levels * K)))
  nlong_out <- n_groups * n_levels * K
  id        <- rep(seq_len(n_groups), each = n_levels * K)
  var_idx   <- rep(rep(seq_len(n_levels), each = K), times = n_groups)
  variable  <- paste0("v", var_idx)
  value     <- rnorm(nlong_out)
  data.frame(id = id, variable = variable, value = value,
             stringsAsFactors = FALSE)
}

# Aggregation name -> R function.
#
# IMPORTANT: keep the entries of R_AGGS as the *unwrapped* base functions.
# dataprep::dcast() selects its C++ aggregation kernel by testing
# `identical(fun.aggregate, base::min)` (and friends); wrapping them
# would silently push dataprep onto its slow stats::aggregate fallback
# and make the benchmark unfair.
R_AGGS <- list(
  sum   = base::sum,
  mean  = base::mean,
  min   = base::min,
  max   = base::max,
  count = base::length
)

# ---------------------------------------------------------------------------
# reshape2-safe aggregation wrappers.
#
# reshape2:::vaggregate() probes `fun.aggregate` with an empty vector
# (`.value[0]`) to infer the return type.  base::min(numeric(0)) and
# base::max(numeric(0)) emit "all arguments are missing" warnings and
# return Inf / -Inf to the probe.  The wrappers below avoid that by
# returning NA_real_ for empty input and computing min / max via sort()
# (which is warning-free on empty vectors).
#
# For non-empty input the wrappers are bit-identical to base::min /
# base::max with the same na.rm semantics, so reshape2's aggregation
# results are unchanged.
#
# Only reshape2 needs these.  dataprep selects its C++ kernel via
# identical(fun.aggregate, base::min) and must be given the raw base
# functions; collapse and data.table never probe with an empty vector.
# ---------------------------------------------------------------------------
safe_min <- function(x, na.rm = FALSE) {
  if (length(x) == 0L) return(NA_real_)
  if (na.rm) x <- x[!is.na(x)]
  if (anyNA(x)) return(NA_real_)
  v <- sort(x)
  v[1L]
}

safe_max <- function(x, na.rm = FALSE) {
  if (length(x) == 0L) return(NA_real_)
  if (na.rm) x <- x[!is.na(x)]
  if (anyNA(x)) return(NA_real_)
  v <- sort(x)
  v[length(v)]
}

R_AGGS_RESHAPE2 <- list(
  sum   = function(x, na.rm = FALSE) {
    if (length(x) == 0L) 0 else base::sum(x, na.rm = na.rm)
  },
  mean  = function(x, na.rm = FALSE) {
    if (length(x) == 0L) NA_real_ else base::mean(x, na.rm = na.rm)
  },
  min   = safe_min,
  max   = safe_max,
  count = function(x) base::length(x)
)

# Aggregation name -> polars / pandas aggregate name.
PY_AGGS <- list(
  sum   = "sum",
  mean  = "mean",
  min   = "min",
  max   = "max",
  count = "count"
)

# Aggregation name -> SQL aggregate keyword.
SQL_AGGS <- list(
  sum   = "SUM",
  mean  = "AVG",
  min   = "MIN",
  max   = "MAX",
  count = "COUNT"
)

# Compute the base-R wide reference for a small long data.frame.
agg_reference <- function(long, agg_fn) {
  ids  <- sort(unique(long$id))
  vars <- sort(unique(long$variable))
  M <- matrix(NA_real_, length(ids), length(vars),
              dimnames = list(ids, vars))
  for (i in seq_along(ids)) for (j in seq_along(vars)) {
    vals <- long$value[long$id == ids[i] & long$variable == vars[j]]
    M[i, j] <- agg_fn(vals)
  }
  M
}

# Normalize any tool's output to a numeric, id-sorted, var-sorted matrix.
agg_to_matrix <- function(df) {
  df <- as.data.frame(df, stringsAsFactors = FALSE)
  id_name <- names(df)[1]
  df <- df[order(df[[id_name]]), , drop = FALSE]
  var_cols <- sort(setdiff(names(df), id_name))
  m <- as.matrix(df[, var_cols, drop = FALSE])
  storage.mode(m) <- "double"
  m
}
