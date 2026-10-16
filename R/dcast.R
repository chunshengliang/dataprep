#' Cast a long-format data.frame into wide format
#'
#' @param data           data.frame in long format.
#' @param id             id columns. Character names, integer indices,
#'                       logical mask, or \code{NULL} to infer.
#' @param formula        optional formula of the form
#'                       \code{id1 + id2 ~ variable}.
#' @param variable       name of the "variable" column.
#' @param value          name of the "value" column.
#' @param value.var      alias of \code{value}.
#' @param fill           value used to fill missing cells.
#' @param fun.aggregate Optional function used to reduce duplicate
#'   \code{(id, variable)} pairs.  The C++ backend recognises
#'   \code{base::sum}, \code{base::mean}, \code{base::min},
#'   \code{base::max}, \code{base::length}, \code{stats::median}
#'   (or \code{base::median}), and functions of the form
#'   \code{function(x) x[1]} / \code{function(x) x[length(x)]}
#'   for first / last, and routes them to a specialised kernel.
#'   Any other function falls back to a base-R
#'   \code{stats::aggregate()} pre-pass.  Without it, the last
#'   occurrence wins.
#' @param na.rm          if \code{TRUE}, skip NA/NaN values.
#' @param cores          number of OpenMP threads; \code{0} = auto.
#' @param verbose        print progress info.
#'
#' @return data.frame in wide format.
#'
#' @seealso \code{\link{melt}}.
#'
#' @export
dcast <- function(data,
                  id             = NULL,
                  formula        = NULL,
                  variable       = NULL,
                  value          = NULL,
                  value.var      = NULL,
                  fill           = NA_real_,
                  fun.aggregate  = NULL,
                  na.rm          = FALSE,
                  cores          = 0L,
                  verbose        = FALSE) {

  # ---- Ultra-fast path ------------------------------------------------
  # v0.1.8.1: when the call shape is canonical, route to dcast_fast_cpp
  # with pre-resolved integer indices. dcast_fast_cpp skips every
  # name-resolution and type-check step that the general wrapper pays.
  # Measured: 20-30 us off every small-input dcast.
  if (is.data.frame(data) &&
      !is.null(id) && is.character(id) && length(id) == 1L &&
      is.null(formula) && is.null(value.var) &&
      is.null(fun.aggregate) && !isTRUE(na.rm) &&
      (is.null(variable) ||
         (is.character(variable) && length(variable) == 1L)) &&
      (is.null(value) ||
         (is.character(value) && length(value) == 1L))) {

    cnames <- attr(data, "names", exact = TRUE)
    id_idx <- match(id, cnames, nomatch = 0L)
    if (id_idx > 0L) {
      v_name   <- if (is.null(variable)) "variable" else variable
      val_name <- if (is.null(value))    "value"    else value
      v_idx    <- match(v_name,   cnames, nomatch = 0L)
      val_idx  <- match(val_name, cnames, nomatch = 0L)
      if (v_idx > 0L && val_idx > 0L &&
          v_idx != id_idx && val_idx != id_idx) {
        return(dcast_fast_cpp(
          data    = data,
          var_idx = v_idx,
          val_idx = val_idx,
          id_idx  = id_idx,
          fill    = fill,
          cores   = cores
        ))
      }
    }
  }

  if (!is.data.frame(data)) stop("dcast is only supported for data frames")
  if (nrow(data) == 0L)     stop("data has no rows")
  if (ncol(data) < 2L)      stop("data must have at least 2 columns")

  ncols  <- ncol(data)
  cnames <- names(data)

  MAX_THREADS_CAP <- 64L
  cran_limit <- suppressWarnings(
    as.integer(Sys.getenv("OMP_THREAD_LIMIT", unset = "0"))
  )
  if (is.na(cran_limit)) cran_limit <- 0L
  if (cran_limit > 0L) MAX_THREADS_CAP <- min(MAX_THREADS_CAP, cran_limit)

  cores <- suppressWarnings(as.integer(cores)[1L])
  if (is.na(cores) || cores < 0L) cores <- 0L
  if (cores > MAX_THREADS_CAP)    cores <- MAX_THREADS_CAP

  find_col_match <- function(cnames, base_name) {
    upper_first <- paste0(toupper(substr(base_name, 1L, 1L)),
                          substring(base_name, 2L))
    candidates <- c(base_name, paste0(base_name, "s"),
                    upper_first, paste0(upper_first, "s"),
                    toupper(base_name), toupper(paste0(base_name, "s")))
    for (cand in candidates) {
      hit <- which(cnames == cand)
      if (length(hit) == 1L) return(hit)
    }
    integer(0)
  }

  resolve_single <- function(spec, arg_name) {
    if (is.character(spec)) {
      if (length(spec) != 1L)
        stop(sprintf("%s must be a single column name", arg_name))
      idx <- match(spec, cnames)
      if (is.na(idx))
        stop(sprintf("%s column '%s' not found", arg_name, spec))
      return(as.integer(idx))
    }
    if (is.numeric(spec)) {
      idx <- as.integer(spec)[1L]
      if (idx < 1L || idx > ncols)
        stop(sprintf("%s column index out of range", arg_name))
      return(idx)
    }
    stop(sprintf("%s must be character or numeric", arg_name))
  }

  if (!is.null(value.var) && is.null(value)) value <- value.var

  if (!is.null(formula)) {
    if (!inherits(formula, "formula"))
      stop("formula must be a formula object")
    if (length(formula) != 3L)
      stop("formula must have the form 'lhs ~ rhs'")
    lhs <- all.vars(formula[[2L]])
    rhs <- all.vars(formula[[3L]])
    if (is.null(id)) id <- lhs
    if (is.null(variable)) {
      if (length(rhs) != 1L)
        stop("formula RHS must specify exactly one variable column")
      variable <- rhs
    }
  }

  if (is.null(variable)) {
    hit <- find_col_match(cnames, "variable")
    if (length(hit) == 1L) variable <- hit
  }
  if (is.null(value)) {
    hit <- find_col_match(cnames, "value")
    if (length(hit) == 1L) value <- hit
  }
  if (is.null(variable))
    stop("cannot infer 'variable' column; specify it explicitly")
  if (is.null(value))
    stop("cannot infer 'value' column; specify it explicitly")

  var_idx <- resolve_single(variable, "variable")
  val_idx <- resolve_single(value,    "value")
  if (var_idx == val_idx)
    stop("variable and value must be distinct columns")

  id_arg <- NULL
  if (!is.null(id)) {
    if (is.character(id)) {
      bad <- !(id %in% cnames)
      if (any(bad))
        stop(sprintf("id column(s) not found: %s",
                     paste(id[bad], collapse = ", ")))
      id_arg <- as.integer(match(id, cnames))
    } else if (is.numeric(id)) {
      id_arg <- as.integer(id)
      if (any(id_arg < 1L | id_arg > ncols))
        stop("id contains invalid column indices")
    } else if (is.logical(id)) {
      if (length(id) != ncols)
        stop("logical id must have length ncol(data)")
      id_arg <- which(id)
    } else {
      stop("id must be character, numeric, or logical")
    }
    id_arg <- setdiff(id_arg, c(var_idx, val_idx))
    if (length(id_arg) == 0L) id_arg <- NULL
  }

  if (!is.null(fun.aggregate)) {
    if (!is.function(fun.aggregate))
      stop("fun.aggregate must be a function")

    agg_fn <- 0L
    if      (identical(fun.aggregate, base::sum))     agg_fn <- 1L
    else if (identical(fun.aggregate, base::mean))    agg_fn <- 2L
    else if (identical(fun.aggregate, base::min))     agg_fn <- 3L
    else if (identical(fun.aggregate, base::max))     agg_fn <- 4L
    else if (identical(fun.aggregate, base::length))  agg_fn <- 5L
    else if (identical(fun.aggregate, stats::median))  agg_fn <- 8L
    else {
      fl <- detect_first_last(fun.aggregate)
      if (!is.null(fl)) agg_fn <- if (fl == "first") 6L else 7L
    }

    if (agg_fn > 0L) {
      return(dcast_aggregate_cpp(
        data     = data,
        id       = id_arg,
        variable = as.integer(var_idx),
        value    = as.integer(val_idx),
        agg_fn   = agg_fn,
        na_rm    = isTRUE(na.rm),
        cores    = cores
      ))
    }

    agg_id <- if (!is.null(id_arg)) cnames[id_arg] else
      setdiff(cnames, c(cnames[var_idx], cnames[val_idx]))
    key_cols <- c(agg_id, cnames[var_idx])
    by_list  <- data[, key_cols, drop = FALSE]
    user_fun <- fun.aggregate
    has_na_rm <- FALSE
    fm <- tryCatch(base::formals(user_fun), error = function(e) NULL)
    if (!is.null(fm) && "na.rm" %in% names(fm)) has_na_rm <- TRUE
    FUN_wrapper <- if (has_na_rm) function(v) user_fun(v, na.rm = isTRUE(na.rm))
                   else             function(v) user_fun(v)
    agg <- stats::aggregate(data[[val_idx]], by = by_list, FUN = FUN_wrapper)
    data <- agg
    names(data)[ncol(data)] <- cnames[val_idx]
    var_idx <- which(names(data) == cnames[var_idx])
    val_idx <- ncol(data)
    id_arg  <- setdiff(seq_len(ncol(data)), c(var_idx, val_idx))
    if (length(id_arg) == 0L) id_arg <- NULL
  }

  if (verbose) {
    n_id_show <- if (!is.null(id_arg)) length(id_arg) else NA_integer_
    cat(sprintf(
      "[dcast] rows=%d  id_cols=%s  variable=%s  value=%s  cores=%d  na_rm=%s\n",
      nrow(data), ifelse(is.na(n_id_show), "auto", as.character(n_id_show)),
      cnames[var_idx], cnames[val_idx], cores, isTRUE(na.rm)))
  }

  t0 <- proc.time()[["elapsed"]]
  result <- dcast_cpp(
    data          = data,
    id            = id_arg,
    variable      = as.integer(var_idx),
    value         = as.integer(val_idx),
    variable_name = NULL,
    cores         = cores,
    fill          = fill,
    na_rm         = isTRUE(na.rm)
  )
  if (verbose)
    cat(sprintf("[dcast] done in %.3fs\n",
                proc.time()[["elapsed"]] - t0))

  result
}

