#' Delete observations with excessive consecutive missing values
#' @param data A data frame.
#' @param cols Columns to check.
#' @param group Optional grouping column.
#' @param by Time unit.
#' @param half Half window size.
#' @param date_col Time column.
#' @param cores Number of CPU cores.
#' @param verbose Logical.
#' @return A data frame with rows removed.
#' @export
#' @noRd
obsedele <- function(data, cols = NULL, group = NULL, by = "min", half = 30,
                     date_col = NULL, cores = NULL, verbose = FALSE) {
  t0 <- Sys.time()
  df <- data
  idx <- resolve_cols(data, cols)

  date_info <- resolve_date_col(data, date_col)
  date_name <- date_info$name

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
    group_int[is.na(group_int)] <- 0
  } else {
    group_int <- rep(0L, nrow(data))
  }

  mat_selected <- to_numeric_matrix(data, idx)

  if (is.null(cores)) {
    n_threads <- 0L
  } else {
    n_threads <- as.integer(cores)
  }

  keep <- obsedele_cpp(
    time_sec = time_sec,
    group_int = group_int,
    x = mat_selected,
    step_sec = step_sec,
    half = half,
    threshold_sec = threshold_sec,
    n_threads = n_threads
  )

  result <- data[keep, , drop = FALSE]
  rownames(result) <- NULL
  if (verbose) {
    cat(nrow(data) - nrow(result), 'observations are deleted\n')
    cat('Time used by obsedele:', format(Sys.time() - t0, digits = 3), '\n')
  }
  result
}