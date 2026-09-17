# ============================================================
# 8-engine consistency check for melt / unpivot
#
# CRAN safety: driver block is gated on DATAPREP_RUN_BENCHMARK.
# ============================================================

source(system.file("benchmark_helpers.R", package = "dataprep"))

# ---- Python helpers for consistency (explicit args) ----
if (.run_bench) {
  reticulate::py_run_string("
def cons_melt_pandas(pd_df, id_cols):
    return pd_df.melt(id_vars=list(id_cols),
                      var_name='variable',
                      value_name='value')

def cons_melt_polars(pl_df, id_cols):
    return pl_df.unpivot(index=list(id_cols),
                         variable_name='variable',
                         value_name='value').to_pandas()

def cons_melt_dask(ddf, id_cols):
    return ddf.melt(id_vars=list(id_cols),
                    var_name='variable',
                    value_name='value').compute()

def cons_melt_duckdb(con, id_cols, value_cols):
    ids    = ', '.join(id_cols)
    quoted = ', '.join([\"'\" + v + \"'\" for v in value_cols])
    sql = ('SELECT ' + ids + ', variable, value FROM df_cons '
           'UNPIVOT (value FOR variable IN (' + quoted + '));')
    return con.sql(sql).df()
")
}

py_pull <- function(tmp) {
  py$tmp_df <- tmp
  py_run_string("out = tmp_df.to_dict(orient='list')")
  df <- as.data.frame(py$out, stringsAsFactors = FALSE)
  py$tmp_df <- NULL
  py$out    <- NULL
  df
}

canonicalize_long <- function(x, id_cols,
                              var_col   = "variable",
                              value_col = "value") {
  x <- as.data.frame(x, stringsAsFactors = FALSE)
  need <- c(id_cols, var_col, value_col)
  missing <- setdiff(need, names(x))
  if (length(missing) > 0)
    stop(sprintf("Missing columns in result: %s",
                 paste(missing, collapse = ", ")))

  x <- x[, need, drop = FALSE]
  x[[var_col]] <- as.character(x[[var_col]])

  ord <- do.call(order, x[c(id_cols, var_col)])
  x   <- x[ord, , drop = FALSE]
  rownames(x) <- NULL
  x
}

compare_long <- function(x, y, id_cols,
                         var_col   = "variable",
                         value_col = "value",
                         tol       = 1e-12) {
  if (nrow(x) != nrow(y)) return(FALSE)
  if (!identical(sort(names(x)), sort(names(y)))) return(FALSE)

  for (nm in id_cols) {
    a <- as.numeric(x[[nm]])
    b <- as.numeric(y[[nm]])
    if (any(xor(is.na(a), is.na(b)))) return(FALSE)
    if (any(abs(a - b) > tol, na.rm = TRUE)) return(FALSE)
  }

  if (!identical(x[[var_col]], y[[var_col]])) return(FALSE)

  vx <- as.numeric(x[[value_col]])
  vy <- as.numeric(y[[value_col]])
  if (any(xor(is.na(vx), is.na(vy)))) return(FALSE)
  ok <- is.finite(vx) & is.finite(vy)
  if (any(ok)) {
    d <- max(abs(vx[ok] - vy[ok]))
    if (!is.finite(d) || d > tol) return(FALSE)
  }
  TRUE
}

melt_all_engines <- function(n_rows, n_id, n_val, tol = 1e-12) {

  id_cols    <- paste0("id", seq_len(n_id))
  value_cols <- paste0("v",  seq_len(n_val))
  n_cols     <- n_id + n_val

  df <- as.data.frame(matrix(rnorm(n_rows * n_cols), nrow = n_rows))
  colnames(df) <- c(id_cols, value_cols)

  df_dt <- as.data.table(df)
  pd_df <- r_to_py(df)
  pl_df <- polars$DataFrame(df)
  ddf   <- dask_dataframe$from_pandas(df, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("df_cons", fail_if_missing = TRUE), silent = TRUE)
  con$register("df_cons", df)

  res <- list()

  res$reshape2 <- canonicalize_long(
    reshape2::melt(df, id.vars = id_cols), id_cols = id_cols)

  res$dataprep <- canonicalize_long(
    dataprep::melt(df, id.vars = id_cols), id_cols = id_cols)

  res$data.table <- canonicalize_long(
    as.data.frame(data.table::melt(df_dt, id.vars = id_cols,
                                   variable.name = "variable",
                                   value.name    = "value")),
    id_cols = id_cols)

  res$tidyr <- canonicalize_long(
    as.data.frame(tidyr::pivot_longer(df, cols = -seq_along(id_cols),
                                      names_to  = "variable",
                                      values_to = "value",
                                      cols_vary = "slowest")),
    id_cols = id_cols)

  res$pandas <- canonicalize_long(
    py_pull(reticulate::py$cons_melt_pandas(pd_df, id_cols)),
    id_cols = id_cols)

  res$polars <- canonicalize_long(
    py_pull(reticulate::py$cons_melt_polars(pl_df, id_cols)),
    id_cols = id_cols)

  res$dask <- canonicalize_long(
    py_pull(reticulate::py$cons_melt_dask(ddf, id_cols)),
    id_cols = id_cols)

  res$duckdb <- canonicalize_long(
    py_pull(reticulate::py$cons_melt_duckdb(con, id_cols, value_cols)),
    id_cols = id_cols)

  con$close()

  ref <- res$reshape2

  cat(sprintf("\n[cell] rows=%s id=%d val=%d\n",
              format(n_rows, big.mark = ","), n_id, n_val))

  cat("  vs reshape2 (reference):\n")
  for (nm in names(res)) {
    ok <- compare_long(res[[nm]], ref, id_cols = id_cols, tol = tol)
    cat(sprintf("    %-12s : %s\n", nm, ifelse(ok, "PASS", "FAIL")))
  }

  cat("  pairwise:\n")
  nms      <- names(res)
  all_pass <- TRUE
  for (i in seq_along(nms)) {
    for (j in seq_along(nms)) {
      if (i >= j) next
      ok <- compare_long(res[[nms[i]]], res[[nms[j]]],
                         id_cols = id_cols, tol = tol)
      if (!ok) {
        cat(sprintf("    FAIL: %s vs %s\n", nms[i], nms[j]))
        all_pass <- FALSE
      }
    }
  }
  if (all_pass) cat("    all pairs consistent\n")

  invisible(list(results = res, all_pass = all_pass))
}

if (.run_bench) {
  set.seed(123)

  melt_all_engines(1000L,   n_id = 1L,  n_val = 9L)
  melt_all_engines(100000L, n_id = 1L,  n_val = 9L)
  melt_all_engines(1000L,   n_id = 1L,  n_val = 100L)
  melt_all_engines(10000L,  n_id = 10L, n_val = 10L)
}