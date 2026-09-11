#' Clean and standardize character columns
#' @param data A data frame or character vector.
#' @param cols Columns to clean.
#' @param trim Trim whitespace.
#' @param tolower Convert to lowercase.
#' @param toupper Convert to uppercase.
#' @param pattern Regex pattern.
#' @param replacement Replacement string.
#' @param verbose Logical.
#' @return A data frame or character vector.
#' @export
#' @noRd
clean_strings <- function(data, cols = NULL,
                          trim = FALSE, tolower = FALSE, toupper = FALSE,
                          pattern = NULL, replacement = NULL,
                          verbose = FALSE) {
  t0 <- Sys.time()

  if (is.vector(data) && !is.list(data)) {
    x <- as.character(data)
    if (trim) x <- trimws(x)
    if (tolower) x <- tolower(x)
    if (toupper) x <- toupper(x)
    if (!is.null(pattern)) {
      if (is.null(replacement)) replacement <- ""
      x <- gsub(pattern, replacement, x)
    }
    if (verbose) cat("Time used by clean_strings:", format(Sys.time() - t0, digits = 3), "\n")
    return(x)
  }

  idx <- resolve_cols(data, cols)
  for (j in idx) {
    x <- as.character(data[[j]])
    if (trim) x <- trimws(x)
    if (tolower) x <- tolower(x)
    if (toupper) x <- toupper(x)
    if (!is.null(pattern)) {
      if (is.null(replacement)) replacement <- ""
      x <- gsub(pattern, replacement, x)
    }
    data[[j]] <- x
  }

  if (verbose) cat("Time used by clean_strings:", format(Sys.time() - t0, digits = 3), "\n")
  data
}