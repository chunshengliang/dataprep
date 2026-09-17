# ============================================================
# 8-engine consistency check for dcast / pivot_wider / pivot
#
# CRAN safety: driver block is gated on DATAPREP_RUN_BENCHMARK.
# ============================================================

source(system.file("benchmark_helpers.R", package = "dataprep"))

if (.run_bench) {
  reticulate::py_run_string("
def cons_dcast_pandas(long_pd, id_cols):
    return long_pd.pivot(index=list(id_cols),
                         columns='variable',
                         values='value').reset_index()

def cons_dcast_polars(long_pl, id_cols):
    try:
        return long_pl.pivot(index=list(id_cols),
                             on='variable',
                             values='value').to_pandas()
    except TypeError:
        return long_pl.pivot(index=list(id_cols),
                             columns='variable',
                             values='value').to_pandas()

def cons_dcast_dask(ddf, id_cols):
    # Distributed pivot_table does not accept a vector index,
    # so compute to pandas first, then pivot with aggfunc='first'
    # to match duckdb PIVOT ... USING FIRST(value).
    pdf = ddf.compute()
    return pdf.pivot_table(index=list(id_cols),
                           columns='variable',
                           values='value',
                           aggfunc='first').reset_index()

def cons_dcast_duckdb(con, id_cols):
    ids = ', '.join(id_cols)
    sql = ('PIVOT long_cons ON variable USING FIRST(value) '
           'GROUP BY ' + ids + ';')
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

canonicalize_wide <- function(x, id_cols) {
  x <- as.data.frame(x, stringsAsFactors = FALSE)

  missing <- setdiff(id_cols, names(x))
  if (length(missing) > 0)
    stop(sprintf("Missing id columns: %s", paste(missing, collapse = ", ")))

  # Force id columns to numeric so sorting matches the reference.
  for (nm in id_cols) x[[nm]] <- as.numeric(x[[nm]])

  ord <- do.call(order, x[id_cols])
  x   <- x[ord, , drop = FALSE]
  rownames(x) <- NULL

  other <- setdiff(names(x), id_cols)
  x     <- x[, c(id_cols, sort(other)), drop = FALSE]
  x
}

compare_wide <- function(x, y, id_cols, tol = 1e-12) {
  if (!identical(dim(x), dim(y)))     return(FALSE)
  if (!identical(names(x), names(y))) return(FALSE)

  for (nm in id_cols) {
    a <- as.numeric(x[[nm]])
    b <- as.numeric(y[[nm]])
    if (any(xor(is.na(a), is.na(b))))        return(FALSE)
    if (any(abs(a - b) > tol, na.rm = TRUE)) return(FALSE)
  }

  val_cols <- setdiff(names(x), id_cols)
  for (nm in val_cols) {
    vx <- as.numeric(x[[nm]])
    vy <- as.numeric(y[[nm]])

    if (any(xor(is.na(vx), is.na(vy)))) return(FALSE)

    ok <- is.finite(vx) & is.finite(vy)
    if (any(ok)) {
      d <- max(abs(vx[ok] - vy[ok]))
      if (!is.finite(d) || d > tol) return(FALSE)
    }
  }
  TRUE
}

dcast_all_engines <- function(n_rows, n_id, n_val, tol = 1e-12) {

  id_cols    <- paste0("id", seq_len(n_id))
  value_cols <- paste0("v",  seq_len(n_val))
  n_cols     <- n_id + n_val

  # ---- build canonical long input ----
  wide <- as.data.frame(matrix(rnorm(n_rows * n_cols), nrow = n_rows))
  colnames(wide) <- c(id_cols, value_cols)

  long <- reshape2::melt(wide,
                         id.vars          = id_cols,
                         variable.name    = "variable",
                         value.name       = "value",
                         factorsAsStrings = FALSE)
  long$variable <- as.character(long$variable)

  # ---- materialize Python inputs ----
  long_dt <- as.data.table(long)
  long_pd <- r_to_py(long)
  long_pl <- polars$DataFrame(long)
  ddf     <- dask_dataframe$from_pandas(long, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("long_cons", fail_if_missing = TRUE), silent = TRUE)
  con$register("long_cons", long)

  fml <- as.formula(paste(paste(id_cols, collapse = "+"), "~ variable"))

  res <- list()

  # ---- R engines ----
  res$reshape2 <- canonicalize_wide(
    reshape2::dcast(long, fml, value.var = "value"),
    id_cols)

  res$dataprep <- canonicalize_wide(
    dataprep::dcast(long, id = id_cols,
                    variable = "variable", value = "value"),
    id_cols)

  res$data.table <- canonicalize_wide(
    as.data.frame(data.table::dcast(long_dt, fml, value.var = "value")),
    id_cols)

  res$tidyr <- canonicalize_wide(
    as.data.frame(tidyr::pivot_wider(long,
                                     id_cols     = dplyr::all_of(id_cols),
                                     names_from  = variable,
                                     values_from = value)),
    id_cols)

  # ---- Python engines ----
  res$pandas <- canonicalize_wide(
    py_pull(reticulate::py$cons_dcast_pandas(long_pd, id_cols)),
    id_cols)

  res$polars <- canonicalize_wide(
    py_pull(reticulate::py$cons_dcast_polars(long_pl, id_cols)),
    id_cols)

  res$dask <- canonicalize_wide(
    py_pull(reticulate::py$cons_dcast_dask(ddf, id_cols)),
    id_cols)

  res$duckdb <- canonicalize_wide(
    py_pull(reticulate::py$cons_dcast_duckdb(con, id_cols)),
    id_cols)

  con$close()

  ref <- res$reshape2

  cat(sprintf("\n[cell] n_long=%s id=%d levels=%d\n",
              format(n_rows * n_val, big.mark = ","), n_id, n_val))

  cat("  vs reshape2 (reference):\n")
  for (nm in names(res)) {
    ok <- compare_wide(res[[nm]], ref, id_cols = id_cols, tol = tol)
    cat(sprintf("    %-12s : %s\n", nm, ifelse(ok, "PASS", "FAIL")))
  }

  cat("  pairwise:\n")
  nms      <- names(res)
  all_pass <- TRUE
  for (i in seq_along(nms)) {
    for (j in seq_along(nms)) {
      if (i >= j) next
      ok <- compare_wide(res[[nms[i]]], res[[nms[j]]],
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

  dcast_all_engines(1000L,   n_id = 2L,  n_val = 5L)
  dcast_all_engines(1000L,   n_id = 1L,  n_val = 50L)
  dcast_all_engines(5000L,   n_id = 10L, n_val = 10L)
  dcast_all_engines(100000L, n_id = 1L,  n_val = 10L)
}