\name{melt}
\alias{melt}
\title{Fast wide-to-long data reshaping with flexible ID/measure specification}
\description{
Transforms a wide-format data frame into a long-format data frame using a
C++ backend with SIMD and optional OpenMP parallelism. Supports automatic
inference of ID columns, user-specified ID or measure variables, custom
column names, and two storage layouts (row-major or column-major).
}
\usage{
melt(data, id = NULL, measure.vars = NULL,
     variable.name = "variable", value.name = "value",
     na.rm = FALSE, cores = NULL, major = c("row", "col"),
     verbose = FALSE, parallel_threshold = 5e6, id.vars = NULL)
}
\arguments{
  \item{data}{A data frame to reshape. Numeric, integer, logical,
    character, and factor columns are supported.}
  \item{id}{Identifier columns. Character names, integer indices, a
    logical mask, or \code{NULL}. If \code{NULL} and
    \code{measure.vars} is also \code{NULL}, ID columns are inferred
    automatically: non-numeric, non-integer, non-logical columns and
    factors are treated as IDs.}
  \item{measure.vars}{Measure columns. Character names, integer
    indices, a logical mask, or \code{NULL}. If supplied, all other
    columns become IDs. Only one of \code{id} and \code{measure.vars}
    should be supplied.}
  \item{variable.name}{Name of the new factor column that stores the
    original column names of the measure variables. Default
    \code{"variable"}.}
  \item{value.name}{Name of the new numeric column that stores the
    melted values. Default \code{"value"}.}
  \item{na.rm}{Logical; if \code{TRUE}, rows where the value column is
    \code{NA} are removed. Default \code{FALSE}.}
  \item{cores}{Number of OpenMP threads. \code{NULL} (default) lets the
    C++ backend pick a value based on data size. The global option
    \code{dataprep.cores} overrides the automatic choice.}
  \item{major}{Memory layout: \code{"row"} (tidyr-style) or
    \code{"col"} (reshape2-style). Default \code{"row"}.}
  \item{verbose}{Logical; if \code{TRUE}, prints timing messages.}
  \item{parallel_threshold}{Minimum number of output elements before
    automatic parallelism is enabled. Default \code{5e6}.}
  \item{id.vars}{Alias of \code{id}, provided for reshape2 / data.table
    compatibility. Supplying both is an error.}
}
\value{
A data frame with \code{nrow(data) * length(measure.vars)} rows and
\code{length(id.vars) + 2} columns: the ID columns, a factor column
named \code{variable.name}, and a numeric column named
\code{value.name}.
}
\references{
1. Example data is from https://smear.avaa.csc.fi/download.
2. Eddelbuettel, D. and Francois, R. (2011). Rcpp: Seamless R and C++
   Integration. Journal of Statistical Software, 40(8), 1-18.
}
\author{
Chun-Sheng Liang <liangchunsheng@lzu.edu.cn>
}
\examples{
df <- data.frame(id = 1:5, category = factor(letters[1:5]),
                 v1 = rnorm(5), v2 = rnorm(5))
melt(df)
melt(df, id.vars = c("id", "category"))
melt(df, measure.vars = c("v1", "v2"))

df2 <- data.frame(id = 1:3, x = c(1, NA, 3), y = c(4, 5, NA))
melt(df2, id.vars = "id", variable.name = "var",
     value.name = "val", na.rm = TRUE)

df_wide <- data.frame(id = 1:100, matrix(rnorm(100 * 50), ncol = 50))
melt(df_wide, id.vars = "id", major = "col")
}
\keyword{manip}