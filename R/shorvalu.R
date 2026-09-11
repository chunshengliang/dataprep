#' Short-period interpolation
#' @param data A data frame, matrix, or numeric vector.
#' @param cols Columns to interpolate.
#' @param intervals Time gap to define short periods.
#' @param units Time unit for \code{intervals}.
#' @param date_col Time column.
#' @param cores Number of CPU cores.
#' @param verbose Logical.
#' @return A data frame with missing values filled.
#' @export
#' @noRd
shorvalu <- function(data, cols = NULL, intervals = 30, units = 'mins',
                     date_col = NULL, cores = NULL, verbose = FALSE) {
  t0 <- Sys.time()
  if (is.vector(data) && !is.list(data)) {
    data <- lin_interp_cpp(data)
    if (verbose) cat("Vector interpolation completed\n")
    return(data)
  }

  idx <- resolve_cols(data, cols)
  check_numeric_cols(data, idx)

  date_info <- resolve_date_col(data, date_col)
  date_name <- date_info$name
  tv <- data[[date_name]]
  if (!inherits(tv, c("POSIXct", "Date"))) {
    stop("Time column must be POSIXct or Date")
  }

  orig_na <- sum(is.na(data[, idx, drop = FALSE]))

  diff_vals <- c(0, as.numeric(diff(tv), units = units))
  new_period <- (diff_vals > intervals) | (diff_vals == 0)
  starts <- which(new_period)
  lens <- diff(c(starts, nrow(data) + 1))

  mat <- to_numeric_matrix(data, idx)
  n_threads <- if (is.null(cores)) 0L else as.integer(cores)

  shorvalu_fill_cpp(mat, as.integer(starts - 1), as.integer(lens),
                    n_threads = n_threads)
  data[, idx] <- as.data.frame(mat)

  after_na <- sum(is.na(data[, idx, drop = FALSE]))

  if (verbose) {
    cat("Missing values left in selected variables:", after_na, "\n")
    cat(orig_na - after_na, "missing values are replaced by shorvalu interpolation\n")
    cat("Time used by shorvalu:", format(Sys.time() - t0, digits = 3), "\n")
  }
  data
}