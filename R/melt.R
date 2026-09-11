#' Melt a data.frame (wide to long)
#'
#' Fast reshape using a SIMD + OpenMP C++ backend (`melt_cpp`).
#' All input sizes are routed to the C++ backend. A pure-R "small-input
#' fast path" was measured to be 5-7x slower than C++ for tables below
#' 1e5 rows, because R's `t()` and manual data.frame assembly are not
#' cache-friendly.
#'
#' @param data               data.frame
#' @param id                 id columns (character names, integer indices,
#'                           logical mask, or NULL). Primary argument.
#' @param measure.vars       measure columns (character, integer, logical,
#'                           or NULL).
#' @param variable.name      name of the output "variable" column.
#' @param value.name         name of the output "value" column.
#' @param na.rm              drop rows where value is NA.
#' @param cores              number of OpenMP threads; NULL = auto.
#' @param major              "row" (tidyr-style) or "col" (reshape2-style).
#' @param verbose            print timing/messages.
#' @param parallel_threshold minimum output size (elements) before auto
#'                           parallelism is enabled.
#' @param id.vars            alias of `id` (reshape2 / data.table compat).
#'
#' @return data.frame in long format.
#' @export
melt <- function(data,
                 id                  = NULL,
                 measure.vars        = NULL,
                 variable.name       = "variable",
                 value.name          = "value",
                 na.rm               = FALSE,
                 cores               = NULL,
                 major               = c("row", "col"),
                 verbose             = FALSE,
                 parallel_threshold  = 5e6,
                 id.vars             = NULL) {

  # ---- basic checks ----
  if (!is.data.frame(data)) stop("melt is only supported for data frames")
  if (ncol(data) < 1L)      stop("data has no columns")
  if (nrow(data) == 0L)     stop("data has no rows")

  major <- match.arg(major)

  # ---- alias: id / id.vars ----
  if (!is.null(id) && !is.null(id.vars))
    stop("Specify only one of `id` or `id.vars` (they are aliases).")
  if (is.null(id)) id <- id.vars
  if (!is.null(id) && !is.null(measure.vars))
    stop("Please specify only one of id (id.vars) or measure.vars")

  ncols  <- ncol(data)
  cnames <- names(data)

  id_arg         <- NULL
  n_measure_cols <- NULL

  # ---- resolve id / measure.vars for the C++ backend ----
  #   Convention:
  #     NULL              -> C++ infers (numeric columns as measures)
  #     positive ints     -> id columns
  #     negative ints     -> measure columns (C++ takes complement as id)
  #     character         -> id names
  if (!is.null(id)) {
    if (is.character(id)) {
      bad <- !(id %in% cnames)
      if (any(bad)) stop(sprintf("id column(s) not found: %s",
                                 paste(id[bad], collapse = ", ")))
      id_arg <- match(id, cnames)
    } else if (is.numeric(id)) {
      id_arg <- as.integer(id)
      if (any(id_arg < 1L | id_arg > ncols))
        stop("id contains invalid column indices")
    } else if (is.logical(id)) {
      if (length(id) != ncols)
        stop("logical id must have length ncol(data)")
      id_arg <- which(id)
    } else stop("id must be character, numeric, or logical")

    n_measure_cols <- ncols - length(unique(id_arg))
  } else if (!is.null(measure.vars)) {
    if (is.character(measure.vars)) {
      bad <- !(measure.vars %in% cnames)
      if (any(bad)) stop(sprintf("measure.vars not found: %s",
                                 paste(measure.vars[bad], collapse = ", ")))
      midx <- match(measure.vars, cnames)
    } else if (is.numeric(measure.vars)) {
      midx <- as.integer(measure.vars)
    } else if (is.logical(measure.vars)) {
      if (length(measure.vars) != ncols)
        stop("logical measure.vars must have length ncol(data)")
      midx <- which(measure.vars)
    } else stop("measure.vars must be character, numeric, or logical")

    if (anyNA(midx) || any(midx < 1L | midx > ncols))
      stop("measure.vars contains invalid column indices")

    id_arg         <- -unique(midx)
    n_measure_cols <- length(unique(midx))
  } else {
    id_arg         <- NULL
    n_measure_cols <- max(1L, ncols - 1L)
  }

  total_elements <- as.double(nrow(data)) * as.double(n_measure_cols)

  # ---- thread count: explicit > global option > automatic ----
  MAX_THREADS_CAP <- 64L

  if (is.null(cores)) {
    opt <- getOption("dataprep.cores", NULL)
    if (!is.null(opt)) {
      cores <- as.integer(opt)
    } else if (total_elements < parallel_threshold) {
      cores <- 1L
    } else {
      cores <- as.integer(floor(total_elements / 2e6))
    }
  }
  cores <- as.integer(cores)
  if (is.na(cores) || cores < 1L) cores <- 1L
  if (cores > MAX_THREADS_CAP)    cores <- MAX_THREADS_CAP

  if (verbose) {
    cat(sprintf("[melt] rows=%d  cols=%d  measure_cols=%d  major=%s  threads=%d\n",
                nrow(data), ncols, n_measure_cols, major, cores))
  }

  # ---- call the C++ backend ----
  result <- melt_cpp(
    df            = data,
    id            = id_arg,
    variable_name = variable.name,
    value_name    = value.name,
    major         = major,
    n_threads     = cores
  )

  # ---- na.rm ----
  if (isTRUE(na.rm)) {
    vcol <- result[[length(result)]]
    if (is.factor(vcol)) vcol <- as.character(vcol)
    keep <- !is.na(vcol)
    if (any(!keep)) {
      result <- result[keep, , drop = FALSE]
      rownames(result) <- NULL
    }
  }

  result
}