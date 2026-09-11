\name{dry_run}
\alias{dry_run}
\title{
Simulate preprocessing and report changes without modifying data
}
\description{
Simulates the effect of one or more preprocessing steps on a dataset and returns a report describing which variables would be removed, how many observations might be deleted, and other changes. The input data are not altered.
}
\usage{
dry_run(data, steps = c("varidele", "obsedele", "outlier"),
        cols = NULL, group = NULL, date_col = NULL,
        fraction = 0.25, top = 0.995, bottom = 0.0025,
        by = "min", half = 30, method_outlier = "iqr", coef = 1.5,
        verbose = FALSE)
}
\arguments{
  \item{data}{A data frame to be simulated.}
  \item{steps}{Character vector of steps to simulate. Currently supported: \code{"varidele"}, \code{"obsedele"}, and \code{"outlier"}.}
  \item{cols}{Column indices or names of numeric variables to consider. If \code{NULL}, all numeric columns are used.}
  \item{group}{Optional grouping column for outlier detection.}
  \item{date_col}{Optional time column for observation deletion.}
  \item{fraction}{Missing fraction threshold for variable deletion.}
  \item{top}{Top percentile for outlier detection (percentile method).}
  \item{bottom}{Bottom percentile for outlier detection (percentile method).}
  \item{by}{Time unit for consecutive missing deletion (see \code{obsedele}).}
  \item{half}{Half window size for consecutive missing deletion.}
  \item{method_outlier}{Outlier detection method: \code{"iqr"}, \code{"mad"}, or \code{"percentile"}.}
  \item{coef}{Coefficient for IQR or MAD outlier detection.}
  \item{verbose}{Logical; if \code{TRUE}, prints progress message.}
}
\value{
A list with the following components:
\itemize{
  \item \code{original_n}: number of rows in the original data.
  \item \code{original_ncol}: number of columns in the original data.
  \item \code{varidele}: a list containing removed column names and count (if step included).
  \item \code{obsedele}: a note that observation deletion would be applied.
  \item \code{outlier}: a note that outlier detection would be applied.
  \item \code{final_n}: number of rows remaining after simulation (only reflects variable deletion at present).
  \item \code{final_ncol}: number of columns remaining after simulation.
}
}
\details{
This function is useful for understanding the potential impact of preprocessing without changing the dataset. Note that for steps like \code{obsedele} and \code{outlier}, the current implementation only indicates that they would be performed; exact row and value counts are not computed in this lightweight version.
}
\examples{
dry_run(data[1:200, c(1, 4, 17:19)], cols = 3:5, steps = c("varidele", "obsedele", "outlier"))
}