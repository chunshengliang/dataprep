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
# ============================================================

if (!nzchar(Sys.getenv("DATAPREP_RUN_BENCHMARK"))) {
  message("Benchmark scripts are disabled. ",
          "Set DATAPREP_RUN_BENCHMARK=1 to enable.")
  return(invisible(NULL))
}

# ---- optional dependencies ----
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

# ---- optional Python modules ----
if (!reticulate::py_available(initialize = TRUE)) {
  stop("Python is not available to reticulate.")
}

pandas         <- tryCatch(import("pandas"),
                           error = function(e) NULL)
polars         <- tryCatch(import("polars"),
                           error = function(e) NULL)
dask_dataframe <- tryCatch(import("dask.dataframe"),
                           error = function(e) NULL)
duckdb         <- tryCatch(import("duckdb"),
                           error = function(e) NULL)

if (is.null(pandas) || is.null(polars) ||
    is.null(dask_dataframe) || is.null(duckdb)) {
  stop("Python modules pandas / polars / dask / duckdb must be ",
       "installed in the reticulate environment.")
}

py_run_string("import warnings; warnings.filterwarnings('ignore')")

options(scipen = 999)

# ------------------------------------------------------------
# Global state: tools permanently disabled within a sequence.
#   bench_state$disabled$melt  -> character vector
#   bench_state$disabled$dcast -> character vector
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
# Empty / skipped result row
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
#
#   family        : "melt" or "dcast" -- separates disable lists
#   fn            : zero-arg closure returning a materialized result
#   max_first_sec : first call must finish within this many seconds
#   budget_sec    : upper bound on total time spent on microbenchmark
# ------------------------------------------------------------
bench_one <- function(fn, tool,
                      family        = "melt",
                      unit          = "ms",
                      verbose       = TRUE,
                      max_first_sec = 15,
                      budget_sec    = 15) {

  # ---- already disabled within this sequence? skip without calling ----
  if (bench_is_disabled(family, tool)) {
    if (verbose) cat(sprintf("    %-10s SKIPPED (disabled in this sequence)\n",
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

  # ---- permanent disable if first call exceeded the cap ----
  if (!ok || (is.finite(first_sec) && first_sec > max_first_sec)) {
    bench_disable(family, tool)
    reason <- if (!ok) msg else
      sprintf("%.2fs > cap %ds", first_sec, max_first_sec)
    if (verbose) {
      cat(sprintf("    %-10s DISABLED after %7.2fs -- %s\n",
                  tool, first_sec, reason))
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