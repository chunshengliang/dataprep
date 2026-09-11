\name{na_diagnose}
\alias{na_diagnose}
\title{
Diagnose missing values patterns in data
}
\description{
Provides a comprehensive summary of missing values for selected columns, including the number and fraction of missing values, the number of consecutive missing runs, and the longest consecutive missing run length. It also optionally reports time gap statistics when a time column is available.
}
\usage{
na_diagnose(data, cols = NULL, date_col = NULL, verbose = FALSE)
}
\arguments{
  \item{data}{A data frame or matrix containing variables to diagnose.}
  \item{cols}{The column indices or names of selected variables. If NULL, all columns are used.}
  \item{date_col}{The column index or name of the time column. If specified, time gap statistics will be reported.}
  \item{verbose}{Logical; if \code{TRUE}, prints progress messages.}
}
\value{
A data frame with columns: variable, n (total observations), na (number of missing values), na_frac (missing fraction), na_runs (number of consecutive missing runs), max_run (longest consecutive missing run length).
}
\examples{
na_diagnose(data[1:500, c(1, 4, 17:19)], cols = 3:5, date_col = 1)
}