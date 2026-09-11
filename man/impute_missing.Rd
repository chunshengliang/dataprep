\name{impute_missing}
\alias{impute_missing}
\title{
Impute missing values using various methods
}
\description{
Fills missing values in selected numeric columns using one of several methods: linear interpolation, last observation carried forward (LOCF), next observation carried backward (NOCB), mean, or median. Grouping is supported.
}
\usage{
impute_missing(data, cols = NULL, method = "linear",
               group = NULL, date_col = NULL, max_gap = NULL,
               verbose = FALSE)
}
\arguments{
  \item{data}{A data frame or matrix containing variables to impute.}
  \item{cols}{The column indices or names of selected variables. If NULL, all columns are used.}
  \item{method}{Imputation method. One of "linear" (default), "locf", "nocb", "mean", "median".}
  \item{group}{The column index or name of the grouping variable. If provided, imputation is performed within each group.}
  \item{date_col}{The column index or name of the time column (optional, not used for most methods).}
  \item{max_gap}{Maximum gap length to impute; longer gaps are left as NA (currently not implemented).}
  \item{verbose}{Logical; if \code{TRUE}, prints progress message.}
}
\value{
A data frame with missing values filled.
}
\examples{
impute_missing(data[1:100, c(1, 4, 17:19)], cols = 3:5, method = "locf")
}