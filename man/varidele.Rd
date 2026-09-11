\name{varidele}
\alias{varidele}
\title{
Delete variables containing too many missing values
}
\description{
Removes numeric columns whose missing value proportion exceeds a given threshold. The deletion is performed only at the two ends of the selected contiguous block, ensuring that the remaining variables form a continuous range (as in the original algorithm).
}
\usage{
varidele(data, cols = NULL, fraction = 0.25, verbose = FALSE)
}
\arguments{
  \item{data}{A data frame or matrix containing numeric columns.}
  \item{cols}{Column indices or names of the contiguous numeric block to consider. Must be continuous. If \code{NULL}, all columns are used.}
  \item{fraction}{Missing proportion threshold. Variables with a missing fraction >= \code{fraction} are deleted. Default 0.25.}
  \item{verbose}{Logical; if \code{TRUE}, prints timing and deleted variables.}
}
\details{
The missing fraction is computed after excluding rows where all selected variables are NA. Only columns at the beginning and end of the selected range are removed, preserving internal structure.
}
\value{
A data frame with the retained columns.
}
\references{
1. Example data is from https://smear.avaa.csc.fi/download. It includes particle number concentrations in SMEAR I Varrio forest.
}
\author{
Chun-Sheng Liang <liangchunsheng@lzu.edu.cn>
}
\examples{
varidele(data, cols = 5:65)
}