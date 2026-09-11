\name{clean_strings}
\alias{clean_strings}
\title{
  Clean and standardize character columns
}
\description{
  Applies common string cleaning operations to character or factor columns,
  such as trimming whitespace, changing case, and applying regular expression
  substitutions.
}
\usage{
clean_strings(data, cols = NULL, trim = FALSE, tolower = FALSE,
              toupper = FALSE, pattern = NULL, replacement = NULL,
              verbose = FALSE)
}
\arguments{
  \item{data}{A data frame.}
  \item{cols}{Column indices or names to clean. Only character or factor columns are processed.}
  \item{trim}{Logical; if \code{TRUE}, leading/trailing whitespace is removed.}
  \item{tolower}{Logical; if \code{TRUE}, convert to lowercase.}
  \item{toupper}{Logical; if \code{TRUE}, convert to uppercase.}
  \item{pattern}{Optional regular expression pattern to replace.}
  \item{replacement}{Replacement string for \code{pattern}.}
  \item{verbose}{Logical; if \code{TRUE}, prints the operations performed and timing.}
}
\details{
  The function applies operations in the order: trim, tolower/toupper, then
  pattern replacement. Only character and factor columns are modified;
  other columns are left unchanged.
}
\value{
  A data frame with cleaned character columns.
}
\examples{
df <- data.frame(
  id = 1:3,
  name = c("  Alice ", "BOB", "Charlie "),
  city = c("New York", "london", "Paris")
)
clean_strings(df, cols = c("name", "city"), trim = TRUE, tolower = TRUE)
}
