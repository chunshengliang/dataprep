\name{dcast}
\alias{dcast}
\title{Cast a long-format data.frame into a wide format}
\description{
Structural inverse of \code{\link{melt}}: a long-format data frame
with one row per \code{(id, variable)} pair is reshaped into a
wide-format data frame with one row per \code{id} combination and
one column per \code{variable} level. The C++ backend builds
compact integer lookup tables for both the row keys (id columns)
and the column keys (variable column), then writes values by
output column in strictly sequential order.
}
\usage{
dcast(data, id = NULL, formula = NULL,
      variable = NULL, value = NULL, value.var = NULL,
      fill = NA_real_, fun.aggregate = NULL,
      na.rm = FALSE, cores = 0L, verbose = FALSE)
}
\arguments{
  \item{data}{A data frame in long format. It must contain one
    column identifying the variable (e.g. \code{"variable"}), one
    column holding the value (e.g. \code{"value"}), and one or more
    id columns.}
  \item{id}{Identifier columns. Character names, integer indices, a
    logical mask, or \code{NULL}. If \code{NULL}, all columns other
    than the variable and value columns are used as ids.}
  \item{formula}{Optional formula of the form
    \code{id1 + id2 ~ variable}. When supplied, the left-hand side
    overrides \code{id} and the right-hand side overrides
    \code{variable}.}
  \item{variable}{Name (or index) of the column identifying the
    variable. If \code{NULL}, a column named \code{"variable"},
    \code{"variables"}, \code{"Variable"}, \code{"Variables"},
    \code{"VARIABLE"}, or \code{"VARIABLES"} is used when exactly
    one such column exists.}
  \item{value}{Name (or index) of the column holding the values. If
    \code{NULL}, a column named \code{"value"}, \code{"values"},
    \code{"Value"}, \code{"Values"}, \code{"VALUE"}, or
    \code{"VALUES"} is used when exactly one such column exists.}
  \item{value.var}{Alias for \code{value}. Used only when \code{value} is \code{NULL}; if both are supplied, \code{value} takes precedence.}
  \item{fill}{Value used to fill cells for \code{(id, variable)}
    pairs that do not appear in the input. Cells that appear in the
    input with value \code{NA} keep \code{NA} unless \code{na.rm =
    TRUE}. Default \code{NA_real_}.}
  \item{fun.aggregate}{Optional function used to reduce duplicate
    \code{(id, variable)} pairs.  The C++ backend recognises
    \code{base::sum}, \code{base::mean}, \code{base::min},
    \code{base::max}, \code{base::length}, \code{stats::median}
    (or \code{base::median}), and functions of the form
    \code{function(x) x[1]} / \code{function(x) x[length(x)]}
    for first / last, and routes them to a specialised kernel.
    Any other function falls back to a base-R
    \code{stats::aggregate()} pre-pass.  Without it, the last
    occurrence wins.}
  \item{na.rm}{Logical; if \code{TRUE}, rows whose value is \code{NA}
    or \code{NaN} are skipped during scatter, so the corresponding
    output cell keeps the \code{fill} value. Default \code{FALSE}.}
  \item{cores}{Number of OpenMP threads. \code{0} (default) lets the
    C++ backend choose based on data size.}
  \item{verbose}{Logical; if \code{TRUE}, prints timing information.}
}
\details{
\code{dcast()} is the structural inverse of \code{\link{melt}}. The
C++ backend detects canonical \code{melt()} output automatically
(the variable column is periodic and every id column is constant
within one period) and switches to a \emph{block-path tile
transpose}: each tile of \code{TILE x period} doubles is read
contiguously into an L1 buffer, transposed in place, and written
contiguously to the output columns. This keeps both reads and
writes sequential and enables OpenMP parallelisation. The cost per
tile is \code{O(TILE * period)}, independent of the number of
levels, which is why wide-level tables scale well.

When the input is not block-aligned, \code{dcast()} builds an
open-addressing hash of 64-bit packed row keys. If the combined bit
budget of the id columns exceeds 64, it falls back to a 96-bit
fingerprint (\code{uint64_t} + \code{uint32_t}) computed by a 4-way
parallel FNV-1a and stored in a 16-byte slot table. For
shuffle-friendly input, a 4-pass LSD radix sort over the packed
keys replaces the hash table entirely; and when the block path is
taken and the id column is a permutation of \code{1..n_blocks}, a
direct-index shortcut bypasses both.

0.1.8.1 adds three implementation shortcuts: a zero-copy
\code{dcast_fast_cpp} entry point used when the call shape is
canonical; a 16x16 AVX-512 register transpose for
\code{period == 16}; and a dedicated
\code{dcast_aggregate_cpp} kernel for \code{fun.aggregate} that
recognises \code{sum}, \code{mean}, \code{min}, \code{max},
\code{length}, \code{median}, \code{first}, and \code{last}.
Output buffers use \code{MADV_HUGEPAGE} and, on Linux 6.1+,
\code{MADV_POPULATE_WRITE} to avoid first-touch page faults;
input columns are promoted with \code{MADV_COLLAPSE}.

When \code{fun.aggregate} is supplied, \code{dcast()} routes to a
fused C++ kernel that reads the long input once and writes the
aggregated wide output directly, without materialising an
intermediate long table. The output is identical to
\code{reshape2::dcast(..., fun.aggregate = f)},
\code{data.table::dcast(..., fun.aggregate = f)},
\code{collapse::pivot(..., FUN = f)},
\code{tidyr::pivot_wider(..., values_fn = f)},
\code{pandas.pivot_table(..., aggfunc = f)},
\code{polars.pivot(..., aggregate_function = f)},
\code{dask.pivot_table(..., aggfunc = f)}, and
\code{duckdb PIVOT ... USING f(value)} within \code{tol = 1e-10}
on every tested shape.

\strong{Benchmarks.}  \code{dcast()} is benchmarked against all
eight major alternatives in the R and Python ecosystems:
\code{reshape2}, \code{data.table}, \code{tidyr}, \code{collapse},
\code{pandas}, \code{polars}, \code{dask}, and \code{duckdb}.
Every cell is measured with a C++ steady-clock timer.  The first
call is a warmup and is excluded from the reported statistics; an
adaptive rule picks the number of timed iterations from the warmup
time.  Mixed-type id columns (integer / character alternation for
\code{n_id >= 2}) are the default; set
\code{DATAPREP_MIXED_TYPES=FALSE} to force the all-integer baseline.

On the canonical long-to-wide shape, \code{dcast()} is faster than
every tested alternative in most cells.  The median speed-up
relative to each competitor spans \code{1.82x} to \code{651.52x} on
Ubuntu 25.10 (\code{median = 74.46x}) and \code{0.92x} to
\code{1849.52x} on Windows 11 Pro for Workstations
(\code{median = 42.22x}).  The single sub-1.0x cell is
\code{collapse @ n_long=1,000, 1 id + 10 lvl} on Windows
(\code{0.92x}).

For aggregated \code{dcast()}, the median speed-up spans
\code{2.32x} to \code{733.49x} on Ubuntu 25.10
(\code{median = 27.41x}) and \code{1.99x} to \code{872.38x} on
Windows 11 Pro for Workstations (\code{median = 17.93x}).
}
\value{
A wide-format data frame with one row per unique \code{id}
combination and one column per unique value of the variable column,
plus the id columns. Column names are the string representation of
the variable values.
}
\references{
Liang, C.-S., Wu, H., Li, H.-Y., Zhang, Q., Li, Z. & He, K.-B.
(2020). Efficient data preprocessing, episode classification, and
source apportionment of particle number concentrations.
\emph{Science of the Total Environment}, 741, 140923.
\doi{10.1016/j.scitotenv.2020.140923}
}
\author{
Chun-Sheng Liang \email{chun-shengliang@qq.com}
}
\seealso{
\code{\link{melt}} for the inverse operation.
\code{vignette("dataprep-melt-dcast")} for usage notes and
implementation.
}
\examples{
## --- Basic usage --------------------------------------------------

long <- data.frame(
  id       = rep(1:3, each = 2),
  variable = rep(c("x", "y"), 3),
  value    = c(1, 2, 3, 4, 5, 6)
)
dcast(long, id = "id", variable = "variable", value = "value")


## --- Formula interface --------------------------------------------

dcast(long, formula = id ~ variable)


## --- Multiple id columns ------------------------------------------

long2 <- data.frame(
  year     = rep(2020:2021, each = 4),
  city     = rep(c("A", "B"), each = 2, times = 2),
  variable = rep(c("temp", "rain"), 4),
  value    = c(15.2, 210, 14.8, 180, 16.1, 230, 15.5, 195)
)

# Explicit id columns
dcast(long2, id = c("year", "city"),
      variable = "variable", value = "value")

# Formula form (equivalent)
dcast(long2, formula = year + city ~ variable)


## --- Fill missing cells -------------------------------------------

# (2, "y") is missing from the input; fill = 0 gives it 0.
long3 <- data.frame(
  id       = c(1, 1, 2),
  variable = c("x", "y", "x"),
  value    = c(1, 2, 3)
)
dcast(long3, id = "id",
      variable = "variable", value = "value",
      fill = 0)


## --- Aggregate duplicate (id, variable) pairs ---------------------

# id = 1 appears twice with variable = "x"; the mean is 1.5.
long4 <- data.frame(
  id       = c(1, 1, 2),
  variable = c("x", "x", "x"),
  value    = c(1, 2, 3)
)
dcast(long4, id = "id",
      variable = "variable", value = "value",
      fun.aggregate = mean)

# sum and length work the same way
dcast(long4, id = "id",
      variable = "variable", value = "value",
      fun.aggregate = sum)


## --- Skip NA values during scatter --------------------------------

long5 <- data.frame(
  id       = c(1, 1, 2, 2),
  variable = c("x", "y", "x", "y"),
  value    = c(1, NA, 3, 4)
)

# Default: NA stays as NA
dcast(long5, id = "id",
      variable = "variable", value = "value")

# na.rm = TRUE: (1, "y") is skipped, cell keeps the fill value
dcast(long5, id = "id",
      variable = "variable", value = "value",
      na.rm = TRUE, fill = -1)


## --- Round-trip with melt() ---------------------------------------

wide <- data.frame(id = 1:3, a = c(1.5, 2.5, 3.5), b = c(4.5, 5.5, 6.5))
back <- dcast(melt(wide, id.vars = "id"),
              id = "id", variable = "variable", value = "value")
back[order(back$id), ]


## --- Larger example: 1000 rows, 50 levels -------------------------

set.seed(1)
n_rows   <- 1000L
n_levels <- 50L
long_big <- data.frame(
  id       = rep(seq_len(n_rows / n_levels), each = n_levels),
  variable = rep(sprintf("v\%02d", seq_len(n_levels)),
                 times = n_rows / n_levels),
  value    = rnorm(n_rows)
)
wide_big <- dcast(long_big, id = "id",
                  variable = "variable", value = "value")
dim(wide_big)
}
\keyword{manip}