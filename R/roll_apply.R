#' Rolling window statistics
#' @param data A data frame, matrix, or numeric vector.
#' @param cols Columns to process.
#' @param window Window size.
#' @param method \code{"mean"}, \code{"sd"}, \code{"var"}, \code{"median"}, \code{"sum"}, \code{"min"}, \code{"max"}.
#' @param align \code{"right"}, \code{"left"}, or \code{"center"}.
#' @param group Optional grouping column.
#' @param date_col Time column.
#' @param verbose Logical.
#' @return A data frame or vector.
#' @export
#' @noRd
roll_apply <- function(data, cols = NULL, window = 3, method = "mean",
                       align = "right", group = NULL, date_col = NULL,
                       verbose = FALSE) {
  t0 <- Sys.time()
  method <- match.arg(method, c("mean", "sd", "var", "median", "sum", "min", "max"))
  align <- match.arg(align, c("right", "left", "center"))

  apply_align <- function(x) {
    if (align == "left") {
      c(x[-1], NA)
    } else if (align == "center") {
      half <- floor(window / 2)
      c(x[(half+1):length(x)], rep(NA, half))
    } else {
      x
    }
  }

  if (is.vector(data) && !is.list(data)) {
    x <- as.numeric(data)
    res <- roll_stats_cpp(x, window, method)
    res <- apply_align(res)
    names(res) <- names(data)
    return(res)
  }

  if (is.matrix(data)) {
    data <- as.data.frame(data)
    if (is.null(cols)) cols <- seq_len(ncol(data))
  }

  idx <- resolve_cols(data, cols)
  check_numeric_cols(data, idx)

  if (is.null(group)) {
    for (j in idx) {
      data[[j]] <- apply_align(roll_stats_cpp(data[[j]], window, method))
    }
  } else {
    group_col <- if (is.character(group)) group else names(data)[group]
    ug <- unique(data[[group_col]])
    for (g in ug) {
      rows <- which(data[[group_col]] == g)
      for (j in idx) {
        data[rows, j] <- apply_align(roll_stats_cpp(data[rows, j], window, method))
      }
    }
  }

  if (verbose) cat("Rolling statistics applied.\n")
  data
}