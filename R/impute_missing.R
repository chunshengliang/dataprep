#' Impute missing values
#' @param data A data frame or matrix.
#' @param cols Columns to impute.
#' @param method \code{"linear"}, \code{"locf"}, \code{"nocb"}, \code{"mean"}, or \code{"median"}.
#' @param group Optional grouping column.
#' @param date_col Time column.
#' @param max_gap Not implemented.
#' @param verbose Logical.
#' @return A data frame with imputed values.
#' @export
#' @noRd
impute_missing <- function(data, cols = NULL, method = "linear",
                           group = NULL, date_col = NULL, max_gap = NULL,
                           verbose = FALSE) {
  t0 <- Sys.time()
  method <- match.arg(method, c("linear", "locf", "nocb", "mean", "median"))

  if (is.vector(data) && !is.list(data)) {
    return(impute_cpp(data, method))
  }

  if (is.matrix(data)) {
    data <- as.data.frame(data)
    if (is.null(cols)) cols <- seq_len(ncol(data))
  }

  idx <- resolve_cols(data, cols)
  check_numeric_cols(data, idx)
  mat <- to_numeric_matrix(data, idx)

  if (is.null(group)) {
    imputed_mat <- impute_matrix_cpp(mat, method)
  } else {
    group_col <- if (is.character(group)) group else names(data)[group]
    ug <- unique(data[[group_col]])
    imputed_mat <- mat
    for (g in ug) {
      rows <- which(data[[group_col]] == g)
      if (length(rows) > 0) {
        imputed_mat[rows, ] <- impute_matrix_cpp(mat[rows, , drop = FALSE], method)
      }
    }
  }
  data[, idx] <- imputed_mat
  if (verbose) cat("Missing value imputation completed.\n")
  data
}