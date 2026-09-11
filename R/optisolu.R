#' Find optimal interval and times for condextr
#' @param data A data frame.
#' @param cols Columns to process.
#' @param group Grouping column.
#' @param interval,times Maximum interval and times to test.
#' @param top,top.error,top.magnitude,bottom,bottom.error,bottom.magnitude Outlier thresholds.
#' @param by,half Time parameters.
#' @param date_col Time column.
#' @param cores Number of CPU cores.
#' @param verbose Logical.
#' @return A data frame with search results.
#' @export
#' @noRd
optisolu <- function(data, cols = NULL, group = NULL,
                     interval = 35, times = 10,
                     top = .995, top.error = .1, top.magnitude = .2,
                     bottom = .0025, bottom.error = .2, bottom.magnitude = .4,
                     by = 'min', half = 30, date_col = NULL, cores = NULL,
                     verbose = FALSE) {
  t0 <- Sys.time()
  idx <- resolve_cols(data, cols)

  if (length(idx) > 0) {
    all_na <- sapply(data[, idx, drop = FALSE], function(x) all(is.na(x)))
    if (any(all_na)) {
      warning("The following selected columns are entirely NA and will be excluded: ",
              paste(names(all_na)[all_na], collapse = ", "),
              ". Consider using varidele to remove them beforehand.")
      idx <- idx[!all_na]
      if (length(idx) == 0) {
        stop("No valid columns remain after removing all-NA columns.")
      }
    }
  }

  date_info <- resolve_date_col(data, date_col)
  date_name <- date_info$name

  cases <- expand.grid(interval = 1:interval, times = 1:times)
  n_cases <- nrow(cases)

  if (is.null(cores)) {
    total_cores <- 1
  } else {
    total_cores <- as.integer(cores)
  }
  outer_cores <- min(128, n_cases)
  inner_cores <- 1L

  run_case <- function(i) {
    inte <- cases$interval[i]
    tim <- cases$times[i]
    a <- condextr(data, cols = idx, group = group,
                  interval = inte, times = tim,
                  top = top, top.error = top.error, top.magnitude = top.magnitude,
                  bottom = bottom, bottom.error = bottom.error,
                  bottom.magnitude = bottom.magnitude,
                  by = by, half = half, date_col = date_col,
                  cores = inner_cores,
                  verbose = FALSE)
    sdr <- 1 - nrow(a) / nrow(data)
    orr <- (sum(is.na(a[idx])) -
            sum(is.na(data[data[[date_name]] %in% a[[date_name]], idx]))) /
           (nrow(data) * ncol(data))
    snr <- mean(sapply(a[idx], function(x) mean(x, na.rm = TRUE) /
                                         sd(x, na.rm = TRUE)))
    data.frame(case = i, interval = inte, times = tim,
               sdr = sdr, orr = orr, snr = snr)
  }

  if (.Platform$OS.type == "unix") {
    dflist <- parallel::mclapply(seq_len(n_cases), run_case,
                                 mc.cores = outer_cores,
                                 mc.preschedule = TRUE)
  } else {
    cl <- parallel::makeCluster(outer_cores)
    on.exit(parallel::stopCluster(cl))

    parallel::clusterExport(cl, c("data", "idx", "group", "top", "top.error",
                                  "top.magnitude", "bottom", "bottom.error",
                                  "bottom.magnitude", "by", "half", "date_col",
                                  "date_name", "condextr", "resolve_cols",
                                  "resolve_date_col", "mark_outliers_cpp",
                                  "obsedele_cpp", "to_numeric_matrix",
                                  "inner_cores", "cases", "run_case"),
                            envir = environment())

    dflist <- parallel::parLapply(cl, seq_len(n_cases), run_case)
  }

  c <- do.call(rbind, dflist)

  c <- transform(c, index = (exp(snr) - 2) * 10 / exp(sdr + orr))
  c <- transform(c, relaindex = index / log(interval * times + 1))

  d <- percoutl(data, cols = idx, group = group, top = top, bottom = bottom,
                by = by, half = half, date_col = date_col, verbose = FALSE)
  e <- data.frame(
    sdr = 1 - nrow(d) / nrow(data),
    orr = (sum(is.na(d[idx])) -
           sum(is.na(data[data[[date_name]] %in% d[[date_name]], idx]))) /
          (nrow(data) * ncol(data)),
    snr = mean(sapply(d[idx], function(x) mean(x, na.rm = TRUE) /
                                         sd(x, na.rm = TRUE)))
  )
  c <- transform(c, optimal = ifelse(sdr < e$sdr & orr < e$orr & snr > e$snr,
                                     TRUE, FALSE))

  if (verbose) cat("Time used by optisolu:", format(Sys.time() - t0, digits = 3), "\n")
  c
}