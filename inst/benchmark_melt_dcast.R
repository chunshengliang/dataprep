# ============================================================================
# Melt / dcast / aggregated-dcast benchmark and consistency harness.
#
# Structure:
#   Section 1   Build / install dataprep
#   Section 2   Sanity checks: dataprep vs reshape2 vs tidyr
#   Section 3   9-engine consistency check for melt / unpivot
#   Section 4   9-engine consistency check for dcast / pivot
#   Section 4b  9-engine consistency check for AGGREGATED dcast
#   Section 5   Adaptive per-tool benchmark for melt
#   Section 6   Adaptive per-tool benchmark for dcast
#   Section 6b  Adaptive per-tool benchmark for AGGREGATED dcast
#
# The 9 engines are: dataprep, reshape2, data.table, tidyr, collapse,
# pandas, polars, dask, duckdb.
# ============================================================================

library(Rcpp)
library(dplyr)

.BENCH_TS  <- format(Sys.time(), "%Y%m%d%H%M")
.MELT_CSV  <- sprintf("melt_benchmark_%s.csv",  .BENCH_TS)
.DCAST_CSV <- sprintf("dcast_benchmark_%s.csv", .BENCH_TS)

source(system.file("benchmark_helpers.R", package = "dataprep"))


# ============================================================================
# Section 1 -- build / install
# ============================================================================
library(dataprep)
suppressPackageStartupMessages(library(collapse))


# ============================================================================
# Section 2 -- sanity checks against reshape2 and tidyr
# ============================================================================
data("data", package = "dataprep")

sanity_ok <- function(a, b) {
  a <- as.data.frame(a, stringsAsFactors = FALSE)
  b <- as.data.frame(b, stringsAsFactors = FALSE)
  if (!identical(dim(a), dim(b)))      return(FALSE)
  if (!identical(names(a), names(b)))  return(FALSE)
  for (nm in names(a)) {
    x <- a[[nm]]; y <- b[[nm]]
    if (is.factor(x)) x <- as.character(x)
    if (is.factor(y)) y <- as.character(y)
    if (is.character(x) || is.character(y)) {
      if (!identical(as.character(x), as.character(y))) return(FALSE)
    } else {
      if (any(xor(is.na(x), is.na(y)))) return(FALSE)
      if (any(abs(as.numeric(x) - as.numeric(y)) > 1e-12, na.rm = TRUE))
        return(FALSE)
    }
  }
  TRUE
}

sanity <- function(label, a, b) {
  ok <- tryCatch(sanity_ok(a, b),
                 error = function(e) FALSE)
  cat(sprintf("  %-50s : %s\n", label,
              if (ok) "PASS" else "FAIL (advisory)"))
  invisible(ok)
}

cat("\n=== Section 2 sanity checks (advisory) ===\n")
sanity("melt(1:4) col-major vs reshape2",
       dataprep::melt(data, 1:4),
       reshape2::melt(data, 1:4))

sanity("melt(1:4) col-major explicit vs reshape2",
       dataprep::melt(data, 1:4, major = "col"),
       reshape2::melt(data, 1:4))

sanity("melt col-major vs tidyr slowest",
       dataprep::melt(data, 1:4, major = "col"),
       tidyr::pivot_longer(data, !1:4, cols_vary = "slowest",
                           names_to = "variable") %>%
         mutate(variable = factor(variable, unique(variable))) %>%
         as.data.frame())

sanity("melt row-major vs tidyr",
       dataprep::melt(data, 1:4, major = "row"),
       tidyr::pivot_longer(data, !1:4, names_to = "variable") %>%
         as.data.frame())

cat("(advisory checks never abort; a FAIL here is informational)\n\n")


# ============================================================================
# Section 3 -- 9-engine consistency check for melt
# ============================================================================
suppressPackageStartupMessages({
  library(reticulate); library(data.table)
  library(reshape2);   library(tidyr)
  library(collapse)
  library(dataprep)
})

cat("\n=== R side ===\n")
cat(R.version.string, "\n")
for (p in c("dataprep", "reshape2", "data.table", "tidyr", "collapse",
            "reticulate", "Rcpp")) {
  v <- tryCatch(as.character(packageVersion(p)),
                error = function(e) "not installed")
  cat(sprintf("  %-12s %s\n", p, v))
}

py_import_safe <- function(mod)
  tryCatch(reticulate::import(mod, delay_load = FALSE),
           error = function(e) NULL)

py_version <- function(mod) {
  if (is.null(mod)) return(NA_character_)
  v <- tryCatch(mod$`__version__`, error = function(e) NULL)
  if (is.null(v)) NA_character_ else as.character(v)
}

pandas         <- py_import_safe("pandas")
polars         <- py_import_safe("polars")
dask           <- py_import_safe("dask")
dask_dataframe <- py_import_safe("dask.dataframe")
duckdb         <- py_import_safe("duckdb")

