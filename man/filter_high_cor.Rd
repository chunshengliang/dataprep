\name{filter_high_cor}
\alias{filter_high_cor}
\title{
  Remove highly correlated variables
}
\description{
  Identifies and removes numeric columns that have an absolute correlation above
  a given threshold. Useful for reducing multicollinearity in regression or
  feature redundancy in machine learning.
}
\usage{
filter_high_cor(data, cols = NULL, cutoff = 0.9,
                method = "pearson", keep = "first", verbose = FALSE)
}
\arguments{
  \item{data}{A data frame.}
  \item{cols}{Column indices or names of numeric variables to check. If \code{NULL}, all numeric columns are used.}
  \item{cutoff}{Absolute correlation threshold. Variables with correlation > \code{cutoff} are candidates for removal.}
  \item{method}{Correlation method. One of \code{"pearson"} or \code{"spearman"}.}
  \item{keep}{Strategy for keeping one variable from each correlated pair. Options: \code{"first"} (keep the first occurrence) or \code{"highest_var"} (keep the one with higher variance).}
  \item{verbose}{Logical; if \code{TRUE}, prints removed column names and their maximum correlation.}
}
\details{
  The function computes the correlation matrix for the selected numeric columns.
  For each pair with absolute correlation > \code{cutoff}, the column to remove
  is determined by the \code{keep} strategy. The returned data frame excludes
  the removed columns.
}
\value{
  A data frame with highly correlated variables removed.
}
\examples{
n <- 100
x1 <- rnorm(n)
x2 <- rnorm(n)
x3 <- x1 * 0.9 + rnorm(n, sd = 0.2)
x4 <- x1 * 0.2 + rnorm(n)
df <- data.frame(x1, x2, x3, x4)
filter_high_cor(df, cutoff = 0.8)
}
