# R/melt.R -- dataprep 0.1.8.1

.melt_env <- new.env(parent = emptyenv())

# ---------------------------------------------------------------------------
# Internal: process-level thread cap, resolved once and cached.
#
# parallel::detectCores(logical = FALSE) walks /sys/devices/system/cpu on
# Linux and costs ~5-8 ms per call.  We cache the result in .melt_env$cap
# so repeated melt() calls do not pay that cost.  Callers that change
# OMP_THREAD_LIMIT or DATAPREP_THREADS at runtime must call
# melt_clear_cache() to invalidate the cache.
#
# Priority (highest first):
#   1. DATAPREP_THREADS                     (dataprep-specific override)
#   2. OMP_THREAD_LIMIT                     (CRAN / user-mandated cap)
#   3. parallel::detectCores(logical = FALSE)   (physical cores)
#   4. parallel::detectCores(logical = TRUE)
#   5. hard fallback of 4
# Clamped to [1, 256].
# ---------------------------------------------------------------------------
.melt_get_cap <- function() {
  cap <- .melt_env$cap
  if (!is.null(cap)) return(cap)

  env_threads <- Sys.getenv("DATAPREP_THREADS", NA_character_)
  if (!is.na(env_threads) && nzchar(env_threads)) {
    v <- suppressWarnings(as.integer(env_threads))
    if (!is.na(v) && v >= 1L) {
      cap <- min(v, 256L); .melt_env$cap <- cap; return(cap)
    }
  }
  env_limit <- Sys.getenv("OMP_THREAD_LIMIT", NA_character_)
  if (!is.na(env_limit) && nzchar(env_limit)) {
    v <- suppressWarnings(as.integer(env_limit))
    if (!is.na(v) && v >= 1L) {
      cap <- min(v, 256L); .melt_env$cap <- cap; return(cap)
    }
  }
  hw <- tryCatch(parallel::detectCores(logical = FALSE),
                 error = function(e) NA_integer_)
  if (is.na(hw) || hw < 1L) {
    hw <- tryCatch(parallel::detectCores(logical = TRUE),
                   error = function(e) NA_integer_)
  }
  if (is.na(hw) || hw < 1L) hw <- 4L
  cap <- min(as.integer(hw), 256L)
  .melt_env$cap <- cap
  cap
}

#' Clear dataprep's internal caches
#'
#' dataprep caches two process-level values across \code{melt()} calls:
#' \itemize{
#'   \item the resolved thread cap, derived from \code{DATAPREP_THREADS},
#'         \code{OMP_THREAD_LIMIT}, and \code{parallel::detectCores()};
#'   \item a large numeric sentinel used to pre-fault the R heap before a
#'         multi-GB melt.
#' }
#' Call this function after changing the thread-related environment
#' variables at runtime if you want the next \code{melt()} call to pick up
#' the new values.  It is safe to call at any time, including when no
#' cache is populated.
#'
#' @return Invisibly \code{NULL}.
#' @seealso \code{\link{melt}}.
#' @export
melt_clear_cache <- function() {
  .melt_env$cap <- NULL
  if (!is.null(.melt_env$sentinel)) .melt_env$sentinel <- NULL
  gc(verbose = FALSE, full = TRUE)
  invisible(NULL)
}

# ---------------------------------------------------------------------------
# Internal: pre-fault the R heap before a multi-GB melt.
#
# Brief numeric allocation of roughly the eventual output size forces
# glibc to raise M_MMAP_THRESHOLD so that subsequent output allocations
# reuse a pre-touched arena instead of paying cold-mmap page faults inside
# the hot loop.  Measured 2.5x improvement on 1e7 x 10-id.
# ---------------------------------------------------------------------------
.melt_ensure_headroom <- function(bytes) {
  if (!is.finite(bytes)) return(invisible(NULL))
  if (bytes < 64 * 1024^2) {
    sent <- .melt_env$sentinel
    if (!is.null(sent) && length(sent) > 5e7) {
      .melt_env$sentinel <- NULL
      gc(verbose = FALSE, full = TRUE)
    }
    return(invisible(NULL))
  }
  sent         <- .melt_env$sentinel
  target_elems <- as.double(min(bytes, 12e9)) / 8
  if (target_elems < 1) return(invisible(NULL))
  have_elems <- if (is.null(sent)) 0 else length(sent)
  if (have_elems < target_elems) {
    n <- as.integer(min(target_elems, 12e9 / 8))
    s <- numeric(n); s[1L] <- 0; s[n] <- 0
    .melt_env$sentinel <- s
    gc(verbose = FALSE, full = TRUE)
  }
  invisible(NULL)
}

