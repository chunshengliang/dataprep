#' Remove highly correlated variables
#' @param data A data frame.
#' @param cols Columns to check.
#' @param cutoff Absolute correlation threshold.
#' @param method Correlation method.
#' @param keep Keep strategy: \code{"first"} or \code{"highest_var"}.
#' @param verbose Logical.
#' @return A data frame with redundant variables removed.
#' @export
#' @noRd
filter_high_cor <- function(data, cols = NULL, cutoff = 0.9,
                            method = "pearson", keep = "first",
                            verbose = FALSE) {
  t0 <- Sys.time()
  method <- match.arg(method, c("pearson", "spearman"))
  keep <- match.arg(keep, c("first", "highest_var"))

  if (is.vector(data) && !is.list(data)) {
    stop("filter_high_cor requires a data frame or matrix with multiple columns")
  }

  idx <- resolve_cols(data, cols)
  check_numeric_cols(data, idx)

  mat <- to_numeric_matrix(data, idx)
  p <- ncol(mat)
  if (p < 2) {
    if (verbose) cat("Only one column selected, nothing to filter.\n")
    return(data)
  }

  keep_logical <- filter_high_cor_cpp(mat, cutoff, keep == "first")
  keep_cols <- idx[keep_logical]

  if (verbose) {
    cat(length(idx) - length(keep_cols), "columns removed due to high correlation (cutoff =", cutoff, ")\n")
  }
  result <- data[, keep_cols, drop = FALSE]

  if (verbose) cat("Time used by filter_high_cor:", format(Sys.time() - t0, digits = 3), "\n")
  result
}