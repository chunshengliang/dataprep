\name{sample_data}
\alias{sample_data}
\title{
  Random sampling with optional stratification
}
\description{
  Performs simple random sampling or stratified random sampling (by a grouping
  variable) from a data frame.
}
\usage{
sample_data(data, group = NULL, size = NULL, frac = NULL,
            replace = FALSE, seed = NULL, verbose = FALSE)
}
\arguments{
  \item{data}{A data frame to sample from.}
  \item{size}{Number of observations to sample (per group if \code{group} is used).}
  \item{frac}{Fraction of observations to sample (per group if \code{group} is used).}
  \item{group}{Optional grouping column for stratified sampling.}
  \item{replace}{Logical; sample with replacement?}
  \item{seed}{Optional integer seed for reproducibility.}
  \item{verbose}{Logical; if \code{TRUE}, prints sample sizes and timing.}
}
\details{
  If \code{group} is provided, sampling is performed separately within each group.
  Either \code{size} or \code{frac} must be specified. If both are provided,
  \code{size} takes precedence.
}
\value{
  A data frame with the sampled rows.
}
\examples{
# Simple random sample
sample_data(mtcars, frac = 0.5)

# Stratified sample by cylinder group
sample_data(mtcars, size = 3, group = "cyl", seed = 123)
}