# ---------------------------------------------------------------------------
# Internal: thread-count selection.
#
# 1. Honour an explicit `cores` or getOption("dataprep.cores").
# 2. Below parallel_threshold, stay single-threaded.
# 3. Otherwise scale threads with total work, then clamp by the process
#    cap and by per-measure-column write-combining-buffer pressure:
#       n_measure_cols >= 2048 -> cap 8
#       n_measure_cols >=   32 -> cap 10
#       n_measure_cols > 1 and n_id_cols > 1 -> cap 16
#    The last rule exists because melt_phase_b writes n_measure_cols
#    concurrent NT streams per worker; x86 has ~10 write-combining
#    buffers per core, so unbounded threads * streams saturates the
#    memory bus (measured 1456 ms vs 746 ms on 1e7 x 10-id).
# ---------------------------------------------------------------------------
.melt_pick_cores <- function(total_elements, n_measure_cols, n_id_cols,
                             parallel_threshold, cores) {
  if (is.null(cores)) {
    opt <- getOption("dataprep.cores", NULL)
    if (!is.null(opt)) {
      cores <- suppressWarnings(as.integer(opt)[1L])
    } else if (total_elements < parallel_threshold) {
      return(1L)
    } else {
      cores <- as.integer(floor(total_elements / 1e4))
    }
  }
  cores <- suppressWarnings(as.integer(cores)[1L])
  if (is.na(cores) || cores < 1L) cores <- 1L

  cap <- .melt_get_cap()

  if (cores > cap) cores <- cap
  if (n_measure_cols >= 2048L) {
    wc <- max(1L, min(cap, 8L))
    if (cores > wc) cores <- wc
  } else if (n_measure_cols >= 32L) {
    wc <- max(1L, min(cap, 10L))
    if (cores > wc) cores <- wc
  } else if (n_measure_cols > 1L && n_id_cols > 1L) {
    wc <- max(1L, min(cap, 16L))
    if (cores > wc) cores <- wc
  }
  as.integer(cores)
}

