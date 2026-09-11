resolve_cols <- function(data, cols) {
  if (is.null(cols)) {
    if (is.data.frame(data) || is.matrix(data)) return(seq_len(ncol(data)))
    else return(NULL)
  }
  if (is.character(cols)) {
    if (is.data.frame(data)) idx <- match(cols, names(data))
    else if (is.matrix(data)) idx <- match(cols, colnames(data))
    else stop("Character column names are only supported for data frames or matrices")
    if (any(is.na(idx))) stop("Some column names do not exist")
    return(idx)
  }
  if (is.numeric(cols)) {
    if (is.data.frame(data) || is.matrix(data)) {
      if (any(cols < 1 | cols > ncol(data))) stop("Column indices out of range")
      return(as.integer(cols))
    } else stop("Numeric column indices are only supported for data frames or matrices")
  }
  stop("cols must be NULL, a character vector, or a numeric vector")
}

resolve_date_col <- function(data, date_col = NULL) {
  if (!is.data.frame(data) && !is.matrix(data)) stop("Time column is only supported for data frames or matrices")
  col_names <- if (is.data.frame(data)) names(data) else colnames(data)
  if (is.null(date_col)) {
    exact <- which(col_names %in% c("date", "Date", "DATE"))
    if (length(exact) > 0) {
      idx <- exact[1]
    } else {
      approx <- which(grepl("date|Date|DATE", col_names))
      if (length(approx) == 0) stop("No time column found; please specify via date_col")
      idx <- approx[1]
    }
  } else {
    if (is.character(date_col)) {
      idx <- match(date_col, col_names)
      if (is.na(idx)) stop("Specified time column name does not exist")
    } else if (is.numeric(date_col)) {
      if (date_col < 1 || date_col > length(col_names)) stop("Time column index out of range")
      idx <- as.integer(date_col)
    } else stop("date_col must be character or numeric")
  }
  list(idx = idx, name = col_names[idx])
}

check_numeric_cols <- function(data, cols) {
  bad <- character()
  for (j in cols) {
    x <- data[[j]]
    if (is.numeric(x)) next
    if (is.logical(x) && all(is.na(x))) next
    bad <- c(bad, names(data)[j])
  }
  if (length(bad) > 0) {
    stop("The following columns are not numeric: ", paste(bad, collapse = ", "))
  }
  invisible(TRUE)
}

to_numeric_matrix <- function(data, cols) {
  check_numeric_cols(data, cols)
  mat <- as.matrix(data[, cols, drop = FALSE])
  storage.mode(mat) <- "double"
  mat
}

getmode <- function(x) {
  keys <- unique(x)
  keys[which.max(tabulate(match(x, keys)))]
}
