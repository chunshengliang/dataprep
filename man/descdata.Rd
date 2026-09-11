\name{descdata}
\alias{descdata}
\title{
Fast descriptive statistics
}
\description{
Computes descriptive statistics for selected numeric columns using efficient C++ routines, without relying on external packages. This provides a fast alternative to base R's \code{sapply} based calculations.
}
\usage{
descdata(data, cols = NULL, stats = 1:9, first = "variables",
         cores = NULL, verbose = FALSE)
}
\arguments{
  \item{data}{A data frame, matrix, or numeric vector. If a vector is supplied, it is treated as a single variable.}
  \item{cols}{Column indices or names of numeric variables to describe. If \code{NULL}, all numeric columns are used (for a data frame or matrix).}
  \item{stats}{Selecting or rearranging items from the 9 statistics: n, na, mean, sd, median, trimmed, min, max, and IQR. Can be a numeric vector or a character vector of statistic names.}
  \item{first}{Name for the first column of the output, which contains the variable identifiers.}
  \item{cores}{Number of CPU cores for parallel computation (passed to C++).}
  \item{verbose}{Logical; if \code{TRUE}, prints timing message.}
}
\details{
The function accepts vectors, matrices, and data frames. Numeric columns are required; non-numeric columns are ignored or cause an error if explicitly selected. The calculation is performed entirely in C++ via Rcpp for speed.
}
\value{
A data frame with descriptive statistics for each variable. Columns correspond to the statistics selected (n, na, mean, sd, median, trimmed, min, max, IQR). The first column contains variable names (or numeric values if names are numeric).
}
\references{
1. Example data is from https://smear.avaa.csc.fi/download. It includes particle number concentrations in SMEAR I Varrio forest.
}
\author{
Chun-Sheng Liang <liangchunsheng@lzu.edu.cn>
}
\examples{
# Numeric variable names
descdata(data, cols = 5:65)
# Select statistics by number
descdata(data, cols = 5:65, stats = c(2, 7:9))
# Select statistics by name
descdata(data, cols = 5:65, stats = c("na", "min", "max", "IQR"))
}