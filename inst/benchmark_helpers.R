# ============================================================
# Shared helpers for adaptive per-tool benchmarks
#
# CRAN safety:
#   These scripts are NOT executed by R CMD check. Set the
#   environment variable DATAPREP_RUN_BENCHMARK=1 to enable them
#   in an interactive session.
#
#   The scripts also require 'microbenchmark', 'reticulate', and
#   a working Python installation with pandas / polars / dask /
#   duckdb. If any of these are missing, the script stops early
#   with a clear message.
#
# Timing semantics:
#   * R tools  : native R call returning a native R object.
#   * Py tools : enter Python, run the operation there, return a
#                Python DataFrame. The return value is kept on the
#                Python heap (no py_to_r marshalling) and input
#                parameters are pre-converted to Python objects
#                before the timed region.
#   * Input materialisation (r_to_py, polars$DataFrame,
#     from_pandas, duckdb$register) is done once, outside the
#     timed region.
# ============================================================

# ---- top-level guard ----
# Using `return()` at the top level of `source()` is unsafe; use a
# conditional block instead so the file is a no-op when the env
# variable is unset, while still loading the helper definitions
# below.
.run_bench <- nzchar(Sys.getenv("DATAPREP_RUN_BENCHMARK"))

if (!.run_bench) {
  message("Benchmark scripts are disabled. ",
          "Set DATAPREP_RUN_BENCHMARK=1 to enable.")
}