# polars runtime: polars >= 1.0 reports the index width as
# "Index type:  UInt32 / UInt64"; polars < 1.0 used "rt32" / "rt64".
#
# polars.show_versions() writes to stderr from a Rust log handler, and on
# Windows the process also emits localised system messages (Azure CLI
# hints, encoding notices) in the OEM code page.  Reading that stream via
# R's system2() decodes it with the R locale encoding, and the subsequent
# regexpr() then fails with "unable to translate ... to a wide string" /
# "input string N is invalid" whenever a stray non-UTF-8 byte appears.
#
# Capture on the Python side instead: subprocess.run(..., text=True,
# encoding='utf-8', errors='replace') decodes both streams as UTF-8 and
# substitutes U+FFFD for invalid bytes, so R only ever sees a valid
# character vector.  Values match the old approach on every platform.
polars_rt <- tryCatch({
  reticulate::py_run_string("
import subprocess as _sp
import sys as _sys
_p = _sp.run([_sys.executable, '-c',
              'import polars; polars.show_versions()'],
             capture_output=True, text=True,
             encoding='utf-8', errors='replace')
_polars_versions_text = (_p.stdout or '') + (_p.stderr or '')
del _p
")
  out <- reticulate::py$`_polars_versions_text`
  reticulate::py_run_string("del _polars_versions_text")
  hit <- regmatches(out,
                    regexpr("UInt32|UInt64|rt32|rt64", out,
                            ignore.case = TRUE))
  if (length(hit) == 0L) NA_character_ else hit[1L]
}, error = function(e) NA_character_)

cat("\n=== Python side ===\n")
cat(sprintf("  python       %s\n", reticulate::py_config()$python))
cat(sprintf("  pandas       %s\n", py_version(pandas)))
cat(sprintf("  polars       %s\n", py_version(polars)))
cat(sprintf("  polars rt    %s\n", polars_rt))
cat(sprintf("  dask         %s\n", py_version(dask)))
cat(sprintf("  duckdb       %s\n\n", py_version(duckdb)))

if (is.null(pandas) || is.null(polars) || is.null(duckdb))
  stop("Core Python modules missing: pandas=", !is.null(pandas),
       ", polars=", !is.null(polars), ", duckdb=", !is.null(duckdb))

py_run_string("import warnings; warnings.filterwarnings('ignore')")

# ---- Python-side helper: dask melt with a unique row index ------------------
# dask.dataframe.compute() returns a pandas DataFrame whose index is the
# concatenation of per-partition 0..k ranges, so the index contains
# duplicates.  reticulate's automatic pandas -> data.frame conversion
# would emit "index contains duplicated values: row names not set" on
# every such call.  Resetting the index on the Python side before
# returning keeps the conversion warning-free.
py_run_string("
def _melt_dask_cons(ddf, id_cols):
    return ddf.melt(id_vars=id_cols,
                    var_name='variable',
                    value_name='value').compute().reset_index(drop=True)
")

py_pull <- function(tmp) {
  if (is.data.frame(tmp)) return(tmp)

  if (is.list(tmp) &&
      !inherits(tmp, "python.builtin.object") &&
      !inherits(tmp, "python.builtin.dict")) {
    return(as.data.frame(tmp, stringsAsFactors = FALSE))
  }

  main <- reticulate::import_main()
  main$`__py_pull_tmp__` <- tmp
  on.exit({ main$`__py_pull_tmp__` <- NULL }, add = TRUE)

  data <- tryCatch(
    reticulate::py_eval(
      "{str(c): list(__py_pull_tmp__[c]) for c in __py_pull_tmp__.columns}",
      convert = TRUE),
    error = function(e) NULL)

  if (is.null(data) ||
      inherits(data, "python.builtin.dict") ||
      inherits(data, "python.builtin.object")) {
    data <- tryCatch(reticulate::py_to_r(data),
                     error = function(e) NULL)
  }

  if (is.null(data) || inherits(data, "python.builtin.object")) {
    cols <- reticulate::py_eval(
      "list(__py_pull_tmp__.columns)", convert = TRUE)
    if (inherits(cols, "python.builtin.object")) {
      cols <- reticulate::py_to_r(cols)
    }
    data <- setNames(lapply(cols, function(cn) {
      v <- reticulate::py_eval(
        sprintf("list(__py_pull_tmp__[%s])", shQuote(cn)),
        convert = TRUE)
      if (inherits(v, "python.builtin.object")) v <- reticulate::py_to_r(v)
      v
    }), cols)
  }

  as.data.frame(data, stringsAsFactors = FALSE)
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

  for (nm in id_cols) {
    if (is.character(x[[nm]]) || is.factor(x[[nm]])) {
      x[[nm]] <- as.character(x[[nm]])
    } else {
      x[[nm]] <- as.numeric(x[[nm]])
    }
  }

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
    xc <- x[[nm]]; yc <- y[[nm]]
    if (is.character(xc) || is.factor(xc) ||
        is.character(yc) || is.factor(yc)) {
      a <- as.character(xc); b <- as.character(yc)
      if (any(xor(is.na(a), is.na(b)))) return(FALSE)
      if (!identical(a, b)) return(FALSE)
    } else {
      a <- as.numeric(xc); b <- as.numeric(yc)
      if (any(xor(is.na(a), is.na(b)))) return(FALSE)
      if (any(abs(a - b) > tol, na.rm = TRUE)) return(FALSE)
    }
  }

  if (!identical(as.character(x[[var_col]]),
                 as.character(y[[var_col]]))) return(FALSE)

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

  df <- make_wide_input(n_rows, n_id, n_val)

  df_dt <- as.data.table(df)
  pd_df <- r_to_py(df)
  pl_df <- polars$DataFrame(df)
  ddf   <- dask_dataframe$from_pandas(df, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("df_cons", fail_if_missing = TRUE), silent = TRUE)
  con$register("df_cons", df)
  sql_unpivot <- sprintf(
    "SELECT %s, variable, value FROM df_cons UNPIVOT (value FOR variable IN (%s));",
    paste(id_cols, collapse = ", "),
    paste(sprintf("'%s'", value_cols), collapse = ", ")
  )

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
                                      names_to   = "variable",
                                      values_to  = "value",
                                      cols_vary  = "slowest")),
    id_cols = id_cols)

  res$collapse <- canonicalize_long(
    as.data.frame(collapse::pivot(df, how = "longer", ids = id_cols)),
    id_cols = id_cols)

  res$pandas <- canonicalize_long(
    py_pull(pandas$melt(pd_df, id_vars = id_cols,
                        var_name = "variable", value_name = "value")),
    id_cols = id_cols)

  res$polars <- canonicalize_long(
    py_pull(pl_df$unpivot(index         = id_cols,
                          variable_name = "variable",
                          value_name    = "value")$to_pandas()),
    id_cols = id_cols)

  # ---- dask: compute + reset_index, executed fully in Python --------------
  res$dask <- canonicalize_long(
    py_pull(reticulate::py$`_melt_dask_cons`(
      ddf, reticulate::r_to_py(id_cols))),
    id_cols = id_cols)

  res$duckdb <- canonicalize_long(
    py_pull(con$sql(sql_unpivot)$df()),
    id_cols = id_cols)

  con$close()

  ref <- res$reshape2

  cat(sprintf("\n[melt consistency] rows=%s %s val=%d mixed=%s\n",
              format(n_rows, big.mark = ","), id_desc(n_id), n_val,
              as.character(mixed_types_enabled() && n_id >= 2L)))

  for (nm in names(res)) {
    ok <- compare_long(res[[nm]], ref, id_cols = id_cols, tol = tol)
    cat(sprintf("  %-12s : %s\n", nm, ifelse(ok, "PASS", "FAIL")))
  }

  nms      <- names(res)
  all_pass <- TRUE
  for (i in seq_along(nms)) {
    for (j in seq_along(nms)) {
      if (i >= j) next
      ok <- compare_long(res[[nms[i]]], res[[nms[j]]],
                         id_cols = id_cols, tol = tol)
      if (!ok) {
        cat(sprintf("  FAIL: %s vs %s\n", nms[i], nms[j]))
        all_pass <- FALSE
      }
    }
  }
  if (all_pass) cat("  pairwise: all consistent\n")

  invisible(list(results = res, all_pass = all_pass))
}