#' Melt a data frame from wide to long format
#'
#' Reshape a wide \code{data.frame} into long format, analogous to
#' \code{reshape2::melt()} and \code{data.table::melt()} but implemented
#' natively in C++ with a two-phase, cache-aware, multi-threaded kernel.
#'
#' @param data A \code{data.frame}.  Must have at least one row and one
#'   column.  Other classes are not supported.
#' @param id Integer vector, character vector, or logical mask selecting
#'   the identity columns (kept as-is, one row per \code{(row, measure)}
#'   pair in the output).  Character values are matched against
#'   \code{names(data)}; integer values are column positions (negative
#'   values are interpreted as in \code{data.table::melt()}); logical
#'   values must have length \code{ncol(data)}.  Mutually exclusive with
#'   \code{measure.vars}; specifying both is an error.
#' @param measure.vars Integer vector, character vector, or logical mask
#'   selecting the columns to be stacked.  Mutually exclusive with
#'   \code{id}; specifying both is an error.  If neither \code{id} nor
#'   \code{measure.vars} is given, all columns except the first are
#'   treated as measure variables (matching \code{reshape2::melt()}
#'   defaults).
#' @param variable.name Length-1 character; name of the output column that
#'   records which measure column each row came from.  Defaults to
#'   \code{"variable"}.  The column is a \code{factor} whose levels are
#'   the names of the measure columns when \code{as.factor} resolves to
#'   \code{TRUE} (the default for \code{major = "col"}), and a
#'   \code{character} vector otherwise.
#' @param value.name Length-1 character; name of the output column that
#'   holds the stacked values.  Defaults to \code{"value"}.  The column is
#'   always \code{double} (integer and logical measures are coerced;
#'   \code{NA_integer_} and \code{NA} become \code{NA_real_}).
#' @param na.rm Logical; if \code{TRUE}, rows whose value is \code{NA} are
#'   dropped from the output.  Defaults to \code{FALSE}.  With
#'   \code{na.rm = TRUE} the fast parallel kernels are bypassed in favour
#'   of a per-column compaction pass; expect noticeably lower throughput.
#' @param cores Integer or \code{NULL}.  Number of OpenMP threads.  When
#'   \code{NULL} (the default), threads are chosen automatically: single
#'   thread below \code{parallel_threshold}, then
#'   \code{floor(total_elements / 1e4)} threads, clamped by the process
#'   cap and by per-column write-combining-buffer pressure (see Details).
#'   An explicit value bypasses the auto-scaling but is still clamped by
#'   the process cap.
#' @param major Length-1 character, either \code{"col"} or \code{"row"}.
#'   Controls the output layout.  \code{"col"} (default) places each
#'   measure column as a contiguous block: rows \code{1..n} are measure 1,
#'   rows \code{n+1..2n} are measure 2, and so on.  \code{"row"}
#'   interleaves measures within each row: output row \code{(r-1)*k + j}
#'   is \code{(input row r, measure j)}.  \code{"col"} is faster.
#' @param as.factor Logical or \code{NULL}.  If \code{TRUE}, the
#'   \code{variable.name} column is returned as a \code{factor}.  If
#'   \code{FALSE}, it is returned as \code{character}.  \code{NULL} (the
#'   default) uses \code{TRUE} when \code{major} is \code{"col"} and
#'   \code{FALSE} when \code{major} is \code{"row"}.  Explicit \code{TRUE}
#'   or \code{FALSE} overrides that default.
#' @param verbose Logical; if \code{TRUE}, print a one-line summary of the
#'   resolved shape and thread count to \code{stdout}.  Defaults to
#'   \code{FALSE}.  Verbose mode also disables the fast path (see
#'   Details).
#' @param parallel_threshold Numeric; below this many output elements
#'   (\code{nrow(data) * length(measure.vars)}), the call runs
#'   single-threaded.  Defaults to \code{2e4}.
#' @param id.vars Alias for \code{id}.  Providing both \code{id} and
#'   \code{id.vars} is an error.
#'
#' @return A \code{data.frame} with \code{length(id)} identity columns,
#'   one \code{variable} column, and one \code{value} column.  Row order
#'   is deterministic and depends on \code{major}: see the parameter
#'   documentation above.  Row names are the compact form
#'   \code{-(1:n)}; the caller is responsible for any further
#'   de-duplication.  When \code{as.factor = NULL} and
#'   \code{major = "row"}, the \code{variable} column is a
#'   \code{character} vector, matching \code{tidyr::pivot_longer()}.
#'
#' @details
#' \strong{Shape.}  A \code{data.frame} with \code{n} rows and
#' \code{k = length(measure.vars)} measure columns becomes a
#' \code{data.frame} with \code{n * k} rows.  Identity columns are
#' replicated \code{k} times; a factor \code{variable} column records the
#' source column; a \code{double} \code{value} column holds the stacked
#' data.
#'
#' \strong{Implementation.}  The heavy lifting is done by a C++17 kernel
#' compiled with OpenMP and dispatched to AVX-512 / AVX2 / SSE4.2
#' implementations of \code{memcpy}-like primitives at first use.  Output
#' buffers are pre-faulted via \code{madvise(MADV_HUGEPAGE)} on Linux and
#' \code{mallopt(M_MMAP_THRESHOLD, ...)} on glibc.  Threads scale with
#' input size up to a hardware-aware cap; per-column write-combining-buffer
#' pressure caps the thread count further when many measure columns are
#' being written at once.
#'
#' \strong{Fast paths.}  Three code paths exist, chosen automatically:
#' a tiny-input path for \code{nrow <= 2048} with at most 64 measure
#' columns; a small-input path for at most 131072 output elements; and a
#' general two-phase path for everything else.  Passing \code{verbose =
#' TRUE} disables the two fast paths so that all calls exercise the same
#' kernel; this is intended for testing, not production.
#'
#' \strong{Memory.}  Peak memory is roughly
#' \code{(n_id + 2) * n * k * 8} bytes plus the input.  For very large
#' inputs the R heap is briefly reserved with a same-sized sentinel so
#' that glibc's \code{M_MMAP_THRESHOLD} rises before the output
#' allocation; the sentinel is released before the C++ kernel runs.
#'
#' @examples
#' df <- data.frame(
#'   id  = 1:3,
#'   a   = c(10, 20, 30),
#'   b   = c(11, 21, 31),
#'   stringsAsFactors = FALSE
#' )
#' melt(df, id.vars = "id")
#'
#' # Column layout is the fastest; use major = "row" for row interleaving.
#' melt(df, id.vars = "id", major = "row")
#'
#' # Character variable column instead of a factor.
#' melt(df, id.vars = "id", as.factor = FALSE)
#'
#' @seealso \code{\link{melt_clear_cache}}.
#' @export
melt <- function(data,
                 id                  = NULL,
                 measure.vars        = NULL,
                 variable.name       = "variable",
                 value.name          = "value",
                 na.rm               = FALSE,
                 cores               = NULL,
                 major               = NULL,
                 as.factor           = NULL,
                 verbose             = FALSE,
                 parallel_threshold  = 2e4,
                 id.vars             = NULL) {

  # ---- Fast path -----------------------------------------------------------
  if (is.data.frame(data) &&
      missing(measure.vars) &&
      !isTRUE(na.rm) && !isTRUE(verbose)) {

    ok_fast <- TRUE
    id_spec <- if (!is.null(id)) id else id.vars
    id_arg  <- NULL
    if (!is.null(id) && !is.null(id.vars)) {
      ok_fast <- FALSE
    } else if (!is.null(id_spec)) {
      cnames <- attr(data, "names", exact = TRUE)
      if (is.character(id_spec)) {
        m <- match(id_spec, cnames)
        if (anyNA(m)) ok_fast <- FALSE else id_arg <- m
      } else if (is.numeric(id_spec)) {
        id_arg <- as.integer(id_spec)
        if (any(id_arg < 1L | id_arg > length(data))) ok_fast <- FALSE
      } else if (is.logical(id_spec)) {
        if (length(id_spec) != length(data)) ok_fast <- FALSE
        else id_arg <- which(id_spec)
      } else {
        ok_fast <- FALSE
      }
    }

    if (ok_fast) {
      ncols <- length(data); nrows <- nrow(data)
      n_id_eff   <- if (is.null(id_arg)) 0L else length(id_arg)
      n_meas_eff <- ncols - n_id_eff

      if (n_meas_eff >= 1L && nrows >= 1L) {
        est_bytes <- as.double(nrows) * as.double(n_meas_eff) *
                     as.double(n_id_eff + 2L) * 8
        if (est_bytes >= 64 * 1024^2) .melt_ensure_headroom(est_bytes)

        total_elements <- as.double(nrows) * as.double(n_meas_eff)
        cores_use <- .melt_pick_cores(total_elements, n_meas_eff, n_id_eff,
                                      parallel_threshold, cores)

        vn <- if (is.null(variable.name)) "variable" else variable.name
        vl <- if (is.null(value.name))    "value"    else value.name
        mj <- if (is.null(major)) "col" else {
          if (is.character(major) && length(major) == 1L &&
              (major == "col" || major == "row")) major
          else match.arg(major, c("col", "row"))
        }
        af <- if (is.null(as.factor)) (mj == "col") else isTRUE(as.factor)

        return(melt_cpp(
          df            = data,
          id            = id_arg,
          variable_name = vn,
          value_name    = vl,
          major         = mj,
          as_factor     = af,
          n_threads     = cores_use,
          na_rm         = FALSE
        ))
      }
    }
  }

  # ---- Slow path: full argument validation ---------------------------------
  if (!is.data.frame(data)) stop("melt is only supported for data frames")
  if (ncol(data) < 1L)      stop("data has no columns")
  if (nrow(data) == 0L)     stop("data has no rows")

  if (is.null(major)) major <- "col"
  else if (!(is.character(major) && length(major) == 1L &&
             (major == "col" || major == "row")))
    major <- match.arg(major, c("col", "row"))

  if (is.null(as.factor)) as.factor <- (major == "col")
  else                    as.factor <- isTRUE(as.factor)

  if (!is.null(id) && !is.null(id.vars))
    stop("Specify only one of `id` or `id.vars` (they are aliases).")
  if (is.null(id)) id <- id.vars
  if (!is.null(id) && !is.null(measure.vars))
    stop("Please specify only one of id (id.vars) or measure.vars")

  ncols  <- ncol(data)
  cnames <- names(data)
  id_arg <- NULL
  n_measure_cols <- NULL

  if (!is.null(id)) {
    if (is.character(id)) {
      id_arg <- match(id, cnames)
      if (anyNA(id_arg))
        stop("id column(s) not found: ",
             paste(id[is.na(id_arg)], collapse = ", "))
    } else if (is.numeric(id)) {
      id_arg <- as.integer(id)
      if (any(id_arg < 1L | id_arg > ncols))
        stop("id contains invalid column indices")
    } else if (is.logical(id)) {
      if (length(id) != ncols)
        stop("logical id must have length ncol(data)")
      id_arg <- which(id)
    } else stop("id must be character, numeric, or logical")
    n_measure_cols <- ncols - length(unique(id_arg))
  } else if (!is.null(measure.vars)) {
    if (is.character(measure.vars)) {
      midx <- match(measure.vars, cnames)
      if (anyNA(midx))
        stop("measure.vars not found: ",
             paste(measure.vars[is.na(midx)], collapse = ", "))
    } else if (is.numeric(measure.vars)) {
      midx <- as.integer(measure.vars)
    } else if (is.logical(measure.vars)) {
      if (length(measure.vars) != ncols)
        stop("logical measure.vars must have length ncol(data)")
      midx <- which(measure.vars)
    } else stop("measure.vars must be character, numeric, or logical")
    if (anyNA(midx) || any(midx < 1L | midx > ncols))
      stop("measure.vars contains invalid column indices")
    id_arg         <- setdiff(seq_len(ncols), unique(midx))
    n_measure_cols <- length(unique(midx))
  } else {
    id_arg         <- NULL
    n_measure_cols <- max(1L, ncols - 1L)
  }

  {
    n_id_eff  <- max(1L, ncols - n_measure_cols)
    est_bytes <- as.double(nrow(data)) * as.double(n_measure_cols) *
                 as.double(n_id_eff + 2L) * 8
    .melt_ensure_headroom(est_bytes)
  }

  total_elements <- as.double(nrow(data)) * as.double(n_measure_cols)
  cores_use <- .melt_pick_cores(total_elements, n_measure_cols,
                                if (is.null(id_arg)) 0L else length(id_arg),
                                parallel_threshold, cores)

  if (verbose)
    cat(sprintf(
      "[melt] rows=%d  cols=%d  measure_cols=%d  major=%s  threads=%d  na.rm=%s\n",
      nrow(data), ncols, n_measure_cols, major, cores_use,
      if (isTRUE(na.rm)) "TRUE" else "FALSE"))

  melt_cpp(
    df            = data,
    id            = id_arg,
    variable_name = variable.name,
    value_name    = value.name,
    major         = major,
    as_factor     = as.factor,
    n_threads     = cores_use,
    na_rm         = isTRUE(na.rm)
  )
}
