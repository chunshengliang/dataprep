#' Remove linear trend from time series
#' @param data A data frame, matrix, or numeric vector.
#' @param cols Columns to detrend.
#' @param date_col Time column.
#' @param verbose Logical.
#' @return A data frame or vector with trend removed.
#' @export
#' @noRd
detrend_ts <- function(data, cols = NULL, date_col = NULL, verbose = FALSE) {
  t0 <- Sys.time()

  if (is.vector(data) && !is.list(data)) {
    x <- as.numeric(data)
    n <- length(x)
    t_seq <- seq_len(n)
    sum_t <- sum(t_seq)
    sum_t2 <- sum(t_seq^2)
    sum_y <- sum(x, na.rm = TRUE)
    sum_ty <- sum(t_seq * x, na.rm = TRUE)
    slope <- (n * sum_ty - sum_t * sum_y) / (n * sum_t2 - sum_t^2)
    intercept <- (sum_y - slope * sum_t) / n
    res <- x - (intercept + slope * t_seq)
    names(res) <- names(data)
    if (verbose) cat("Linear trend removed.\n")
    return(res)
  }

  if (is.matrix(data)) {
    data <- as.data.frame(data)
    if (is.null(cols)) cols <- seq_len(ncol(data))
  }

  idx <- resolve_cols(data, cols)
  check_numeric_cols(data, idx)

  n <- nrow(data)
  t_seq <- seq_len(n)
  sum_t <- sum(t_seq)
  sum_t2 <- sum(t_seq^2)

  for (j in idx) {
    x <- data[[j]]
    valid <- !is.na(x)
    if (sum(valid) < 2) next
    sum_y <- sum(x, na.rm = TRUE)
    sum_ty <- sum(t_seq * x, na.rm = TRUE)
    slope <- (n * sum_ty - sum_t * sum_y) / (n * sum_t2 - sum_t^2)
    intercept <- (sum_y - slope * sum_t) / n
    data[[j]] <- x - (intercept + slope * t_seq)
  }

  if (verbose) cat("Linear trend removed.\n")
  data
}