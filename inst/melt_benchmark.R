# ============================================================
# Adaptive per-tool benchmark for dataprep::melt
#
# Engines (8):
#   R     : dataprep, data.table, reshape2, tidyr
#   Python: pandas, polars, dask, duckdb
#
# Default cap = 15 seconds. A tool that exceeds the cap on its
# first attempt is disabled for the remainder of the current
# gradient sequence. Call bench_reset_disabled("melt") before
# starting a new sequence.
#
# CRAN safety: the driver block is wrapped in
# `if (nzchar(Sys.getenv("DATAPREP_RUN_BENCHMARK")))` so it is
# never executed by R CMD check.
# ============================================================

source(system.file("benchmark_helpers.R", package = "dataprep"))

run_melt_bench <- function(n_rows, n_id, n_val,
                           label         = "",
                           csv_path      = "melt_benchmark_result.csv",
                           max_first_sec = 15,
                           budget_sec    = 15) {

  id_cols    <- paste0("id", seq_len(n_id))
  value_cols <- paste0("v",  seq_len(n_val))
  n_cols     <- n_id + n_val

  cat(sprintf("\n%s\n[%s] rows=%s | id=%d | val=%d | cols=%d\n%s\n",
              strrep("=", 100), label,
              format(n_rows, big.mark = ","), n_id, n_val, n_cols,
              strrep("=", 100)))

  # ---- build input ----
  df <- as.data.frame(matrix(rnorm(n_rows * n_cols), nrow = n_rows))
  colnames(df) <- c(id_cols, value_cols)

  # ---- pre-materialize inputs (NOT timed) ----
  df_dt <- as.data.table(df)
  pd_df <- r_to_py(df)
  pl_df <- polars$DataFrame(df)
  ddf   <- dask_dataframe$from_pandas(df, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("df_bench", fail_if_missing = TRUE), silent = TRUE)
  con$register("df_bench", df)
  sql_unpivot <- sprintf(
    "SELECT %s, variable, value FROM df_bench UNPIVOT (value FOR variable IN (%s));",
    paste(id_cols, collapse = ", "),
    paste(sprintf("'%s'", value_cols), collapse = ", ")
  )

  # ---- per-tool closures ----
  tools <- list(
    reshape2   = function() reshape2::melt(df, id.vars = id_cols),
    data.table = function() data.table::melt(df_dt, id.vars = id_cols),
    tidyr      = function() tidyr::pivot_longer(df, cols = -seq_along(id_cols)),
    dataprep   = function() dataprep::melt(df, id.vars = id_cols),
    pandas     = function() pd_df$melt(id_vars    = id_cols,
                                       var_name    = "variable",
                                       value_name  = "value"),
    polars     = function() pl_df$unpivot(index         = id_cols,
                                          variable_name = "variable",
                                          value_name    = "value"),
    dask       = function() ddf$melt(id_vars   = id_cols,
                                     var_name   = "variable",
                                     value_name = "value")$compute(),
    duckdb     = function() con$sql(sql_unpivot)$df()
  )

  cat("\n  Per-tool first-run timing and chosen `times`:\n")
  sm_list <- lapply(names(tools), function(nm)
    bench_one(tools[[nm]], nm,
              family        = "melt",
              max_first_sec = max_first_sec,
              budget_sec    = budget_sec))

  try(con$close(), silent = TRUE)

  sm <- do.call(rbind, sm_list)
  sm$n_rows <- n_rows
  sm$n_cols <- n_cols
  sm$n_id   <- n_id
  sm$n_val  <- n_val

  base_mean   <- min(sm$mean,   na.rm = TRUE)
  base_median <- min(sm$median, na.rm = TRUE)
  sm$relative_mean   <- ifelse(sm$skipped, NA_real_, sm$mean   / base_mean)
  sm$relative_median <- ifelse(sm$skipped, NA_real_, sm$median / base_median)

  sm <- sm[order(sm$skipped, sm$relative_median), ]
  sm <- sm[, c("tool", "n_rows", "n_cols", "n_id", "n_val",
               "times", "first_run_sec", "skipped",
               "min", "lq", "mean", "median", "uq", "max", "neval",
               "relative_mean", "relative_median")]

  cat("\n  Results (sorted by skipped, then relative_median, ms):\n")
  print(sm, digits = 4, row.names = FALSE)

  append_result(sm, csv_path)
  rm(df, df_dt, pd_df, pl_df, ddf, con, sm_list, sm)
  gc()
  invisible(NULL)
}

# ============================================================
# Driver
#
# Each gradient sequence starts with bench_reset_disabled("melt")
# so that small data always gets a fresh chance for every tool.
# ============================================================
if (nzchar(Sys.getenv("DATAPREP_RUN_BENCHMARK"))) {
  set.seed(123)

  # ---- Sequence 1: vary rows, 1 id + 9 value cols ----
  bench_reset_disabled("melt")
  for (nr in 10^(3:8)) {
    run_melt_bench(nr, n_id = 1, n_val = 9,
                   label = sprintf("Melt: rows=%s, 1 id + 9 val",
                                   format(nr, big.mark = ",")))
  }

  # ---- Sequence 2: 1e3 rows, 1 id + varying value cols ----
  bench_reset_disabled("melt")
  for (nc in 10^(1:5)) {
    run_melt_bench(1e3, n_id = 1, n_val = nc,
                   label = sprintf("Melt: 1e3 rows, 1 id + %d val", nc))
  }

  # ---- Sequence 3: vary rows, 10 id + 9 value cols ----
  bench_reset_disabled("melt")
  for (nr in 10^(3:8)) {
    run_melt_bench(nr, n_id = 10, n_val = 9,
                   label = sprintf("Melt: rows=%s, 10 id + 9 val",
                                   format(nr, big.mark = ",")))
  }

  # ---- Sequence 4: 1e3 rows, 10 id + varying value cols ----
  bench_reset_disabled("melt")
  for (nc in 10^(1:5)) {
    run_melt_bench(1e3, n_id = 10, n_val = nc,
                   label = sprintf("Melt: 1e3 rows, 10 id + %d val", nc))
  }
}