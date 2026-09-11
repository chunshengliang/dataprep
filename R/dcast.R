#' Cast a long-format data.frame into a wide format
#'
#' Thin R wrapper. All heavy lifting (key building, scatter-write, fill)
#' is done in C++ (`dcast_cpp`). Only `fun.aggregate` falls back to R,
#' because it may be an arbitrary user-supplied function.
#'
#' @param data           data.frame in long format.
#' @param id             id columns (character names, integer indices,
#'                       logical mask, or NULL). Overrides formula LHS.
#' @param formula        optional formula of the form `id1 + id2 ~ variable`.
#' @param variable       name of the "variable" column (case/plural tolerant
#'                       when NULL).
#' @param value          name of the "value" column (case/plural tolerant
#'                       when NULL).
#' @param value.var      alias of `value` (reshape2 / data.table compat).
#' @param fill           value used to fill missing cells (default NA_real_).
#' @param fun.aggregate  optional aggregation function applied to duplicate
#'                       (id, variable) pairs before casting. If NULL, the
#'                       C++ backend uses "last write wins", which for a
#'                       canonical melt output is the right value.
#' @param na.rm          if TRUE, skip NA/NaN values when scattering.
#' @param cores          number of OpenMP threads; 0 = auto.
#' @param verbose        print progress info.
#'
#' @return data.frame in wide format.
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

  # ---- basic checks ----
  if (!is.data.frame(data)) stop("dcast is only supported for data frames")
  if (nrow(data) == 0L)     stop("data has no rows")
  if (ncol(data) < 2L)      stop("data must have at least 2 columns")

  ncols  <- ncol(data)
  cnames <- names(data)

  # ---- helper: case/plural tolerant column matcher ----
  find_col_match <- function(cnames, base_name) {
    upper_first <- paste0(toupper(substr(base_name, 1L, 1L)),
                          substring(base_name, 2L))
    candidates <- c(
      base_name,
      paste0(base_name, "s"),
      upper_first,
      paste0(upper_first, "s"),
      toupper(base_name),
      toupper(paste0(base_name, "s"))
    )
    for (cand in candidates) {
      hit <- which(cnames == cand)
      if (length(hit) == 1L) return(hit)
    }
    integer(0)
  }

  # ---- alias handling ----
  if (!is.null(value.var) && is.null(value)) value <- value.var

  # ---- formula parsing ----
  if (!is.null(formula)) {
    if (!inherits(formula, "formula"))
      stop("formula must be a formula object")
    if (length(formula) != 3L)
      stop("formula must have the form 'lhs ~ rhs'")

    lhs <- all.vars(formula[[2]])
    rhs <- all.vars(formula[[3]])

    if (is.null(id))       id <- lhs
    if (is.null(variable)) {
      if (length(rhs) != 1L)
        stop("formula RHS must specify exactly one variable column")
      variable <- rhs
    }
  }

  # ---- smart inference of variable / value ----
  if (is.null(variable)) {
    hit <- find_col_match(cnames, "variable")
    if (length(hit) == 1L) variable <- hit
  }
  if (is.null(value)) {
    hit <- find_col_match(cnames, "value")
    if (length(hit) == 1L) value <- hit
  }
  if (is.null(variable)) stop("cannot infer 'variable' column; specify it explicitly")
  if (is.null(value))    stop("cannot infer 'value' column; specify it explicitly")

  # ---- resolve variable / value to single integer indices ----
  resolve_single <- function(spec, arg_name) {
    if (is.character(spec)) {
      if (length(spec) != 1L)
        stop(sprintf("%s must be a single column name", arg_name))
      idx <- match(spec, cnames)
      if (is.na(idx)) stop(sprintf("%s column '%s' not found", arg_name, spec))
      return(as.integer(idx))
    }
    if (is.numeric(spec)) {
      idx <- as.integer(spec)[1]
      if (idx < 1L || idx > ncols)
        stop(sprintf("%s column index out of range", arg_name))
      return(idx)
    }
    stop(sprintf("%s must be character or numeric", arg_name))
  }
  var_idx <- resolve_single(variable, "variable")
  val_idx <- resolve_single(value,    "value")
  if (var_idx == val_idx)
    stop("variable and value must be distinct columns")

  # ---- normalize id ----
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
    } else stop("id must be character, numeric, or logical")
    id_arg <- setdiff(id_arg, c(var_idx, val_idx))
    if (length(id_arg) == 0L) id_arg <- NULL
  }

  # ---- fun.aggregate: pre-aggregate in R ----
  #   This is the only code path that stays in R, because
  #   `fun.aggregate` is an arbitrary user function.
  if (!is.null(fun.aggregate)) {
    if (!is.function(fun.aggregate))
      stop("fun.aggregate must be a function")

    agg_id <- if (!is.null(id_arg)) cnames[id_arg] else
      setdiff(cnames, c(cnames[var_idx], cnames[val_idx]))

    key_cols <- c(agg_id, cnames[var_idx])
    key_list <- lapply(key_cols, function(nm) data[[nm]])
    names(key_list) <- key_cols

    grp       <- do.call(paste, c(key_list, sep = "\r"))
    uniq_grp  <- unique(grp)
    first_idx <- match(uniq_grp, grp)
    dedup     <- data[first_idx, , drop = FALSE]
    rownames(dedup) <- NULL

    split_vals <- split(data[[val_idx]], grp)
    agg_vals   <- vapply(split_vals,
                         function(v) fun.aggregate(v, na.rm = TRUE),
                         FUN.VALUE = numeric(1))
    dedup[[val_idx]] <- as.numeric(agg_vals[grp[first_idx]])
    data <- dedup
  }

  # ---- normalize cores ----
  cores <- as.integer(cores)[1]
  if (is.na(cores) || cores < 0L) cores <- 0L
  if (cores > 64L)                cores <- 64L

  if (verbose) {
    n_id_show <- if (!is.null(id_arg)) length(id_arg) else NA_integer_
    cat(sprintf(
      "[dcast] rows=%d  id_cols=%s  variable=%s  value=%s  cores=%d  na_rm=%s\n",
      nrow(data), ifelse(is.na(n_id_show), "auto", n_id_show),
      cnames[var_idx], cnames[val_idx], cores, na.rm
    ))
  }

  t0 <- proc.time()[["elapsed"]]
  # NOTE: `R_NilValue` is a C++ symbol. In R we pass NULL.
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
  if (verbose) cat(sprintf("[dcast] done in %.3fs\n",
                           proc.time()[["elapsed"]] - t0))

  result
}