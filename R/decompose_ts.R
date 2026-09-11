#' Simple time series decomposition
#' @param data A data frame with a time column.
#' @param cols Columns to decompose.
#' @param date_col Time column.
#' @param period \code{"month"}, \code{"day"}, or \code{"hour"}.
#' @param method \code{"additive"} or \code{"multiplicative"}.
#' @param verbose Logical.
#' @return A data frame with residual component.
#' @export
#' @noRd
decompose_ts <- function(data, cols = NULL, date_col = NULL,
                         period = "month", method = "additive",
                         verbose = FALSE) {
  t0 <- Sys.time()
  method <- match.arg(method, c("additive", "multiplicative"))

  if (is.vector(data) && !is.list(data)) {
    x <- as.numeric(data)
    n <- length(x)
    freq <- switch(period,
                   month = 12,
                   day = 365,
                   hour = 24,
                   stop("period must be 'month', 'day', or 'hour'"))
    cycle_id <- ((seq_len(n) - 1) %% freq) + 1
    trend <- roll_stats_cpp(x, freq, "mean")
    seasonal <- tapply(x, cycle_id, mean, na.rm = TRUE)
    seasonal_vec <- seasonal[as.character(cycle_id)]
    if (method == "additive") {
      residual <- x - trend - seasonal_vec
    } else {
      residual <- x / (trend * seasonal_vec)
    }
    names(residual) <- names(data)
    if (verbose) cat("Decomposition completed.\n")
    return(residual)
  }

  if (is.matrix(data)) {
    data <- as.data.frame(data)
    if (is.null(cols)) cols <- seq_len(ncol(data))
  }

  idx <- resolve_cols(data, cols)
  check_numeric_cols(data, idx)

  date_info <- resolve_date_col(data, date_col)
  date_name <- date_info$name
  time_vec <- data[[date_name]]
  if (!inherits(time_vec, c("POSIXct", "Date"))) {
    stop("Time column must be POSIXct or Date")
  }

  if (period == "month") {
    cycle_id <- as.integer(format(as.POSIXlt(time_vec), "%m"))
    freq <- 12
  } else if (period == "day") {
    cycle_id <- as.integer(format(as.POSIXlt(time_vec), "%j"))
    freq <- 365
  } else if (period == "hour") {
    cycle_id <- as.integer(format(as.POSIXlt(time_vec), "%H"))
    freq <- 24
  } else {
    stop("period must be 'month', 'day', or 'hour'")
  }

  for (j in idx) {
    x <- data[[j]]
    trend <- roll_stats_cpp(x, freq, "mean")
    seasonal <- tapply(x, cycle_id, mean, na.rm = TRUE)
    seasonal_vec <- seasonal[as.character(cycle_id)]
    if (method == "additive") {
      residual <- x - trend - seasonal_vec
    } else {
      residual <- x / (trend * seasonal_vec)
    }
    data[[j]] <- residual
  }

  if (verbose) cat("Decomposition completed.\n")
  data
}