if (.run_bench) {
  set.seed(123)
  invisible(gc()); melt_all_engines(1000L,  n_id = 1L,  n_val = 9L)
  invisible(gc()); melt_all_engines(1000L,  n_id = 2L,  n_val = 5L)
  invisible(gc()); melt_all_engines(10000L, n_id = 10L, n_val = 10L)
  invisible(gc()); melt_all_engines(100000L, n_id = 1L, n_val = 9L)
}


# ============================================================================
# Section 4 -- 9-engine consistency check for dcast
# ============================================================================
suppressPackageStartupMessages({
  library(reticulate); library(data.table)
  library(reshape2);   library(tidyr)
  library(collapse)
  library(dplyr);      library(dataprep)
})

pandas         <- import("pandas")
polars         <- import("polars")
dask_dataframe <- import("dask.dataframe")
duckdb         <- import("duckdb")
py_run_string("import warnings; warnings.filterwarnings('ignore')")

# ---- Python-side dask pivot helper (used by consistency check) -------------
# Doing compute() + pivot_table() entirely on the Python side avoids
# reticulate's automatic conversion of the intermediate pandas DataFrame,
# which otherwise makes `$pivot_table` inaccessible from R.
#
# IMPORTANT: pivot_table() places id_cols into the result's *index*.
# `.reset_index()` moves them back into columns and yields a fresh,
# unique 0..n-1 row index.  `.reset_index(drop=True)` would *drop* the
# id columns -- don't use it here.
py_run_string("
def _dcast_dask_cons(ddf, id_cols):
    pdf = ddf.compute()
    return pdf.pivot_table(index=id_cols, columns='variable',
                           values='value', aggfunc='first').reset_index()
")

canonicalize_wide <- function(x, id_cols) {
  x <- as.data.frame(x, stringsAsFactors = FALSE)
  missing <- setdiff(id_cols, names(x))
  if (length(missing) > 0)
    stop(sprintf("Missing id columns: %s", paste(missing, collapse = ", ")))

  for (nm in id_cols) {
    if (is.character(x[[nm]]) || is.factor(x[[nm]])) {
      x[[nm]] <- as.character(x[[nm]])
    } else {
      x[[nm]] <- as.numeric(x[[nm]])
    }
  }

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
    xc <- x[[nm]]; yc <- y[[nm]]
    if (is.character(xc) || is.factor(xc) ||
        is.character(yc) || is.factor(yc)) {
      a <- as.character(xc); b <- as.character(yc)
      if (any(xor(is.na(a), is.na(b)))) return(FALSE)
      if (!identical(a, b)) return(FALSE)
    } else {
      a <- as.numeric(xc); b <- as.numeric(yc)
      if (any(xor(is.na(a), is.na(b))))        return(FALSE)
      if (any(abs(a - b) > tol, na.rm = TRUE)) return(FALSE)
    }
  }

  val_cols <- setdiff(names(x), id_cols)
  for (nm in val_cols) {
    vx <- as.numeric(x[[nm]]); vy <- as.numeric(y[[nm]])
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

  wide <- make_wide_input(n_rows, n_id, n_val)
  wide[[1L]] <- seq_len(n_rows)
  long <- reshape2::melt(wide,
                         id.vars          = id_cols,
                         variable.name    = "variable",
                         value.name       = "value",
                         factorsAsStrings = FALSE)
  long$variable <- as.character(long$variable)

  long_dt <- as.data.table(long)
  long_pd <- r_to_py(long)
  long_pl <- polars$DataFrame(long)
  ddf     <- dask_dataframe$from_pandas(long, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("long_cons", fail_if_missing = TRUE), silent = TRUE)
  con$register("long_cons", long)
  sql_pivot <- sprintf(
    "PIVOT long_cons ON variable USING FIRST(value) GROUP BY %s;",
    paste(id_cols, collapse = ", ")
  )

  pl_pivot <- function() {
    tryCatch(
      long_pl$pivot(index = id_cols, on = "variable", values = "value"),
      error = function(e)
        long_pl$pivot(index = id_cols, columns = "variable", values = "value")
    )
  }

  fml <- as.formula(paste(paste(id_cols, collapse = "+"), "~ variable"))

  res <- list()

  res$reshape2 <- canonicalize_wide(
    reshape2::dcast(long, fml, value.var = "value"), id_cols)

  res$dataprep <- canonicalize_wide(
    dataprep::dcast(long, id = id_cols,
                    variable = "variable", value = "value"),
    id_cols)

  res$data.table <- canonicalize_wide(
    as.data.frame(data.table::dcast(long_dt, fml, value.var = "value")),
    id_cols)

  res$tidyr <- canonicalize_wide(
    as.data.frame(tidyr::pivot_wider(long,
                                     id_cols     = all_of(id_cols),
                                     names_from  = variable,
                                     values_from = value)),
    id_cols)

  res$collapse <- canonicalize_wide(
    as.data.frame(collapse::pivot(long,
                                  how    = "wider",
                                  ids    = id_cols,
                                  values = "value",
                                  names  = "variable")),
    id_cols)

  pd_res <- long_pd$pivot(index   = id_cols,
                          columns = "variable",
                          values  = "value")$reset_index()
  res$pandas <- canonicalize_wide(py_pull(pd_res), id_cols)

  res$polars <- canonicalize_wide(
    py_pull(pl_pivot()$to_pandas()), id_cols)

  # ---- dask: compute + pivot_table(first), executed fully in Python -------
  res$dask <- canonicalize_wide(
    py_pull(reticulate::py$`_dcast_dask_cons`(
      ddf, reticulate::r_to_py(id_cols))),
    id_cols)

  res$duckdb <- canonicalize_wide(
    py_pull(con$sql(sql_pivot)$df()), id_cols)

  con$close()

  ref <- res$reshape2

  cat(sprintf("\n[dcast consistency] n_long=%s %s levels=%d mixed=%s\n",
              format(n_rows * n_val, big.mark = ","),
              id_desc(n_id), n_val,
              as.character(mixed_types_enabled() && n_id >= 2L)))

  for (nm in names(res)) {
    ok <- compare_wide(res[[nm]], ref, id_cols = id_cols, tol = tol)
    cat(sprintf("  %-12s : %s\n", nm, ifelse(ok, "PASS", "FAIL")))
  }

  nms      <- names(res)
  all_pass <- TRUE
  for (i in seq_along(nms)) {
    for (j in seq_along(nms)) {
      if (i >= j) next
      ok <- compare_wide(res[[nms[i]]], res[[nms[j]]],
                         id_cols = id_cols, tol = tol)
      if (!ok) {
        cat(sprintf("  FAIL: %s vs %s\n", nms[i], nms[j]))
        all_pass <- FALSE
      }
    }
  }
  if (all_pass) cat("  pairwise: all consistent\n")

  invisible(list(results = res, all_pass = all_pass))
}

if (.run_bench) {
  set.seed(123)
  invisible(gc()); dcast_all_engines(1000L,   n_id = 2L,  n_val = 5L)
  invisible(gc()); dcast_all_engines(1000L,   n_id = 1L,  n_val = 50L)
  invisible(gc()); dcast_all_engines(5000L,   n_id = 10L, n_val = 10L)
  invisible(gc()); dcast_all_engines(100000L, n_id = 1L,  n_val = 10L)
}


# ============================================================================
# Section 4b -- 9-engine consistency check for AGGREGATED dcast
# ============================================================================
suppressPackageStartupMessages({
  library(reticulate); library(data.table)
  library(reshape2);   library(tidyr)
  library(collapse);   library(dataprep)
})

pandas         <- import("pandas")
polars         <- import("polars")
dask_dataframe <- import("dask.dataframe")
duckdb         <- import("duckdb")
py_run_string("import warnings; warnings.filterwarnings('ignore')")

# R_AGGS and R_AGGS_TIDYR are provided by benchmark_helpers.R:
#   * R_AGGS       -- the raw base functions, used by dataprep (which
#                     selects its C++ kernel via identical()), collapse,
#                     reshape2, data.table.
#   * R_AGGS_TIDYR -- empty-input-safe wrappers, used only by tidyr,
#                     because pivot_wider() probes values_fn with an
#                     empty vector during type inference.

# --- tool wrappers -----------------------------------------------------------
# Each takes (long, agg_name) where agg_name is a string key into R_AGGS /
# PY_AGGS / SQL_AGGS.  The wrapper is responsible for translating the
# string to the tool-specific aggregation argument.
agg_wrappers <- list(

  dataprep = function(long, agg_name) {
    dataprep::dcast(long, id = "id", variable = "variable",
                    value = "value", fun.aggregate = R_AGGS[[agg_name]])
  },

  collapse = function(long, agg_name) {
    as.data.frame(collapse::pivot(long, how = "wider", ids = "id",
                                  values = "value", names = "variable",
                                  FUN = R_AGGS[[agg_name]]))
  },

  reshape2 = function(long, agg_name) {
    reshape2::dcast(long, id ~ variable, value.var = "value",
                    fun.aggregate = R_AGGS_RESHAPE2[[agg_name]])
  },

  data.table = function(long, agg_name) {
    as.data.frame(data.table::dcast(as.data.table(long), id ~ variable,
                                    value.var = "value",
                                    fun.aggregate = R_AGGS[[agg_name]]))
  },

  tidyr = function(long, agg_name) {
    as.data.frame(tidyr::pivot_wider(long, id_cols = id,
                                     names_from = variable,
                                     values_from = value,
                                     values_fn = R_AGGS[[agg_name]]))
  },

  polars = function(long, agg_name) {
    main <- reticulate::import_main()
    main$tmp_pl_agg <- polars$DataFrame(long)
    on.exit({ main$tmp_pl_agg <- NULL }, add = TRUE)
    py_run_string(sprintf("
_tmp = tmp_pl_agg.pivot(index='id', on='variable', values='value',
                        aggregate_function='%s').to_pandas()
_out = {str(c): list(_tmp[c]) for c in _tmp.columns}
", PY_AGGS[[agg_name]]))
    out <- reticulate::py$`_out`
    py_run_string("del _tmp, _out")
    as.data.frame(out, stringsAsFactors = FALSE)
  },

  pandas = function(long, agg_name) {
    main <- reticulate::import_main()
    main$tmp_pd_agg <- reticulate::r_to_py(long)
    on.exit({ main$tmp_pd_agg <- NULL }, add = TRUE)
    py_run_string(sprintf("
_tmp = tmp_pd_agg.pivot_table(index='id', columns='variable',
                              values='value',
                              aggfunc='%s').reset_index()
_tmp.columns.name = None
_out = {str(c): list(_tmp[c]) for c in _tmp.columns}
", PY_AGGS[[agg_name]]))
    out <- reticulate::py$`_out`
    py_run_string("del _tmp, _out")
    as.data.frame(out, stringsAsFactors = FALSE)
  },

  # dask: pivot_table() puts id into the index; .reset_index() moves it
  # back into a column.  Do NOT use .reset_index(drop=True) -- that would
  # drop the id column and every comparison against the base-R reference
  # would then fail.
  dask = function(long, agg_name) {
    main <- reticulate::import_main()
    main$tmp_ddf_agg <- dask_dataframe$from_pandas(long, npartitions = 4L)
    on.exit({ main$tmp_ddf_agg <- NULL }, add = TRUE)
    py_run_string(sprintf("
_tmp = tmp_ddf_agg.compute().pivot_table(index='id', columns='variable',
                                         values='value',
                                         aggfunc='%s').reset_index()
_tmp.columns.name = None
_out = {str(c): list(_tmp[c]) for c in _tmp.columns}
", PY_AGGS[[agg_name]]))
    out <- reticulate::py$`_out`
    py_run_string("del _tmp, _out")
    as.data.frame(out, stringsAsFactors = FALSE)
  },

  duckdb = function(long, agg_name) {
    con <- duckdb$connect()
    on.exit({ try(con$close(), silent = TRUE) }, add = TRUE)
    con$register("t_agg", long)
    main <- reticulate::import_main()
    main$tmp_con_agg <- con
    on.exit({ main$tmp_con_agg <- NULL }, add = TRUE)
    py_run_string(sprintf("
_tmp = tmp_con_agg.sql('PIVOT t_agg ON variable USING %s(value) GROUP BY id;').df()
_out = {str(c): list(_tmp[c]) for c in _tmp.columns}
", SQL_AGGS[[agg_name]]))
    out <- reticulate::py$`_out`
    py_run_string("del _tmp, _out")
    as.data.frame(out, stringsAsFactors = FALSE)
  }
)

# --- one consistency cell ----------------------------------------------------
dcast_agg_all_engines <- function(nlong, n_levels = 10L, K = 3L,
                                  tol = 1e-10) {
  long <- make_agg_input(nlong, n_levels, K)
  cat(sprintf("\n[dcast agg consistency] nlong=%s  n_levels=%d  K=%d  out=%d x %d\n",
              format(nrow(long), big.mark = ","), n_levels, K,
              length(unique(long$id)), n_levels))
  overall_pass <- TRUE
  for (agg_name in names(R_AGGS)) {
    ref <- agg_reference(long, R_AGGS[[agg_name]])
    cat(sprintf("  Agg=%-6s: ", agg_name))
    line <- character(0)
    for (nm in names(agg_wrappers)) {
      st <- tryCatch({
        out <- agg_wrappers[[nm]](long, agg_name)
        m   <- agg_to_matrix(out)
        if (identical(dim(m), dim(ref)) &&
            isTRUE(all.equal(m, ref, tolerance = tol,
                             check.attributes = FALSE))) "PASS"
        else "FAIL"
      }, error = function(e) "ERR")
      line <- c(line, sprintf("%s=%s", nm, st))
      if (st != "PASS") overall_pass <- FALSE
    }
    cat(paste(line, collapse = "  "), "\n")
    invisible(gc())
  }
  cat(sprintf("  --> overall: %s\n",
              if (overall_pass) "ALL PASS" else "SOME FAIL"))
  invisible(overall_pass)
}

if (.run_bench) {
  set.seed(123)
  invisible(gc()); dcast_agg_all_engines(1000L,  n_levels = 10L, K = 3L)
  invisible(gc()); dcast_agg_all_engines(10000L, n_levels = 10L, K = 3L)
}


# ============================================================================
# Post-processing helper for bench tables
# ============================================================================
add_relative_cols <- function(sm) {
  pos_mean   <- sm$mean[is.finite(sm$mean)     & sm$mean   > 0]
  pos_median <- sm$median[is.finite(sm$median) & sm$median > 0]
  base_mean   <- if (length(pos_mean)   > 0) min(pos_mean)   else NA_real_
  base_median <- if (length(pos_median) > 0) min(pos_median) else NA_real_

  safe_ratio <- function(x, base) {
    if (!is.finite(base) || base <= 0) return(rep(NA_real_, length(x)))
    out <- x / base
    out[!is.finite(out)] <- NA_real_
    out
  }
  sm$relative_mean   <- ifelse(sm$skipped, NA_real_,
                               safe_ratio(sm$mean,   base_mean))
  sm$relative_median <- ifelse(sm$skipped, NA_real_,
                               safe_ratio(sm$median, base_median))
  sm
}


# ============================================================================
# Section 5 -- adaptive per-tool benchmark for melt
# ============================================================================
source(system.file("benchmark_helpers.R", package = "dataprep"))

reticulate::py_run_string("
def bench_melt_pandas():
    return bench_pd_df.melt(id_vars=bench_id_cols,
                            var_name='variable', value_name='value')
def bench_melt_polars():
    return bench_pl_df.unpivot(index=bench_id_cols,
                               variable_name='variable', value_name='value')
def bench_melt_dask():
    return bench_ddf.melt(id_vars=bench_id_cols,
                          var_name='variable',
                          value_name='value').compute().reset_index(drop=True)
def bench_melt_duckdb():
    return bench_con.sql(bench_sql).df()
")

melt_py_call <- function(code) {
  reticulate::py_eval(code, convert = FALSE)
}

prepare_melt_inputs <- function(n_rows, n_id, n_val) {
  id_cols    <- paste0("id", seq_len(n_id))
  value_cols <- paste0("v",  seq_len(n_val))
  n_cols     <- n_id + n_val

  df <- make_wide_input(n_rows, n_id, n_val)

  df_dt <- as.data.table(df)
  pd_df <- reticulate::r_to_py(df)
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

  list(
    df             = df,
    df_dt          = df_dt,
    pd_df          = pd_df,
    pl_df          = pl_df,
    ddf            = ddf,
    con            = con,
    id_cols        = id_cols,
    value_cols     = value_cols,
    id_cols_py     = reticulate::r_to_py(id_cols),
    sql_unpivot    = sql_unpivot,
    sql_unpivot_py = reticulate::r_to_py(sql_unpivot),
    n_rows         = n_rows,
    n_id           = n_id,
    n_val          = n_val,
    n_cols         = n_cols
  )
}

run_melt_bench <- function(inputs, label = "",
                           csv_path = .MELT_CSV) {

  df             <- inputs$df
  df_dt          <- inputs$df_dt
  pd_df          <- inputs$pd_df
  pl_df          <- inputs$pl_df
  ddf            <- inputs$ddf
  con            <- inputs$con
  id_cols        <- inputs$id_cols
  id_cols_py     <- inputs$id_cols_py
  sql_unpivot_py <- inputs$sql_unpivot_py
  n_rows         <- inputs$n_rows
  n_id           <- inputs$n_id
  n_val          <- inputs$n_val
  n_cols         <- inputs$n_cols

  if (!nzchar(label))
    label <- sprintf("Melt: rows=%s, %s + %d val",
                     format(n_rows, big.mark = ","), id_desc(n_id), n_val)

  cat(sprintf("\n%s\n[%s] mixed=%s\n%s\n",
              strrep("=", 100), label,
              as.character(mixed_types_enabled() && n_id >= 2L),
              strrep("=", 100)))

  expected_output_bytes <-
    as.numeric(n_rows) * as.numeric(n_val) *
    (12 + 4 * as.numeric(n_id))
  sentinel <- sentinel_alloc(sentinel_mult() * expected_output_bytes)
  gc(verbose = FALSE, full = TRUE)
  py_gc_collect()

  main <- reticulate::import_main()
  main$bench_pd_df   <- pd_df
  main$bench_pl_df   <- pl_df
  main$bench_ddf     <- ddf
  main$bench_con     <- con
  main$bench_id_cols <- id_cols_py
  main$bench_sql     <- sql_unpivot_py

  tools <- list(
    reshape2   = function() reshape2::melt(df, id.vars = id_cols),
    data.table = function() data.table::melt(df_dt, id.vars = id_cols),
    tidyr      = function() tidyr::pivot_longer(df, cols = -seq_along(id_cols)),
    collapse   = function() collapse::pivot(df, how = "longer",
                                            ids = id_cols),
    dataprep   = function() dataprep::melt(df, id.vars = id_cols),
    pandas     = function() melt_py_call("bench_melt_pandas()"),
    polars     = function() melt_py_call("bench_melt_polars()"),
    dask       = function() melt_py_call("bench_melt_dask()"),
    duckdb     = function() melt_py_call("bench_melt_duckdb()")
  )

  cat("\n  Per-tool warmup + timed runs:\n")
  sm_list <- lapply(names(tools), function(nm)
    bench_one(tools[[nm]], nm, family = "melt"))

  main$bench_pd_df   <- NULL
  main$bench_pl_df   <- NULL
  main$bench_ddf     <- NULL
  main$bench_con     <- NULL
  main$bench_id_cols <- NULL
  main$bench_sql     <- NULL

  sm <- do.call(rbind, sm_list)
  sm$n_rows <- n_rows
  sm$n_cols <- n_cols
  sm$n_id   <- n_id
  sm$n_val  <- n_val

  sm <- add_relative_cols(sm)
  sm$label <- label

  sm <- sm[order(sm$skipped, sm$median, na.last = TRUE), ]
  sm <- sm[, c("label", "tool", "n_rows", "n_cols", "n_id", "n_val",
               "times", "first_run_sec", "skipped",
               "min", "lq", "mean", "median", "uq", "max", "neval",
               "gc_sec",
               "relative_mean", "relative_median")]

  cat("\n  Results (sorted by skipped, then median, ms):\n")
  print(sm, digits = 4, row.names = FALSE)

  write_result(sm, csv_path)

  invisible(NULL)
}

run_melt_cell <- function(n_rows, n_id, n_val, label = "",
                          csv_path = .MELT_CSV) {
  inputs <- prepare_melt_inputs(n_rows, n_id, n_val)
  on.exit({
    try(inputs$con$close(), silent = TRUE)
    rm(inputs); invisible(gc())
  }, add = TRUE)
  run_melt_bench(inputs, label = label, csv_path = csv_path)
}


# ============================================================================
# Section 6 -- adaptive per-tool benchmark for dcast
# ============================================================================
source(system.file("benchmark_helpers.R", package = "dataprep"))

reticulate::py_run_string("
def pivot_pandas(long_pd, id_cols):
    return long_pd.pivot(index=id_cols, columns='variable',
                         values='value').reset_index()
def pivot_polars(long_pl, id_cols):
    try:
        return long_pl.pivot(index=id_cols, on='variable',
                             values='value').to_pandas()
    except TypeError:
        return long_pl.pivot(index=id_cols, columns='variable',
                             values='value').to_pandas()
def pivot_dask(ddf, id_cols):
    pdf = ddf.compute()
    return pdf.pivot_table(index=id_cols, columns='variable',
                           values='value', aggfunc='first').reset_index()
def pivot_duckdb(con, sql):
    return con.sql(sql).df()
")

dcast_py_call <- function(fn, ...) reticulate::py_call(fn, ...)

prepare_dcast_inputs <- function(n_long, n_id, n_levels) {
  id_cols <- paste0("id", seq_len(n_id))

  long   <- make_long(n_long, n_id, n_levels)
  n_long <- nrow(long)

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

  list(
    long           = long,
    long_dt        = long_dt,
    long_pd        = long_pd,
    long_pl        = long_pl,
    ddf            = ddf,
    con            = con,
    id_cols        = id_cols,
    sql_pivot      = sql_pivot,
    sql_pivot_py   = reticulate::r_to_py(sql_pivot),
    id_cols_py     = reticulate::r_to_py(id_cols),
    n_long         = n_long,
    n_id           = n_id,
    n_levels       = n_levels
  )
}

run_dcast_bench <- function(inputs, label = "",
                            csv_path = .DCAST_CSV) {

  long           <- inputs$long
  long_dt        <- inputs$long_dt
  long_pd        <- inputs$long_pd
  long_pl        <- inputs$long_pl
  ddf            <- inputs$ddf
  con            <- inputs$con
  id_cols        <- inputs$id_cols
  sql_pivot_py   <- inputs$sql_pivot_py
  id_cols_py     <- inputs$id_cols_py
  n_long         <- inputs$n_long
  n_id           <- inputs$n_id
  n_levels       <- inputs$n_levels

  if (!nzchar(label))
    label <- sprintf("Dcast: n_long=%s, %s + %d lvl",
                     format(n_long, big.mark = ","), id_desc(n_id), n_levels)

  cat(sprintf("\n%s\n[%s] mixed=%s\n%s\n",
              strrep("=", 100), label,
              as.character(mixed_types_enabled() && n_id >= 2L),
              strrep("=", 100)))

  expected_output_bytes <-
    as.numeric(n_long) * 8 +
    as.numeric(n_long %/% max(1L, n_levels)) * as.numeric(n_id) * 8
  sentinel <- sentinel_alloc(sentinel_mult() * expected_output_bytes)
  gc(verbose = FALSE, full = TRUE)
  py_gc_collect()

  py_pivot_pandas <- reticulate::py$pivot_pandas
  py_pivot_polars <- reticulate::py$pivot_polars
  py_pivot_dask   <- reticulate::py$pivot_dask
  py_pivot_duckdb <- reticulate::py$pivot_duckdb

  fml <- as.formula(paste(paste(id_cols, collapse = "+"), "~ variable"))

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
    collapse   = function()
      collapse::pivot(long,
                      how    = "wider",
                      ids    = id_cols,
                      values = "value",
                      names  = "variable"),
    dataprep   = function()
      dataprep::dcast(long, id = id_cols,
                      variable = "variable", value = "value"),
    pandas     = function()
      dcast_py_call(py_pivot_pandas, long_pd, id_cols_py),
    polars     = function()
      dcast_py_call(py_pivot_polars, long_pl, id_cols_py),
    dask       = function()
      dcast_py_call(py_pivot_dask, ddf, id_cols_py),
    duckdb     = function()
      dcast_py_call(py_pivot_duckdb, con, sql_pivot_py)
  )

  cat("\n  Per-tool warmup + timed runs:\n")
  sm_list <- lapply(names(tools), function(nm)
    bench_one(tools[[nm]], nm, family = "dcast"))

  sm <- do.call(rbind, sm_list)
  sm$n_long   <- n_long
  sm$n_id     <- n_id
  sm$n_levels <- n_levels

  sm <- add_relative_cols(sm)
  sm$label <- label

  sm <- sm[order(sm$skipped, sm$median, na.last = TRUE), ]
  sm <- sm[, c("label", "tool", "n_long", "n_id", "n_levels",
               "times", "first_run_sec", "skipped",
               "min", "lq", "mean", "median", "uq", "max", "neval",
               "gc_sec",
               "relative_mean", "relative_median")]

  cat("\n  Results (sorted by skipped, then median, ms):\n")
  print(sm, digits = 4, row.names = FALSE)

  write_result(sm, csv_path)

  invisible(NULL)
}

run_dcast_cell <- function(n_long, n_id, n_levels, label = "",
                           csv_path = .DCAST_CSV) {
  inputs <- prepare_dcast_inputs(n_long, n_id, n_levels)
  on.exit({
    try(inputs$con$close(), silent = TRUE)
    rm(inputs); invisible(gc())
  }, add = TRUE)
  run_dcast_bench(inputs, label = label, csv_path = csv_path)
}


# ============================================================================
# Section 6b -- AGGREGATED dcast benchmark (9 engines x 5 aggregates x 5 sizes)
# ============================================================================
source(system.file("benchmark_helpers.R", package = "dataprep"))

run_dcast_agg_bench <- function(long, agg_name,
                                n_levels = 10L,
                                csv_path = .DCAST_CSV) {

  n_long <- nrow(long)
  label  <- sprintf("Dcast agg=%-5s: n_long=%s, 1 id + %d lvl, K=3",
                    agg_name, format(n_long, big.mark = ","), n_levels)

  cat(sprintf("\n%s\n[%s]\n%s\n",
              strrep("=", 100), label, strrep("=", 100)))

  expected_output_bytes <-
    as.numeric(length(unique(long$id))) * n_levels * 8
  sentinel <- sentinel_alloc(sentinel_mult() * expected_output_bytes)
  gc(verbose = FALSE, full = TRUE)
  py_gc_collect()

  long_dt <- as.data.table(long)
  long_pd <- reticulate::r_to_py(long)
  long_pl <- polars$DataFrame(long)
  ddf     <- dask_dataframe$from_pandas(long, npartitions = 4L)

  con <- duckdb$connect()
  try(con$unregister("long_agg_bench", fail_if_missing = TRUE),
      silent = TRUE)
  con$register("long_agg_bench", long)
  sql_pivot <- sprintf(
    "PIVOT long_agg_bench ON variable USING %s(value) GROUP BY id;",
    SQL_AGGS[[agg_name]])

  main <- reticulate::import_main()
  main$bench_long_pd  <- long_pd
  main$bench_long_pl  <- long_pl
  main$bench_long_ddf <- ddf
  main$bench_con_agg  <- con

  # Python entry points, defined once per cell and cleaned up after.
  #
  # NOTE: pivot_table() places 'id' into the result's *index*.  Plain
  # .reset_index() moves it back into a column and yields a unique
  # 0..n-1 row index.  .reset_index(drop=True) would DROP 'id' and
  # break the aggregation benchmark.
  py_run_string(sprintf("
def _bench_polars_agg():
    return bench_long_pl.pivot(index='id', on='variable', values='value',
                               aggregate_function='%s').to_pandas()
def _bench_pandas_agg():
    return bench_long_pd.pivot_table(index='id', columns='variable',
                                     values='value',
                                     aggfunc='%s').reset_index()
def _bench_dask_agg():
    return bench_long_ddf.compute().pivot_table(index='id',
                                                columns='variable',
                                                values='value',
                                                aggfunc='%s').reset_index()
def _bench_duckdb_agg():
    return bench_con_agg.sql('%s').df()
", PY_AGGS[[agg_name]], PY_AGGS[[agg_name]], PY_AGGS[[agg_name]],
   gsub("'", "''", sql_pivot)))

  fn_agg <- R_AGGS[[agg_name]]

  tools <- list(
    dataprep = function()
      dataprep::dcast(long, id = "id", variable = "variable",
                      value = "value", fun.aggregate = fn_agg),
    collapse = function()
      collapse::pivot(long, how = "wider", ids = "id",
                      values = "value", names = "variable",
                      FUN = fn_agg),
    reshape2 = function()
      reshape2::dcast(long, id ~ variable, value.var = "value",
                      fun.aggregate = R_AGGS_RESHAPE2[[agg_name]]),
    data.table = function()
      data.table::dcast(long_dt, id ~ variable, value.var = "value",
                        fun.aggregate = fn_agg),
    tidyr = function()
      tidyr::pivot_wider(long, id_cols = id,
                         names_from = variable, values_from = value,
                         values_fn = R_AGGS[[agg_name]]),
    polars = function() reticulate::py_eval("_bench_polars_agg()",
                                            convert = FALSE),
    pandas = function() reticulate::py_eval("_bench_pandas_agg()",
                                            convert = FALSE),
    dask   = function() reticulate::py_eval("_bench_dask_agg()",
                                            convert = FALSE),
    duckdb = function() reticulate::py_eval("_bench_duckdb_agg()",
                                            convert = FALSE)
  )

  cat("\n  Per-tool warmup + timed runs:\n")
  sm_list <- lapply(names(tools), function(nm)
    bench_one(tools[[nm]], nm, family = "dcast"))

  main$bench_long_pd  <- NULL
  main$bench_long_pl  <- NULL
  main$bench_long_ddf <- NULL
  main$bench_con_agg  <- NULL
  py_run_string(
    "del _bench_polars_agg, _bench_pandas_agg, _bench_dask_agg, _bench_duckdb_agg")
  try(con$close(), silent = TRUE)

  sm <- do.call(rbind, sm_list)
  sm$n_long   <- n_long
  sm$n_id     <- 1L
  sm$n_levels <- n_levels

  sm <- add_relative_cols(sm)
  sm$label <- label

  sm <- sm[order(sm$skipped, sm$median, na.last = TRUE), ]
  sm <- sm[, c("label", "tool", "n_long", "n_id", "n_levels",
               "times", "first_run_sec", "skipped",
               "min", "lq", "mean", "median", "uq", "max", "neval",
               "gc_sec",
               "relative_mean", "relative_median")]

  cat("\n  Results (sorted by skipped, then median, ms):\n")
  print(sm, digits = 4, row.names = FALSE)

  write_result(sm, csv_path)
  invisible(NULL)
}

# --- driver: 5 sizes x 5 aggregates -----------------------------------------
dcast_agg_sweep <- function(sizes = c(1e4, 1e5, 1e6, 1e7, 1e8),
                            aggs  = names(R_AGGS),
                            n_levels = 10L, K = 3L) {
  for (nlong in sizes) {
    for (agg_name in aggs) {
      set.seed(123)
      long <- make_agg_input(nlong, n_levels = n_levels, K = K)
      run_dcast_agg_bench(long, agg_name, n_levels = n_levels)
      rm(long); invisible(gc())
    }
  }
}

# NOTE: the aggregated-dcast sweep is invoked exactly once, from the
# Driver block at the bottom of this file.  Do not call it again from
# this location, or the full 5x5 aggregated-dcast sweep will run twice.


# ============================================================================
# Driver -- full sweep when DATAPREP_RUN_BENCHMARK=1
# ============================================================================
if (.run_bench) {

  unlink(c(.MELT_CSV, .DCAST_CSV))

  # ---- melt sweep ---------------------------------------------------------
  set.seed(123)

  bench_reset_disabled("melt")
  for (nr in 10^(3:8)) {
    run_melt_cell(nr, n_id = 1, n_val = 9,
                  label = sprintf("Melt: rows=%s, %s + 9 val",
                                  format(nr, big.mark = ","), id_desc(1L)))
  }

  bench_reset_disabled("melt")
  for (nr in 10^(3:7)) {
    run_melt_cell(nr, n_id = 10, n_val = 9,
                  label = sprintf("Melt: rows=%s, %s + 9 val",
                                  format(nr, big.mark = ","), id_desc(10L)))
  }

  bench_reset_disabled("melt")
  for (nc in 10^(1:4)) {
    run_melt_cell(1e3, n_id = 1, n_val = nc,
                  label = sprintf("Melt: 1e3 rows, %s + %d val",
                                  id_desc(1L), nc))
  }

  bench_reset_disabled("melt")
  for (nc in 10^(1:4)) {
    run_melt_cell(1e3, n_id = 10, n_val = nc,
                  label = sprintf("Melt: 1e3 rows, %s + %d val",
                                  id_desc(10L), nc))
  }

  # ---- dcast sweep --------------------------------------------------------
  bench_reset_disabled("dcast")
  for (nl in 10^(3:8)) {
    run_dcast_cell(nl, n_id = 1L, n_levels = 10L,
                   label = sprintf("Dcast: n_long=%s, %s + 10 lvl",
                                   format(nl, big.mark = ","), id_desc(1L)))
  }

  bench_reset_disabled("dcast")
  for (lv in c(100L, 1000L, 10000L)) {
    run_dcast_cell(1e6, n_id = 1L, n_levels = lv,
                   label = sprintf("Dcast: n_long=%s, %s + %d lvl",
                                   format(1e6, big.mark = ","),
                                   id_desc(1L), lv))
  }

  bench_reset_disabled("dcast")
  for (ni in c(2L, 10L, 100L)) {
    run_dcast_cell(1e6, n_id = ni, n_levels = 10L,
                   label = sprintf("Dcast: n_long=%s, %s + 10 lvl",
                                   format(1e6, big.mark = ","),
                                   id_desc(ni)))
  }

  bench_reset_disabled("dcast")
  for (nl in setdiff(10^(4:8), 1e6)) {
    run_dcast_cell(nl, n_id = 1L, n_levels = 100L,
                   label = sprintf("Dcast: n_long=%s, %s + 100 lvl",
                                   format(nl, big.mark = ","), id_desc(1L)))
  }

  # ---- aggregated dcast sweep (single pass) ------------------------------
  bench_reset_disabled("dcast")
  dcast_agg_sweep()
}
