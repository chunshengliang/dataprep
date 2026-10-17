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
     na.rm = FALSE, cores = NULL, major = NULL,
     as.factor = NULL,
     verbose = FALSE, parallel_threshold = 2e4, id.vars = NULL)
}
\arguments{
  \item{data}{A data frame to reshape. Numeric, integer, logical,
    character, and factor columns are accepted as id columns. Factor
    columns used as \code{measure.vars} cannot be cast to numeric, so
    their cells are written as \code{NA} in the value column; pass
    \code{measure.vars} as \code{character} or \code{numeric} to
    avoid this.}
  \item{id}{Identifier columns. Character names, integer indices, a
    logical mask, or \code{NULL}. If \code{NULL} and
    \code{measure.vars} is also \code{NULL}, ID columns are inferred
    automatically: non-numeric, non-integer, non-logical columns and
    factors are treated as IDs.}
  \item{measure.vars}{Measure columns. Character names, integer
    indices, a logical mask, or \code{NULL}. If supplied, all other
    columns become IDs. Only one of \code{id} and \code{measure.vars}
    should be supplied.}
  \item{variable.name}{Length-1 character; name of the output column that
    records which measure column each row came from.  Defaults to
    \code{"variable"}.  The column is a \code{factor} whose levels are
    the names of the measure columns when \code{as.factor} resolves to
    \code{TRUE} (the default for \code{major = "col"}), and a
    \code{character} vector otherwise.}
  \item{value.name}{Name of the new numeric column that stores the
    melted values. Default \code{"value"}.}
  \item{na.rm}{Logical; if \code{TRUE}, rows where the value column is
    \code{NA} are removed. Default \code{FALSE}.}
  \item{cores}{Number of OpenMP threads. \code{NULL} (default) lets the
    C++ backend pick a value based on data size. The global option
    \code{dataprep.cores} overrides the automatic choice.}
  \item{major}{Memory layout. \code{NULL} (default) is equivalent to
    \code{"col"}: column-major, identical to \code{reshape2::melt}
    (rows grouped by variable). \code{"row"} produces a row-major
    result matching the row order of \code{tidyr::pivot_longer}
    (rows grouped by id). There is no automatic switching based on
    the input shape.}
  \item{as.factor}{Logical or \code{NULL}.  If \code{TRUE}, the
    \code{variable.name} column is returned as a \code{factor}.  If
    \code{FALSE}, it is returned as \code{character}.  \code{NULL} (the
    default) uses \code{TRUE} when \code{major} is \code{"col"} and
    \code{FALSE} when \code{major} is \code{"row"}.  Explicit \code{TRUE}
    or \code{FALSE} overrides that default.}
  \item{verbose}{Logical; if \code{TRUE}, prints timing messages.}
  \item{parallel_threshold}{Minimum number of output elements, i.e.\
    \code{nrow(data) * length(measure.vars)}, before automatic
    parallelism is enabled. Default \code{2e4}.}
  \item{id.vars}{Alias of \code{id}, provided for reshape2 / data.table
    compatibility. Supplying both is an error.}
}
\details{
\strong{Shape.}  A \code{data.frame} with \code{n} rows and
\code{k = length(measure.vars)} measure columns becomes a
\code{data.frame} with \code{n * k} rows.  Identity columns are
replicated \code{k} times; a factor \code{variable} column records the
source column; a \code{double} \code{value} column holds the stacked
data.

\strong{Implementation.}  The heavy lifting is done by a C++17 kernel
compiled with OpenMP and dispatched to AVX-512 / AVX2 / SSE4.2
implementations of \code{memcpy}-like primitives at first use.  Output
buffers are pre-faulted via \code{madvise(MADV_HUGEPAGE)} on Linux and
\code{mallopt(M_MMAP_THRESHOLD, ...)} on glibc.  Threads scale with
input size up to a hardware-aware cap; per-column write-combining-buffer
pressure caps the thread count further when many measure columns are
being written at once.

The general path uses a two-phase, cache-aware schedule.  Phase A
writes the value and variable columns with non-temporal stores.
Phase B replicates the id columns with L2-sized blocking so that
each worker has exactly one active non-temporal write stream at a
time; this avoids saturating the x86 write-combining buffers when
many id columns are present.

\strong{Fast paths.}  Three code paths exist, chosen automatically:
a tiny-input path for \code{nrow <= 2048} with at most 64 measure
columns; a small-input path for at most 131072 output elements; and a
general two-phase path for everything else.  Passing \code{verbose =
TRUE} disables the two fast paths so that all calls exercise the same
kernel; this is intended for testing, not production.

\strong{Memory.}  Peak memory is roughly
\code{(n_id + 2) * n * k * 8} bytes plus the input.  For very large
inputs the R heap is briefly reserved with a same-sized sentinel so
that glibc's \code{M_MMAP_THRESHOLD} rises before the output
allocation; the sentinel is released before the C++ kernel runs.

\strong{Benchmarks.}  \code{melt()} is benchmarked against all
eight major alternatives in the R and Python ecosystems:
\code{reshape2}, \code{data.table}, \code{tidyr}, \code{collapse},
\code{pandas}, \code{polars}, \code{dask}, and \code{duckdb}.
Every cell is measured with a C++ steady-clock timer.  The first
call is a warmup and is excluded from the reported statistics; an
adaptive rule picks the number of timed iterations from the warmup
time.  Mixed-type id columns (integer / character alternation for
\code{n_id >= 2}) are the default; set
\code{DATAPREP_MIXED_TYPES=FALSE} to force the all-integer baseline.

The median speed-up of \code{melt()} relative to each competitor
spans \code{0.3x} to \code{1750.0x} on Ubuntu 25.10 and
\code{0.6x} to \code{435.2x} on Windows 11 Pro for Workstations.
The median across all tested cells and all competitors is
\code{9.64x} on Ubuntu 25.10 and \code{3.72x} on Windows 11 Pro
for Workstations.

The sub-1.0x cells are concentrated on the \code{1 id + 9 value}
shape, where \code{polars} is faster than \code{melt()}:
\code{polars @ rows=10,000,000, 10 id + 9 val} on Ubuntu
(\code{0.3x}), and \code{polars @ rows=100,000, 10 id + 9 val}
on Windows (\code{0.6x}).  Every other cell has \code{melt()}
ahead of or on par with the fastest competitor.

The speed-up comes from three design choices:

\enumerate{
  \item \strong{Two layout paths, selected by \code{major}.} The
        default is column-major. On the column-major path, each output
        column is written as one contiguous \code{memcpy} of the input
        column --- the fastest possible pattern. On the row-major path, tiles of eight
        rows are transposed in registers with
        \code{_mm512_shuffle_f64x2}.
  \item \strong{SIMD streaming stores.} For large outputs,
        \code{_mm512_stream_pd} and \code{_mm256_stream_si256}
        write directly to memory, bypassing the CPU cache and
        avoiding the cache pollution that would otherwise evict
        useful input data. A \code{_mm_sfence()} is issued at the
        end of each streaming region.
  \item \strong{Hugepage hint for large outputs.} Output vectors
        larger than 512 KB are allocated through
        \code{Rf_allocVector()} and then hinted with
        \code{madvise(MADV_HUGEPAGE)}, so the kernel can back
        them with 2 MB pages. This reduces first-touch page
        faults on the 1e8-row case. There is no custom
        allocator and no free pool.
}

Two fast paths for small inputs are used. Tiny inputs
(\code{n <= 2048}, \code{n_meas <= 64}, \code{n_id <= 8}) route
to \code{melt_tiny_cpp}; small inputs
(\code{total <= 131072}, \code{n_meas <= 256}) route to
\code{melt_small_cpp}. Both skip hugepage hinting, OpenMP setup,
and thread-cap detection, which keeps \code{melt()} the fastest
engine even on tables of a few thousand rows.

The output is identical to \code{reshape2::melt},
\code{data.table::melt}, \code{tidyr::pivot_longer},
\code{collapse::pivot(how = "longer")}, \code{pandas.melt},
\code{polars.unpivot}, \code{dask}, and \code{duckdb UNPIVOT} on
every tested shape.  Since \code{melt()} performs no arithmetic --
it is pure data movement -- the output is bit-identical across
engines.  See
\code{vignette("dataprep-melt-dcast")} for the implementation notes
and \code{vignette("dataprep-performance")} for the full benchmark
tables, including \code{mean}, \code{median}, and the
per-competitor gradient at every tested scale.
}
\value{
A data frame in long format. When \code{na.rm = FALSE}, it has
\code{nrow(data) * length(measure.vars)} rows. With
\code{na.rm = TRUE}, rows whose value is \code{NA} or \code{NaN}
are dropped, so the row count is smaller and not known in advance.
Columns are the ID columns, a column named
\code{variable.name} (a factor when \code{as.factor} resolves to
\code{TRUE}, otherwise a character vector), and a numeric column
named \code{value.name}.

When \code{as.factor = NULL} and \code{major = "row"}, the
\code{variable} column is a \code{character} vector, matching
\code{tidyr::pivot_longer()}.
}
\references{
1. Example data is from \url{https://smear.avaa.csc.fi/download}.
2. Eddelbuettel, D. and Francois, R. (2011). Rcpp: Seamless R and
   C++ Integration. \emph{Journal of Statistical Software}, 40(8),
   1--18.
3. Liang, C.-S., Wu, H., Li, H.-Y., Zhang, Q., Li, Z. & He, K.-B.
   (2020). Efficient data preprocessing, episode classification, and
   source apportionment of particle number concentrations.
   \emph{Science of the Total Environment}, 741, 140923.
   \doi{10.1016/j.scitotenv.2020.140923}
}
\author{
Chun-Sheng Liang \email{chun-shengliang@qq.com}
}
\seealso{
\code{\link{dcast}} for the inverse operation.
\code{vignette("dataprep-melt-dcast")} for usage notes and
implementation.
\code{vignette("dataprep-performance")} for benchmark tables.
}
\examples{
## --- Basic usage --------------------------------------------------

df <- data.frame(id = 1:5,
                 category = factor(letters[1:5]),
                 v1 = rnorm(5), v2 = rnorm(5))

# Automatic ID inference: `id` and `category` are non-numeric
melt(df)

# Explicit ID columns
melt(df, id.vars = c("id", "category"))

# Explicit measure columns
melt(df, measure.vars = c("v1", "v2"))


## --- Custom column names and na.rm --------------------------------

df2 <- data.frame(id = 1:3, x = c(1, NA, 3), y = c(4, 5, NA))

melt(df2, id.vars = "id",
     variable.name = "var", value.name = "val",
     na.rm = TRUE)


## --- Two memory layouts ------------------------------------------

# major = NULL (default): column-major (reshape2-compatible)
# major = "col" : column-major, reshape2-compatible
# major = "row" : row-major,    tidyr-compatible
df_wide <- data.frame(id = 1:100,
                      matrix(rnorm(100 * 50), ncol = 50))

res_default <- melt(df_wide, id.vars = "id")
res_col     <- melt(df_wide, id.vars = "id", major = "col")
res_row     <- melt(df_wide, id.vars = "id", major = "row")

# The two layouts produce the same content in a different order
identical(sort(res_col$value), sort(res_row$value))

## --- as.factor controls the variable column type ------------------

# Default: factor for "col", character for "row"
is.factor(res_col$variable)                      # TRUE
is.character(res_row$variable)                   # TRUE

# Explicit override
res_row_fac <- melt(df_wide, id.vars = "id", major = "row",
                    as.factor = TRUE)
is.factor(res_row_fac$variable)                  # TRUE
}
\keyword{manip}