# ---- optional dependencies (only loaded when benchmarks are on) ----
if (.run_bench) {
  required <- c("microbenchmark", "reticulate",
                "data.table", "reshape2", "tidyr", "dataprep")
  missing_pkgs <- required[!vapply(required,
                                   requireNamespace, logical(1),
                                   quietly = TRUE)]
  if (length(missing_pkgs) > 0) {
    stop("Missing R packages: ", paste(missing_pkgs, collapse = ", "))
  }

  suppressPackageStartupMessages({
    library(microbenchmark)
    library(reticulate)
    library(data.table)
    library(reshape2)
    library(tidyr)
    library(dataprep)
  })

  if (!reticulate::py_available(initialize = TRUE)) {
    stop("Python is not available to reticulate.")
  }

  pandas         <- tryCatch(import("pandas"),         error = function(e) NULL)
  polars         <- tryCatch(import("polars"),         error = function(e) NULL)
  dask_dataframe <- tryCatch(import("dask.dataframe"), error = function(e) NULL)
  duckdb         <- tryCatch(import("duckdb"),         error = function(e) NULL)

  if (is.null(pandas) || is.null(polars) ||
      is.null(dask_dataframe) || is.null(duckdb)) {
    stop("Python modules pandas / polars / dask / duckdb must be ",
         "installed in the reticulate environment.")
  }

  py_run_string("import warnings; warnings.filterwarnings('ignore')")

  options(scipen = 999)

  # ---- Python-side helpers, defined once in __main__ ----
  # Each helper is a zero-argument closure that reads its inputs
  # from module-level globals registered by the R side before
  # timing begins (bench_pd_df, bench_pl_df, bench_ddf,
  # bench_con, bench_id_cols, bench_sql). This keeps the call
  # site inside the benchmark loop a single string with no
  # argument marshalling.
  reticulate::py_run_string("
def bench_melt_pandas():
    return bench_pd_df.melt(id_vars=bench_id_cols,
                            var_name='variable',
                            value_name='value')

def bench_melt_polars():
    return bench_pl_df.unpivot(index=bench_id_cols,
                               variable_name='variable',
                               value_name='value')

def bench_melt_dask():
    # dask's melt is lazy; compute() materialises to pandas, which
    # is the same finish line as pandas/polars (return a concrete DF).
    return bench_ddf.melt(id_vars=bench_id_cols,
                          var_name='variable',
                          value_name='value').compute()

def bench_melt_duckdb():
    return bench_con.sql(bench_sql).df()

def bench_dcast_pandas():
    return bench_long_pd.pivot(index=bench_id_cols,
                               columns='variable',
                               values='value').reset_index()

def bench_dcast_polars():
    try:
        return bench_long_pl.pivot(index=bench_id_cols,
                                   on='variable',
                                   values='value').to_pandas()
    except TypeError:
        return bench_long_pl.pivot(index=bench_id_cols,
                                   columns='variable',
                                   values='value').to_pandas()

def bench_dcast_dask():
    pdf = bench_ddf.compute()
    return pdf.pivot_table(index=bench_id_cols,
                           columns='variable',
                           values='value',
                           aggfunc='first').reset_index()

def bench_dcast_duckdb():
    return bench_con.sql(bench_sql).df()
")

  # ---- thin wrapper: py_eval without py_to_r ----
  # reticulate's py_call() has no `convert` argument in this
  # version, so route through py_eval() with convert = FALSE to
  # keep the return value on the Python heap.
  py_nc <- function(code) {
    reticulate::py_eval(code, convert = FALSE)
  }

  # ---- register __main__ handle for LHS assignment ----
  # `reticulate::py$name <- value` is illegal on the LHS of an
  # assignment (`::` cannot be the target of `$<-`); bind the
  # __main__ module to an R variable first, then assign into it.
  main <- reticulate::import_main()
}

# ------------------------------------------------------------
# State: tools marked "single-run" within the current sequence.
# A tool that exceeded max_first_sec on its first call still
# records its single-call time; it is not retried for the
# remaining cells in the same sequence.
# ------------------------------------------------------------
bench_state <- new.env(parent = emptyenv())
bench_state$disabled <- list(melt = character(0), dcast = character(0))

bench_is_disabled <- function(family, tool) {
  tool %in% bench_state$disabled[[family]]
}

bench_disable <- function(family, tool) {
  if (!bench_is_disabled(family, tool)) {
    bench_state$disabled[[family]] <-
      c(bench_state$disabled[[family]], tool)
  }
  invisible(NULL)
}

# Call at the start of a new gradient sequence.
bench_reset_disabled <- function(family = NULL) {
  if (is.null(family)) {
    bench_state$disabled <- list(melt = character(0), dcast = character(0))
  } else {
    bench_state$disabled[[family]] <- character(0)
  }
  invisible(NULL)
}

# ------------------------------------------------------------
# Adaptive `times` rule
# ------------------------------------------------------------
choose_times <- function(t_sec) {
  if (!is.finite(t_sec) || t_sec <= 0) return(1L)
  if (t_sec >  10)   return(1L)
  if (t_sec >   1)   return(5L)
  if (t_sec >   0.1) return(10L)
  if (t_sec >   0.01) return(20L)
  return(100L)
}

# ------------------------------------------------------------
# Single-run result row (no repeated measurement)
# ------------------------------------------------------------
bench_skipped_row <- function(tool, first_sec = NA_real_, times = 0L) {
  data.frame(
    tool          = tool,
    times         = times,
    first_run_sec = first_sec,
    skipped       = TRUE,
    min           = NA_real_,
    lq            = NA_real_,
    mean          = NA_real_,
    median        = NA_real_,
    uq            = NA_real_,
    max           = NA_real_,
    neval         = NA_integer_,
    stringsAsFactors = FALSE
  )
}

# ------------------------------------------------------------
# Bench one tool for one cell
# ------------------------------------------------------------
bench_one <- function(fn, tool,
                      family        = "melt",
                      unit          = "ms",
                      verbose       = TRUE,
                      max_first_sec = 15,
                      budget_sec    = 15) {

  # ---- already marked single-run within this sequence? ----
  if (bench_is_disabled(family, tool)) {
    if (verbose) cat(sprintf("    %-10s single-run (from previous cell)\n",
                             tool))
    return(bench_skipped_row(tool))
  }

  # ---- single call: warm-up + timing basis ----
  t0 <- proc.time()[["elapsed"]]
  ok <- TRUE
  msg <- ""
  tryCatch(
    {
      invisible(fn())
    },
    error = function(e) {
      ok  <<- FALSE
      msg <<- conditionMessage(e)
    }
  )
  first_sec <- proc.time()[["elapsed"]] - t0

  # ---- mark single-run if the first call exceeded the cap ----
  if (!ok || (is.finite(first_sec) && first_sec > max_first_sec)) {
    bench_disable(family, tool)
    if (verbose) {
      if (!ok) {
        cat(sprintf("    %-10s ERROR after %7.2fs -- %s\n",
                    tool, first_sec, msg))
      } else {
        cat(sprintf("    %-10s single-run, first=%8.2fs (cap=%ds)\n",
                    tool, first_sec, max_first_sec))
      }
    }
    return(bench_skipped_row(tool, first_sec = first_sec, times = 0L))
  }

  # ---- choose `times`: adaptive rule + budget guard ----
  times <- choose_times(first_sec)

  # Budget guard: only apply when the planned measurement would
  # exceed `budget_sec`. This avoids a division with a near-zero
  # denominator, which would overflow as.integer() and produce NA.
  if (is.finite(first_sec) && first_sec > 0 &&
      first_sec * times > budget_sec) {
    budget_times <- floor(budget_sec / first_sec)
    budget_times <- min(budget_times, .Machine$integer.max)
    budget_times <- as.integer(budget_times)
    if (!is.na(budget_times) && budget_times >= 1L &&
        budget_times < times) {
      times <- budget_times
    }
  }
  if (times < 1L) times <- 1L

  if (verbose) {
    cat(sprintf("    %-10s first=%10.5fs  times=%3d\n",
                tool, first_sec, times))
  }

  # ---- microbenchmark ----
  expr <- as.call(list(fn))
  mb <- tryCatch(
    eval(bquote(
      microbenchmark::microbenchmark(
        .(expr),
        times = .(times),
        unit  = .(unit)
      )
    )),
    error = function(e) {
      if (verbose) {
        cat(sprintf("    %-10s microbenchmark aborted: %s\n",
                    tool, conditionMessage(e)))
      }
      NULL
    }
  )

  if (is.null(mb)) {
    return(bench_skipped_row(tool, first_sec = first_sec, times = times))
  }

  sm <- summary(mb)
  sm$tool          <- tool
  sm$times         <- times
  sm$first_run_sec <- first_sec
  sm$skipped       <- FALSE
  sm$expr          <- NULL
  sm
}

# ------------------------------------------------------------
# Append a block of results to a CSV
# ------------------------------------------------------------
append_result <- function(sm, path) {
  if (file.exists(path)) {
    write.table(sm, path, sep = ",", append = TRUE,
                row.names = FALSE, col.names = FALSE)
  } else {
    write.csv(sm, path, row.names = FALSE)
  }
}