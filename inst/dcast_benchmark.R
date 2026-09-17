# ============================================================
# Adaptive per-tool benchmark for dataprep::dcast
#
# Engines (8):
#   R     : dataprep, data.table, reshape2, tidyr
#   Python: pandas, polars, dask, duckdb
#
# Timing semantics: identical to melt_benchmark.R. Python
# functions read their inputs from __main__ globals registered
# by the R side; no argument or return-value marshalling is
# timed.
#
# CRAN safety: the driver block is gated on
# DATAPREP_RUN_BENCHMARK so it is never executed by R CMD check.
# ============================================================

source(system.file("benchmark_helpers.R", package = "dataprep"))

# ------------------------------------------------------------
# Build a canonical long table with n_long rows.
#
#   n_long    : total number of long-table rows
#   n_id      : number of id columns
#   n_levels  : number of distinct `variable` values
#
# Every (id_combination, variable) pair appears exactly once:
#   n_comb = n_long %/% n_levels
#   n_long is rounded down to n_comb * n_levels
# ------------------------------------------------------------
make_long <- function(n_long, n_id, n_levels) {
  n_comb <- max(1L, as.integer(n_long %/% n_levels))
  n_long <- n_comb * n_levels

  ids <- lapply(seq_len(n_id), function(k)
    sample.int(n_comb, n_comb, replace = FALSE))
  names(ids) <- paste0("id", seq_len(n_id))

  idx  <- rep(seq_len(n_comb), each = n_levels)
  long <- as.data.frame(lapply(ids, `[`, idx))
  colnames(long) <- paste0("id", seq_len(n_id))

  long$variable <- rep(paste0("v", seq_len(n_levels)), times = n_comb)
  long$value    <- rnorm(n_long)
  long
}

