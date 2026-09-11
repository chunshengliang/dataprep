\name{obsedele}
\alias{obsedele}
\title{
Delete observations with excessive consecutive missing values in time series
}
\description{
Removes observations (rows) where at least one selected variable has a run of consecutive missing values longer than a specified threshold. The threshold is derived from the half-window size and time step. This ensures that remaining data can be reasonably interpolated.
}
\usage{
obsedele(data, cols = NULL, group = NULL, by = "min",
half = 30, date_col = NULL, cores = NULL, verbose = FALSE)
}
\arguments{
  \item{data}{A data frame, matrix, or numeric vector. If a vector, consecutive NA runs are directly removed.}
  \item{cols}{Column indices or names of the contiguous numeric block to check. Must be continuous.}
  \item{group}{Grouping column index or name. If provided, the deletion is performed separately within each group.}
  \item{by}{Time extension unit (e.g., "min", "5 min").}
  \item{half}{Half window size; consecutive NA runs longer than \code{half} (or \code{2*half} for internal runs) are considered excessive.}
  \item{date_col}{Time column index or name. If \code{NULL}, automatically detected.}
  \item{cores}{Number of CPU cores (currently not used; parallelization is not implemented in the Rcpp version).}
  \item{verbose}{Logical; if \code{TRUE}, prints timing and deletion counts.}
}
\details{
The algorithm expands the time series to a regular grid, marks runs of missing values, and deletes rows where any selected column has a run longer than allowed. This approach is faster than a moving average calculation.
}
\value{
A data frame with rows deleted if they contain excessive consecutive NAs.
}
\references{
1. Example data is from https://smear.avaa.csc.fi/download. It includes particle number concentrations in SMEAR I Varrio forest.
}
\author{
Chun-Sheng Liang <liangchunsheng@lzu.edu.cn>
}
\examples{
obsedele(data[1:500, c(1, 4, 17:19)], cols = 3:5, group = 2)
}