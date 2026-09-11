#' Apply a preprocessing plan to new data
#' @param plan A \code{prep_plan} from \code{\link{prep_fit}}.
#' @param newdata A data frame.
#' @param verbose Logical.
#' @return A data frame with the plan applied.
#' @export
#' @noRd
prep_transform <- function(plan, newdata, verbose = FALSE) {
  t0 <- Sys.time()
  if (!is.data.frame(newdata)) stop("newdata must be a data frame")
  data <- newdata

  original_names <- plan$data_info$original_names
  original_cols <- plan$data_info$col_idx

  if (any(!(original_names[original_cols] %in% names(data)))) {
    stop("newdata is missing some required columns")
  }

  cols <- match(original_names[original_cols], names(data))

  for (step in plan$steps) {
    switch(step,
           "varidele" = {
             keep_flag <- plan$params$varidele_keep
             cols <- cols[keep_flag]
             if (length(cols) == 0) stop("No selected variables remain after varidele")
           },
           "obsedele" = {
             data <- obsedele(data, cols = cols,
                              group = plan$data_info$group,
                              by = plan$params$obsedele_by,
                              half = plan$params$obsedele_half,
                              date_col = plan$data_info$date_col,
                              cores = NULL, verbose = verbose)
           },
           "outlier" = {
             method_outlier <- plan$params$outlier_method
             group <- plan$params$outlier_group
             thresholds <- plan$params$outlier_thresholds
             if (is.null(group)) {
               for (j in cols) {
                 nm <- names(data)[j]
                 th <- thresholds[[as.character(j)]]
                 if (is.null(th)) next
                 x <- data[[j]]
                 if (method_outlier == "percentile") {
                   data[[j]][x > th$top | x < th$bottom] <- NA_real_
                 } else if (method_outlier == "iqr") {
                   data[[j]][x < th$lower | x > th$upper] <- NA_real_
                 } else if (method_outlier == "mad") {
                   z <- 0.6745 * (x - th$median) / th$mad
                   data[[j]][abs(z) > th$coef] <- NA_real_
                 }
               }
             } else {
               group_col <- if (is.character(group)) group else names(data)[group]
               ug <- unique(data[[group_col]])
               for (g in ug) {
                 rows <- which(data[[group_col]] == g)
                 for (j in cols) {
                   nm <- names(data)[j]
                   key <- paste0(g, "_", nm)
                   th <- thresholds[[key]]
                   if (is.null(th)) next
                   x <- data[rows, j]
                   if (method_outlier == "percentile") {
                     data[rows, j][x > th$top | x < th$bottom] <- NA_real_
                   } else if (method_outlier == "iqr") {
                     data[rows, j][x < th$lower | x > th$upper] <- NA_real_
                   } else if (method_outlier == "mad") {
                     z <- 0.6745 * (x - th$median) / th$mad
                     data[rows, j][abs(z) > th$coef] <- NA_real_
                   }
                 }
               }
             }
           },
           "impute" = {
             data <- impute_missing(data, cols = cols,
                                    method = plan$params$impute_method,
                                    group = plan$params$impute_group,
                                    verbose = verbose)
           },
           "scale" = {
             scale_method <- plan$params$scale_method
             for (j in cols) {
               nm <- names(data)[j]
               center <- plan$params$scale_center[[nm]]
               scale_val <- plan$params$scale_scale[[nm]]
               if (is.na(center) || is.na(scale_val)) next
               data[[j]] <- (data[[j]] - center) / scale_val
             }
           },
           stop(paste("Unknown step:", step))
    )
  }

  if (verbose) cat("Preprocessing plan applied.\n")
  data
}