# ------------------------------------------------------------
# Benchmark one cell
# ------------------------------------------------------------
run_dcast_bench <- function(n_long, n_id, n_levels,
                            label         = "",
                            csv_path      = "dcast_benchmark_result.csv",
                            max_first_sec = 15,
                            budget_sec    = 15) {

  id_cols <- paste0("id", seq_len(n_id))

  cat(sprintf("\n%s\n[%s] n_long=%s | id=%d | levels=%d\n%s\n",
              strrep("=", 100), label,
              format(n_long, big.mark = ","),
              n_id, n_levels, strrep("=", 100)))

  # ---- build a canonical long table ----
  long   <- make_long(n_long, n_id, n_levels)
  n_long <- nrow(long)

  # ---- pre-materialise inputs (NOT timed) ----
  long_dt <- as.data.table(long)

  long_pd <- reticulate::r_to_py(long)
  long_pl <- polars$DataFrame(long)
  ddf     <- dask_dataframe$from_pandas(long, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("long_bench", fail_if_missing = TRUE), silent = TRUE)
  con$register("long_bench", long)
  sql_pivot <- sprintf(
    "PIVOT long_bench ON variable USING FIRST(value) GROUP BY %s;",
    paste(id_cols, collapse = ", ")
  )

  # ---- pre-convert strings/vectors once ----
  id_cols_py   <- reticulate::r_to_py(id_cols)
  sql_pivot_py <- reticulate::r_to_py(sql_pivot)

  # ---- register Python globals used by bench_dcast_* ----
  main$bench_long_pd <- long_pd
  main$bench_long_pl <- long_pl
  main$bench_ddf     <- ddf
  main$bench_con     <- con
  main$bench_id_cols <- id_cols_py
  main$bench_sql     <- sql_pivot_py

  # ---- per-tool closures ----
  tools <- list(
    reshape2   = function() {
      fml <- as.formula(paste(paste(id_cols, collapse = "+"), "~ variable"))
      reshape2::dcast(long, fml, value.var = "value")
    },

    data.table = function() {
      fml <- as.formula(paste(paste(id_cols, collapse = "+"), "~ variable"))
      data.table::dcast(long_dt, fml, value.var = "value")
    },

    tidyr      = function()
      tidyr::pivot_wider(long,
                         id_cols     = dplyr::all_of(id_cols),
                         names_from  = variable,
                         values_from = value),

    dataprep   = function()
      dataprep::dcast(long, id = id_cols,
                      variable = "variable", value = "value"),

    pandas     = function()
      py_nc("bench_dcast_pandas()"),

    polars     = function()
      py_nc("bench_dcast_polars()"),

    dask       = function()
      py_nc("bench_dcast_dask()"),

    duckdb     = function()
      py_nc("bench_dcast_duckdb()")
  )

  cat("\n  Per-tool first-run timing and chosen `times`:\n")
  sm_list <- lapply(names(tools), function(nm)
    bench_one(tools[[nm]], nm,
              family        = "dcast",
              max_first_sec = max_first_sec,
              budget_sec    = budget_sec))

  try(con$close(), silent = TRUE)

  sm <- do.call(rbind, sm_list)
  sm$n_long   <- n_long
  sm$n_id     <- n_id
  sm$n_levels <- n_levels

  base_mean   <- min(sm$mean,   na.rm = TRUE)
  base_median <- min(sm$median, na.rm = TRUE)
  sm$relative_mean   <- ifelse(sm$skipped, NA_real_, sm$mean   / base_mean)
  sm$relative_median <- ifelse(sm$skipped, NA_real_, sm$median / base_median)

  sm <- sm[order(sm$skipped, sm$relative_median), ]
  sm <- sm[, c("tool", "n_long", "n_id", "n_levels",
               "times", "first_run_sec", "skipped",
               "min", "lq", "mean", "median", "uq", "max", "neval",
               "relative_mean", "relative_median")]

  cat("\n  Results (sorted by skipped, then relative_median, ms):\n")
  print(sm, digits = 4, row.names = FALSE)

  append_result(sm, csv_path)

  # ---- drop Python-side references ----
  main$bench_long_pd <- NULL
  main$bench_long_pl <- NULL
  main$bench_ddf     <- NULL
  main$bench_con     <- NULL
  main$bench_id_cols <- NULL
  main$bench_sql     <- NULL

  rm(long, long_dt, long_pd, long_pl, ddf, con, sm_list, sm)
  gc()
  invisible(NULL)
}

# ============================================================
# Driver
# ============================================================
if (.run_bench) {
  set.seed(123)

  # ---- Sequence 1: vary n_long, 1 id + 10 levels ----
  bench_reset_disabled("dcast")
  for (nl in 10^(3:8)) {
    run_dcast_bench(nl, n_id = 1L, n_levels = 10L,
                    label = sprintf("Dcast: n_long=%s, 1 id + 10 lvl",
                                    format(nl, big.mark = ",")))
  }

  # ---- Sequence 2: 1e6 rows, 1 id + varying levels ----
  bench_reset_disabled("dcast")
  for (lv in c(10L, 100L, 1000L)) {
    run_dcast_bench(1e6, n_id = 1L, n_levels = lv,
                    label = sprintf("Dcast: n_long=1e6, 1 id + %d lvl", lv))
  }

  # ---- Sequence 3: 1e6 rows, varying n_id + 10 levels ----
  bench_reset_disabled("dcast")
  for (ni in c(1L, 10L, 100L)) {
    run_dcast_bench(1e6, n_id = ni, n_levels = 10L,
                    label = sprintf("Dcast: n_long=1e6, %d id + 10 lvl", ni))
  }

  # ---- Sequence 4: vary n_long, 1 id + 100 levels ----
  bench_reset_disabled("dcast")
  for (nl in 10^(4:8)) {
    run_dcast_bench(nl, n_id = 1L, n_levels = 100L,
                    label = sprintf("Dcast: n_long=%s, 1 id + 100 lvl",
                                    format(nl, big.mark = ",")))
  }
}