\name{percoutl}
\alias{percoutl}
\title{Traditional percentile-based outlier removal}
\description{
Removes values above a top percentile and below a bottom percentile.
Thresholds are computed from quantiles. Optionally, observation deletion
based on consecutive missing values can be performed after outlier removal.
}
\usage{
percoutl(data, cols = NULL, group = NULL, top = 0.995, bottom = 0.0025,
         by = "min", half = 30, date_col = NULL, cores = NULL,
         verbose = FALSE)
}
\arguments{
  \item{data}{A data frame, matrix, or numeric vector. If a vector,
    outlier removal is applied directly.}
  \item{cols}{Column indices or names of numeric variables. If
    \code{NULL}, all numeric columns are used.}
  \item{group}{Optional grouping column for group-wise outlier removal.}
  \item{top}{Top percentile threshold. Values above this quantile are
    removed.}
  \item{bottom}{Bottom percentile threshold. Values below this quantile
    are removed.}
  \item{by}{Time unit for observation deletion (see \code{obsedele}).}
  \item{half}{Half window size for consecutive missing deletion.}
  \item{date_col}{Time column index or name. If \code{NULL},
    automatically detected.}
  \item{cores}{Number of CPU cores. Passed to \code{obsedele} for
    OpenMP control.}
  \item{verbose}{Logical; if \code{TRUE}, prints timing and deletion
    counts.}
}
\details{
This method is a one-size-fits-all approach and may remove non-outliers
or fail to remove some outliers. It is provided for comparison with
\code{\link{condextr}}, which uses a point-by-point weighted conditional
extremum criterion.
}
\value{
A data frame with outliers removed.
}
\references{
1. Example data is from https://smear.avaa.csc.fi/download. It includes
   particle number concentrations in SMEAR I Varrio forest.
}
\author{
Chun-Sheng Liang <liangchunsheng@lzu.edu.cn>
}
\examples{
percoutl(obsedele(data[1:500, c(1, 4, 17:19)], cols = 3:5, group = 2),
         cols = 3:5, group = 2)
}