# @noRd
detect_first_last <- function(f) {
  if (!is.function(f) || is.primitive(f)) return(NULL)
  b <- base::body(f)
  if (is.null(b)) return(NULL)

  while (base::is.call(b) &&
         identical(b[[1L]], base::as.name("{")) &&
         length(b) == 2L) {
    b <- b[[2L]]
  }

  if (!base::is.call(b) || length(b) < 2L) return(NULL)
  fn <- b[[1L]]

  if ((identical(fn, base::as.name("[")) ||
       identical(fn, base::as.name("[["))) &&
      length(b) == 3L) {
    var <- b[[2L]]
    idx <- b[[3L]]
    if (is.name(var) || base::is.call(var)) {
      if (length(idx) == 1L && is.numeric(idx) && idx == 1L)
        return("first")
      if (base::is.call(idx) && length(idx) == 2L &&
          identical(idx[[1L]], base::as.name("length")) &&
          identical(idx[[2L]], var))
        return("last")
    }
  }

  if (is.name(fn) && length(b) >= 3L) {
    nm <- as.character(fn)
    if (nm %in% c("head", "tail")) {
      second <- b[[3L]]
      if (length(second) == 1L && is.numeric(second) && second == 1L)
        return(if (nm == "head") "first" else "last")
    }
  }

  NULL
}
