# ============================================================
# Adaptive per-tool benchmark for dataprep::dcast
#
# Engines (8):
#   R     : dataprep, data.table, reshape2, tidyr
#   Python: pandas, polars, dask, duckdb
#
# Design:
#   - The input is constructed DIRECTLY as a canonical long table
#     (id1, ..., idN, variable, value), not by melting a wide one.
#     This matches the typical real-world dcast use case.
#   - `n_long` (long-table row count) is the primary dimension.
#   - Sequences:
#       S1: vary n_long, 1 id + 10 levels
#       S2: 1e6 rows, 1 id + varying levels
#       S3: 1e6 rows, varying n_id + 10 levels
#       S4: vary n_long, 1 id + 100 levels
#   - A tool that exceeds `max_first_sec` on its first attempt is
#     disabled for the remainder of the current sequence.
#
# Notes:
#   - dask's distributed pivot_table does not support a vector
#     `index`; we compute() then use pandas pivot_table with
#     aggfunc='first', matching duckdb PIVOT ... USING FIRST.
#   - All Python engines are called as DataFrame instance methods.
#
# CRAN safety: the driver block is wrapped in
# `if (nzchar(Sys.getenv("DATAPREP_RUN_BENCHMARK")))` so it is
# never executed by R CMD check.
# ============================================================

source(system.file("benchmark_helpers.R", package = "dataprep"))

# ------------------------------------------------------------
# Build a canonical long table with n_long rows.
#
#   n_long    : total number of long-table rows
#   n_id      : number of id columns
#   n_levels  : number of distinct `variable` values
#
# The table is generated so that every (id_combination, variable)
# pair appears exactly once. Hence:
#   n_comb = n_long %/% n_levels   unique id combinations
#   n_long is rounded down to n_comb * n_levels
# ------------------------------------------------------------
make_long <- function(n_long, n_id, n_levels) {
  n_comb <- max(1L, as.integer(n_long %/% n_levels))
  n_long <- n_comb * n_levels

  # Unique id combinations: each column is a random permutation of 1..n_comb
  ids <- lapply(seq_len(n_id), function(k)
    sample.int(n_comb, n_comb, replace = FALSE))
  names(ids) <- paste0("id", seq_len(n_id))

  # Expand: each id appears once per level
  idx <- rep(seq_len(n_comb), each = n_levels)
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
  long <- make_long(n_long, n_id, n_levels)
  n_long <- nrow(long)

  # ---- pre-materialize inputs (NOT timed) ----
  long_dt <- as.data.table(long)
  long_pd <- r_to_py(long)
  long_pl <- polars$DataFrame(long)
  ddf     <- dask_dataframe$from_pandas(long, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("long_bench", fail_if_missing = TRUE), silent = TRUE)
  con$register("long_bench", long)
  sql_pivot <- sprintf(
    "PIVOT long_bench ON variable USING FIRST(value) GROUP BY %s;",
    paste(id_cols, collapse = ", ")
  )

  # polars pivot: try new API first, fall back to old
  pl_pivot <- function() {
    tryCatch(
      long_pl$pivot(index = id_cols, on = "variable", values = "value"),
      error = function(e)
        long_pl$pivot(index = id_cols, columns = "variable", values = "value")
    )
  }

  fml <- as.formula(paste(paste(id_cols, collapse = "+"), "~ variable"))

  # ---- per-tool closures ----
  tools <- list(
    reshape2   = function()
      reshape2::dcast(long, fml, value.var = "value"),
    data.table = function()
      data.table::dcast(long_dt, fml, value.var = "value"),
    tidyr      = function()
      tidyr::pivot_wider(long,
                         id_cols     = dplyr::all_of(id_cols),
                         names_from  = variable,
                         values_from = value),
    dataprep   = function()
      dataprep::dcast(long, id = id_cols,
                      variable = "variable", value = "value"),
    pandas     = function()
      long_pd$pivot(index   = id_cols,
                    columns = "variable",
                    values  = "value")$reset_index(),
    polars     = function() pl_pivot()$to_pandas(),
    dask       = function() {
      # Distributed pivot_table does not support vector `index`.
      # Compute to pandas first, then pivot with aggfunc='first'
      # to match duckdb PIVOT ... USING FIRST(value).
      pdf <- ddf$compute()
      pdf$pivot_table(index   = id_cols,
                      columns = "variable",
                      values  = "value",
                      aggfunc = "first")$reset_index()
    },
    duckdb     = function() con$sql(sql_pivot)$df()
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
  rm(long, long_dt, long_pd, long_pl, ddf, con, sm_list, sm)
  gc()
  invisible(NULL)
}

# ============================================================
# Driver
#
# Each gradient sequence starts with bench_reset_disabled("dcast")
# so that small data always gets a fresh chance for every tool.
# ============================================================
if (nzchar(Sys.getenv("DATAPREP_RUN_BENCHMARK"))) {
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
  for (nl in 10^(4:7)) {
    run_dcast_bench(nl, n_id = 1L, n_levels = 100L,
                    label = sprintf("Dcast: n_long=%s, 1 id + 100 lvl",
                                    format(nl, big.mark = ",")))
  }
}