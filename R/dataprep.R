#' One-call data preprocessing pipeline
#' @param data A data frame.
#' @param cols Columns to process.
#' @param group Grouping column.
#' @param optimal Logical; use \code{optisolu} to search for optimal parameters.
#' @param interval,times Parameters for \code{condextr}.
#' @param fraction Missing fraction threshold for \code{varidele}.
#' @param top,top.error,top.magnitude,bottom,bottom.error,bottom.magnitude Outlier thresholds.
#' @param by,half Time parameters.
#' @param intervals Time gap for interpolation.
#' @param date_col Time column.
#' @param cores Number of CPU cores.
#' @param verbose Logical.
#' @return A preprocessed data frame.
#' @export
#' @noRd
dataprep <- function(data, cols = NULL, group = NULL,
                     optimal = FALSE, interval = 10, times = 10,
                     fraction = .25,
                     top = .995, top.error = .1, top.magnitude = .2,
                     bottom = .0025, bottom.error = .2, bottom.magnitude = .4,
                     by = 'min', half = 30, intervals = 30,
                     date_col = NULL, cores = NULL, verbose = FALSE) {
  t0 <- Sys.time()
  date_info <- resolve_date_col(data, date_col)

  idx <- resolve_cols(data, cols)
  original_names <- names(data)
  original_group <- group

  a <- varidele(data, cols = idx, fraction = fraction, verbose = verbose)
  if (is.null(a)) return(NULL)

  retained_names <- intersect(original_names[idx], names(a))
  if (length(retained_names) < 1) {
    if (verbose) cat("No selected variables remain after varidele\n")
    return(NULL)
  }

  if (!is.null(group)) {
    if (is.numeric(group)) {
      group_name <- original_names[group]
    } else {
      group_name <- group
    }
    if (!(group_name %in% names(a))) {
      stop("Group column was removed during variable deletion")
    }
  } else {
    group_name <- NULL
  }

  b <- obsedele(a, cols = retained_names, group = group_name,
                by = by, half = half, date_col = date_col,
                cores = cores, verbose = verbose)

  if (optimal) {
    s <- optisolu(b, cols = retained_names, group = group_name,
                  interval = interval, times = times,
                  top = top, top.error = top.error, top.magnitude = top.magnitude,
                  bottom = bottom, bottom.error = bottom.error, bottom.magnitude = bottom.magnitude,
                  by = by, half = half, date_col = date_col,
                  cores = cores, verbose = verbose)
    opt_rows <- which(s$optimal)
    if (length(opt_rows) > 0) {
      interval <- s$interval[opt_rows[1]]
      times <- s$times[opt_rows[1]]
      if (verbose) cat("Optimal solution found: interval =", interval, "times =", times, "\n")
    } else {
      stop("No optimal solution found; please increase interval/times")
    }
  }

  c <- condextr(b, cols = retained_names, group = group_name,
                interval = interval, times = times,
                top = top, top.error = top.error, top.magnitude = top.magnitude,
                bottom = bottom, bottom.error = bottom.error, bottom.magnitude = bottom.magnitude,
                by = by, half = half, date_col = date_col,
                cores = cores, verbose = verbose)

  d <- shorvalu(c, cols = retained_names, intervals = intervals,
                date_col = date_col, verbose = verbose)

  if (verbose) {
    if (optimal) cat("Optimal solution found with interval =", interval, "and times =", times, "\n")
    cat("Time used by dataprep:", format(Sys.time() - t0, digits = 3), "\n")
  }
  d
}