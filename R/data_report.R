#' Generate a data quality report
#' @param data A data frame.
#' @param cols Columns to include.
#' @param date_col Time column.
#' @param verbose Logical.
#' @return Invisibly, a list with dimensions, types, missing diagnosis, and descriptive statistics.
#' @export
#' @noRd
data_report <- function(data, cols = NULL, date_col = NULL, verbose = FALSE) {
  t0 <- Sys.time()
  if (verbose) {
    cat("========== Data Quality Report ==========\n")
    cat("Dimensions:", nrow(data), "rows x", ncol(data), "columns\n\n")
  }
  types <- sapply(data, function(x) class(x)[1])
  if (verbose) {
    cat("Variable type distribution:\n")
    print(table(types))
    cat("\n")
  }
  if (is.null(cols)) {
    numeric_cols <- which(sapply(data, is.numeric))
  } else {
    numeric_cols <- resolve_cols(data, cols)
  }
  if (length(numeric_cols) > 0 && verbose) {
    cat("Missing value diagnosis (numeric columns):\n")
    print(na_diagnose(data, cols = numeric_cols, date_col = date_col, verbose = FALSE))
    cat("\n")
    cat("Descriptive statistics (numeric columns):\n")
    print(descdata(data, cols = numeric_cols, verbose = FALSE))
    cat("\n")
  }
  if (verbose) cat("Time used by data_report:", format(Sys.time() - t0, digits = 3), "\n")
  invisible(list(
    dim = c(nrow(data), ncol(data)),
    types = types,
    na_diagnose = if (length(numeric_cols) > 0) na_diagnose(data, cols = numeric_cols, date_col = date_col, verbose = FALSE) else NULL,
    desc_stats = if (length(numeric_cols) > 0) descdata(data, cols = numeric_cols, verbose = FALSE) else NULL
  ))
}