#' Random sampling with optional stratification
#' @param data A data frame.
#' @param group Grouping column.
#' @param size Sample size.
#' @param frac Sampling fraction.
#' @param replace Sample with replacement.
#' @param seed Random seed.
#' @param verbose Logical.
#' @return A sampled data frame.
#' @export
#' @noRd
sample_data <- function(data, group = NULL, size = NULL, frac = NULL,
                        replace = FALSE, seed = NULL, verbose = FALSE) {
  t0 <- Sys.time()
  if (is.null(group) && is.null(size) && is.null(frac)) {
    stop("At least one of group, size, or frac must be provided")
  }
  if (!is.null(size) && !is.null(frac)) {
    stop("Provide either size or frac, not both")
  }

  if (is.vector(data) && !is.list(data)) {
    stop("sample_data requires a data frame")
  }

  if (is.null(group)) {
    n <- nrow(data)
    if (!is.null(size)) {
      k <- min(size, n)
    } else if (!is.null(frac)) {
      k <- round(frac * n)
    }
    if (!replace) k <- min(k, n)
    if (!is.null(seed)) set.seed(seed)
    idx <- sample(n, k, replace = replace)
    result <- data[idx, , drop = FALSE]
    if (verbose) cat("Time used by sample_data:", format(Sys.time() - t0, digits = 3), "\n")
    return(result)
  }

  group_col <- if (is.character(group)) group else names(data)[group]
  group_vec <- data[[group_col]]

  if (!is.null(size)) {
    size_per_group <- size
  } else {
    sizes <- table(group_vec)
    size_per_group <- round(frac * sizes)
    size_per_group <- pmax(size_per_group, 1)
  }

  if (!is.null(seed)) set.seed(seed)
  idx_vec <- sample_data_cpp(as.integer(factor(group_vec)), size_per_group, replace, ifelse(is.null(seed), -1, seed))
  idx <- as.integer(idx_vec)
  result <- data[idx, , drop = FALSE]

  if (verbose) cat("Time used by sample_data:", format(Sys.time() - t0, digits = 3), "\n")
  result
}