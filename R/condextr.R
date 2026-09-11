#' Conditional extremum outlier removal
#' @param data A data frame, matrix, or numeric vector.
#' @param cols Columns to process.
#' @param group Grouping column.
#' @param top,top.error,top.magnitude Top threshold parameters.
#' @param bottom,bottom.error,bottom.magnitude Bottom threshold parameters.
#' @param interval Number of marking steps between observation deletions.
#' @param by,half Time unit and half-window for observation deletion.
#' @param times Number of observation deletion rounds.
#' @param date_col Time column.
#' @param cores Number of CPU cores.
#' @param verbose Logical.
#' @return Data with outliers removed.
#' @export
#' @noRd
condextr <- function(data, cols = NULL, group = NULL, top = .995,
                     top.error = .1, top.magnitude = .2,
                     bottom = .0025, bottom.error = .2, bottom.magnitude = .4,
                     interval = 10, by = 'min', half = 30, times = 10,
                     date_col = NULL, cores = NULL, verbose = FALSE) {
  t0 <- Sys.time()

  if (is.vector(data) && !is.list(data)) {
    for (j in 1:times) {
      for (i in 1:interval) {
        data <- mark_outliers_cpp(data, top, top.error, top.magnitude,
                                  bottom, bottom.error, bottom.magnitude, TRUE)
      }
      data <- obsedele(data, by = by, half = half, date_col = date_col,
                       cores = cores, verbose = verbose)
    }
    return(data)
  }

  date_info <- resolve_date_col(data, date_col)
  date_name <- date_info$name
  idx <- resolve_cols(data, cols)

  group_idx <- NULL
  if (!is.null(group)) {
    group_idx <- if (is.character(group)) which(names(data) == group) else as.integer(group)
    if (group_idx %in% idx) {
      stop("group column should not be within cols")
    }
  }

  time_vec <- data[[date_name]]
  if (inherits(time_vec, "POSIXct")) {
    time_sec <- as.numeric(time_vec)
  } else if (inherits(time_vec, "Date")) {
    time_sec <- as.numeric(time_vec) * 86400
  } else {
    stop("Time column must be POSIXct or Date")
  }

  num <- ifelse(grepl("^[A-Za-z]+$", by), 1, as.numeric(gsub(".*?([0-9]+).*", "\\1", by)))
  unit_char <- gsub(".*?([a-z]+).*", "\\1", by)
  unit_sec <- switch(unit_char,
                     "secs" = 1, "sec" = 1,
                     "mins" = 60, "min" = 60,
                     "hours" = 3600, "hour" = 3600,
                     "days" = 86400, "day" = 86400,
                     "weeks" = 604800, "week" = 604800,
                     stop("Unsupported time unit"))
  step_sec <- num * unit_sec
  threshold_sec <- half * num * unit_sec

  if (!is.null(group_idx)) {
    group_vec <- data[[group_idx]]
    group_int <- as.integer(factor(group_vec))
    group_int[is.na(group_int)] <- 0L
  } else {
    group_int <- rep(0L, nrow(data))
  }

  mat <- to_numeric_matrix(data, idx)
  n <- nrow(mat)
  p <- ncol(mat)
  orig_idx <- seq_len(n)

  n_threads <- if (is.null(cores)) 0L else as.integer(cores)

  mark_matrix <- function(m, grp) {
    if (all(grp == 0)) {
      for (j in 1:p) {
        m[, j] <- mark_outliers_cpp(m[, j], top, top.error, top.magnitude,
                                    bottom, bottom.error, bottom.magnitude, TRUE)
      }
    } else {
      ug <- unique(grp)
      for (g in ug) {
        rows <- which(grp == g)
        sub <- m[rows, , drop = FALSE]
        for (j in 1:p) {
          sub[, j] <- mark_outliers_cpp(sub[, j], top, top.error, top.magnitude,
                                        bottom, bottom.error, bottom.magnitude, TRUE)
        }
        m[rows, ] <- sub
      }
    }
    m
  }

  total_deleted <- 0

  for (round in 1:times) {
    for (rep in 1:interval) {
      mat <- mark_matrix(mat, group_int)
    }
    old_n <- n
    keep <- obsedele_cpp(time_sec, group_int, mat,
                         step_sec, half, threshold_sec, n_threads)
    mat <- mat[keep, , drop = FALSE]
    time_sec <- time_sec[keep]
    group_int <- group_int[keep]
    orig_idx <- orig_idx[keep]
    n <- nrow(mat)
    total_deleted <- total_deleted + (old_n - n)
    if (n == 0) break
  }

  if (n > 0) {
    old_n <- n
    keep <- obsedele_cpp(time_sec, group_int, mat,
                         step_sec, half, threshold_sec, n_threads)
    mat <- mat[keep, , drop = FALSE]
    time_sec <- time_sec[keep]
    group_int <- group_int[keep]
    orig_idx <- orig_idx[keep]
    n <- nrow(mat)
    total_deleted <- total_deleted + (old_n - n)
  }

  final_keep <- rep(FALSE, nrow(data))
  final_keep[orig_idx] <- TRUE
  result <- data[final_keep, , drop = FALSE]
  result[, idx] <- as.data.frame(mat)

  rownames(result) <- NULL

  if (inherits(data, "grouped_df")) {
    group_vars <- dplyr::group_vars(data)
    if (length(group_vars) > 0) {
      result <- dplyr::group_by(result, dplyr::across(dplyr::all_of(group_vars)))
    } else {
      result <- dplyr::ungroup(result)
    }
  }

  kept_indices <- which(final_keep)
  orig_subset <- data[kept_indices, idx, drop = FALSE]
  final_subset <- result[, idx, drop = FALSE]
  outliers_marked <- sum(is.na(final_subset) & !is.na(orig_subset))

  if (verbose) {
    cat(outliers_marked,
        "values are regarded as outliers and deleted excluding those in deleted observations\n")
    cat(total_deleted,
        "observations are deleted in total by condextr (after", interval * times, "cycles)\n")
    cat("Time used by condextr:", format(Sys.time() - t0, digits = 3), "\n")
  }

  result
}