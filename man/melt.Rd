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
  \item{major}{Memory layout. \code{NULL} (default) lets the C++
    backend auto-select based on the input shape: column-major
    (\code{"col"}, reshape2-compatible) when the number of value
    columns is large, row-major (\code{"row"}, tidyr-compatible)
    otherwise. Passing \code{"row"} or \code{"col"} forces the
    corresponding layout.}
  \item{verbose}{Logical; if \code{TRUE}, prints timing messages.}
  \item{parallel_threshold}{Minimum number of output elements before
    automatic parallelism is enabled. Default \code{5e6}.}
  \item{id.vars}{Alias of \code{id}, provided for reshape2 / data.table
    compatibility. Supplying both is an error.}
}
\details{
\code{melt()} is the fast, drop-in replacement for
\code{reshape2::melt} and \code{tidyr::pivot_longer}. The C++
backend (\code{melt_cpp}) has been benchmarked against \strong{all
seven major alternatives} in the R and Python ecosystems:
\code{reshape2}, \code{data.table}, \code{tidyr}, \code{pandas},
\code{polars}, \code{dask}, and \code{duckdb}. Every cell is
measured with \code{microbenchmark} using an adaptive \code{times}
rule.

\strong{Speed-up relative to each competitor spans
\code{1.03x} to \code{2664x}.} The median across all tested cells
and all competitors is above \code{30x}. The largest gaps appear on
wide tables (many value columns, few rows); the smallest gaps
appear on small tables where initialization overhead dominates.

Representative cells (medians; numbers in parentheses are the
speed-up of \code{melt()} relative to that competitor):

\itemize{
  \item \strong{1e6 rows, 1 id, 9 value columns} ---
        \code{4.33 ms} vs \code{44.8 ms} (\code{data.table},
        \code{10x}), \code{309 ms} (\code{tidyr}, \code{71x}),
        \code{729 ms} (\code{duckdb}, \code{168x}).
  \item \strong{1e7 rows, 1 id, 9 value columns} ---
        \code{21.2 ms} vs \code{549 ms} (\code{data.table},
        \code{26x}), \code{1,424 ms} (\code{tidyr}, \code{67x}),
        \code{6,958 ms} (\code{duckdb}, \code{328x}).
  \item \strong{1e3 rows, 1 id, 10000 value columns} ---
        \code{5.21 ms} vs \code{48.6 ms} (\code{data.table},
        \code{9.3x}), \code{59.2 ms} (\code{polars}, \code{11.4x}),
        \code{13,865 ms} (\code{dask}, \strong{\code{2664x}}).
  \item \strong{1e3 rows, 10 id, 10000 value columns} ---
        \code{24.8 ms} vs \code{102 ms} (\code{polars},
        \code{4.1x}), \code{380 ms} (\code{data.table},
        \code{15x}), \code{17,085 ms} (\code{dask},
        \strong{\code{688x}}).
}

The speed-up comes from three design choices:

\enumerate{
  \item \strong{Two layout paths chosen automatically.} On the
        column-major path, each output column is written as one
        contiguous \code{memcpy} of the input column --- the fastest
        possible pattern. On the row-major path, tiles of eight
        rows are transposed in registers with
        \code{_mm512_shuffle_f64x2}.
  \item \strong{SIMD streaming stores.} For large outputs,
        \code{_mm512_stream_pd} and \code{_mm256_stream_si256}
        write directly to memory, bypassing the CPU cache and
        avoiding the cache pollution that would otherwise evict
        useful input data. A \code{_mm_sfence()} is issued at the
        end of each streaming region.
  \item \strong{Hugepage allocator with a free pool.} Output
        matrices larger than 512 KB are allocated through a custom
        \code{R_allocator_t} that maps anonymous memory with
        \code{MADV_HUGEPAGE}. Freed regions are returned to a
        32 GB / 512-entry pool instead of being \code{munmap}'d
        immediately, so the next allocation in the same process
        can reuse the same physical pages without a page-fault
        storm.
}

A fast path for small inputs (\code{total <= 200000} and
\code{n_meas <= 128}) routes to a dedicated backend that skips
hugepage mapping, OpenMP setup, and thread-cap detection, which
keeps \code{melt()} the fastest engine even on tables of a few
thousand rows.

The output is identical to \code{reshape2::melt},
\code{data.table::melt}, \code{tidyr::pivot_longer},
\code{pandas.melt}, \code{polars.unpivot}, \code{dask}, and
\code{duckdb UNPIVOT} on every tested shape, within
\code{tol = 1e-12}. See \code{vignette("dataprep-melt-dcast")} for
the implementation notes and
\code{vignette("dataprep-performance")} for the full benchmark
tables, including \code{mean}, \code{median}, and the per-competitor
gradient at every tested scale.
}
\value{
A data frame in long format. When \code{na.rm = FALSE}, it has
\code{nrow(data) * length(measure.vars)} rows. With
\code{na.rm = TRUE}, rows whose value is \code{NA} or \code{NaN}
are dropped, so the row count is smaller and not known in advance.
Columns are the ID columns, a factor column named
\code{variable.name}, and a numeric column named
\code{value.name}.
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
Chun-Sheng Liang <liangchunsheng@lzu.edu.cn>
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

# major = NULL (default): auto-select based on shape
# major = "col" : column-major, reshape2-compatible
# major = "row" : row-major,    tidyr-compatible
df_wide <- data.frame(id = 1:100,
                      matrix(rnorm(100 * 50), ncol = 50))

res_auto <- melt(df_wide, id.vars = "id")
res_col  <- melt(df_wide, id.vars = "id", major = "col")
res_row  <- melt(df_wide, id.vars = "id", major = "row")

# The two layouts produce the same content in a different order
identical(sort(res_col$value), sort(res_row$value))
}
\keyword{manip}