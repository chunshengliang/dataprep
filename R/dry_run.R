#' Simulate preprocessing and report changes
#' @param data A data frame.
#' @param steps Steps to simulate.
#' @param cols Columns to include.
#' @param group Grouping column.
#' @param date_col Time column.
#' @param fraction Missing fraction threshold.
#' @param top,bottom Percentile thresholds.
#' @param by,half Time parameters.
#' @param method_outlier Outlier method.
#' @param coef Coefficient.
#' @param verbose Logical.
#' @return A list with simulation report.
#' @export
#' @noRd
dry_run <- function(data, steps = c("varidele", "obsedele", "outlier"),
                    cols = NULL, group = NULL, date_col = NULL,
                    fraction = 0.25, top = 0.995, bottom = 0.0025,
                    by = "min", half = 30, method_outlier = "iqr", coef = 1.5,
                    verbose = FALSE) {
  t0 <- Sys.time()
  if (!is.data.frame(data)) stop("data must be a data frame")
  if (is.null(cols)) cols <- which(sapply(data, is.numeric))
  else cols <- resolve_cols(data, cols)

  report <- list()
  original_n <- nrow(data)
  original_cols <- ncol(data)

  for (step in steps) {
    switch(step,
           "varidele" = {
             mat <- as.matrix(data[, cols, drop = FALSE])
             frac <- colMeans(is.na(mat))
             removed_cols <- names(data)[cols][frac >= fraction]
             report$varidele <- list(
               removed_columns = removed_cols,
               removed_count = length(removed_cols)
             )
             keep_cols <- cols[frac < fraction]
             data <- data[, keep_cols, drop = FALSE]
             cols <- seq_along(keep_cols)
           },
           "obsedele" = {
             n_before <- nrow(data)
             report$obsedele <- list(
               rows_before = n_before,
               note = "Observation deletion would be applied"
             )
           },
           "outlier" = {
             report$outlier <- list(
               note = "Outlier detection would be applied"
             )
           }
    )
  }

  report$original_n <- original_n
  report$original_ncol <- original_cols
  report$final_n <- nrow(data)
  report$final_ncol <- ncol(data)

  if (verbose) cat("Dry run completed.\n")
  report
}