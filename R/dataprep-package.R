#' dataprep: Efficient and Flexible Data Preprocessing Tools
#'
#' @name dataprep-package
#' @aliases dataprep-package
#' @keywords internal
#' @importFrom Rcpp evalCpp
#' @importFrom stats IQR aggregate complete.cases mad median
#' @importFrom stats quantile sd setNames as.formula
#' @importFrom utils head tail
#' @importFrom parallel detectCores makeCluster stopCluster
#' @useDynLib dataprep, .registration = TRUE
NULL

utils::globalVariables(c(
  ".data", ".x",
  "value", "variable", "percentile",
  "snr", "sdr", "orr", "index", "n"
))