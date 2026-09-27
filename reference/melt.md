# Fast wide-to-long data reshaping with flexible ID/measure specification

Transforms a wide-format data frame into a long-format data frame using
a C++ backend with SIMD and optional OpenMP parallelism. Supports
automatic inference of ID columns, user-specified ID or measure
variables, custom column names, and two storage layouts (row-major or
column-major).

## Usage

``` r
melt(data, id = NULL, measure.vars = NULL,
     variable.name = "variable", value.name = "value",
     na.rm = FALSE, cores = NULL, major = NULL,
     verbose = FALSE, parallel_threshold = 5e6, id.vars = NULL)
```

## Arguments

- data:

  A data frame to reshape. Numeric, integer, logical, character, and
  factor columns are supported.

- id:

  Identifier columns. Character names, integer indices, a logical mask,
  or `NULL`. If `NULL` and `measure.vars` is also `NULL`, ID columns are
  inferred automatically: non-numeric, non-integer, non-logical columns
  and factors are treated as IDs.

- measure.vars:

  Measure columns. Character names, integer indices, a logical mask, or
  `NULL`. If supplied, all other columns become IDs. Only one of `id`
  and `measure.vars` should be supplied.

- variable.name:

  Name of the new factor column that stores the original column names of
  the measure variables. Default `"variable"`.

- value.name:

  Name of the new numeric column that stores the melted values. Default
  `"value"`.

- na.rm:

  Logical; if `TRUE`, rows where the value column is `NA` are removed.
  Default `FALSE`.

- cores:

  Number of OpenMP threads. `NULL` (default) lets the C++ backend pick a
  value based on data size. The global option `dataprep.cores` overrides
  the automatic choice.

- major:

  Memory layout. `NULL` (default) lets the C++ backend auto-select based
  on the input shape: column-major (`"col"`, reshape2-compatible) when
  the number of value columns is large, row-major (`"row"`,
  tidyr-compatible) otherwise. Passing `"row"` or `"col"` forces the
  corresponding layout.

- verbose:

  Logical; if `TRUE`, prints timing messages.

- parallel_threshold:

  Minimum number of output elements before automatic parallelism is
  enabled. Default `5e6`.

- id.vars:

  Alias of `id`, provided for reshape2 / data.table compatibility.
  Supplying both is an error.

## Details

`melt()` is the fast, drop-in replacement for
[`reshape2::melt`](https://rdrr.io/pkg/reshape2/man/melt.html) and
[`tidyr::pivot_longer`](https://tidyr.tidyverse.org/reference/pivot_longer.html).
The C++ backend (`melt_cpp`) has been benchmarked against **all seven
major alternatives** in the R and Python ecosystems: `reshape2`,
`data.table`, `tidyr`, `pandas`, `polars`, `dask`, and `duckdb`. Every
cell is measured with `microbenchmark` using an adaptive `times` rule.

**Speed-up relative to each competitor spans `0.5x` to `1187x`.** The
median across all tested cells and all competitors is `10.3x` on Ubuntu
25.10 and `5.7x` on Windows 11 Pro for Workstations; the mean is `58.5x`
and `44.2x`, respectively. The largest gaps appear on wide tables (many
value columns, few rows); the smallest gaps appear on small tables where
initialization overhead dominates.

Representative cells (medians; numbers in parentheses are the speed-up
of `melt()` relative to that competitor) taken from the Ubuntu 25.10
host:

- **1e6 rows, 1 id, 9 value columns** — `3.68 ms` vs `7.90 ms`
  (`data.table`, `2.1x`), `76.8 ms` (`tidyr`, `20.9x`), `642 ms`
  (`duckdb`, `174x`).

- **1e7 rows, 1 id, 9 value columns** — `37.9 ms` vs `372 ms`
  (`data.table`, `9.8x`), `1111 ms` (`tidyr`, `29.3x`), `6423 ms`
  (`duckdb`, `169x`).

- **1e3 rows, 1 id, 10000 value columns** — `3.62 ms` vs `9.66 ms`
  (`data.table`, `2.7x`), `16.3 ms` (`polars`, `4.5x`), `4292 ms`
  (`dask`, **`1187x`**).

- **1e3 rows, 10 id, 10000 value columns** — `25.2 ms` vs `59.4 ms`
  (`polars`, `2.4x`), `169 ms` (`data.table`, `6.7x`), `22456 ms`
  (`dask`, **`891x`**).

The speed-up comes from three design choices:

1.  **Two layout paths chosen automatically.** On the column-major path,
    each output column is written as one contiguous `memcpy` of the
    input column — the fastest possible pattern. On the row-major path,
    tiles of eight rows are transposed in registers with
    `_mm512_shuffle_f64x2`.

2.  **SIMD streaming stores.** For large outputs, `_mm512_stream_pd` and
    `_mm256_stream_si256` write directly to memory, bypassing the CPU
    cache and avoiding the cache pollution that would otherwise evict
    useful input data. A `_mm_sfence()` is issued at the end of each
    streaming region.

3.  **Hugepage hint for large outputs.** Output vectors larger than 512
    KB are allocated through `Rf_allocVector()` and then hinted with
    `madvise(MADV_HUGEPAGE)`, so the kernel can back them with 2 MB
    pages. This reduces first-touch page faults on the 1e8-row case.
    There is no custom allocator and no free pool.

Two fast paths for small inputs are used. Tiny inputs (`n <= 2048`,
`n_meas <= 64`, `n_id <= 8`) route to `melt_tiny_cpp`; small inputs
(`total <= 131072`, `n_meas <= 256`) route to `melt_small_cpp`. Both
skip hugepage hinting, OpenMP setup, and thread-cap detection, which
keeps `melt()` the fastest engine even on tables of a few thousand rows.

The output is identical to
[`reshape2::melt`](https://rdrr.io/pkg/reshape2/man/melt.html),
[`data.table::melt`](https://rdrr.io/pkg/data.table/man/melt.data.table.html),
[`tidyr::pivot_longer`](https://tidyr.tidyverse.org/reference/pivot_longer.html),
`pandas.melt`, `polars.unpivot`, `dask`, and `duckdb UNPIVOT` on every
tested shape, within `tol = 1e-12`. See
[`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md)
for the implementation notes and
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md)
for the full benchmark tables, including `mean`, `median`, and the
per-competitor gradient at every tested scale.

## Value

A data frame in long format. When `na.rm = FALSE`, it has
`nrow(data) * length(measure.vars)` rows. With `na.rm = TRUE`, rows
whose value is `NA` or `NaN` are dropped, so the row count is smaller
and not known in advance. Columns are the ID columns, a factor column
named `variable.name`, and a numeric column named `value.name`.

## References

1\. Example data is from <https://smear.avaa.csc.fi/download>. 2.
Eddelbuettel, D. and Francois, R. (2011). Rcpp: Seamless R and C++
Integration. *Journal of Statistical Software*, 40(8), 1–18. 3. Liang,
C.-S., Wu, H., Li, H.-Y., Zhang, Q., Li, Z. & He, K.-B. (2020).
Efficient data preprocessing, episode classification, and source
apportionment of particle number concentrations. *Science of the Total
Environment*, 741, 140923.
[doi:10.1016/j.scitotenv.2020.140923](https://doi.org/10.1016/j.scitotenv.2020.140923)

## Author

Chun-Sheng Liang <chun-shengliang@qq.com>

## See also

[`dcast`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
for the inverse operation.
[`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md)
for usage notes and implementation.
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md)
for benchmark tables.

## Examples

``` r
## --- Basic usage --------------------------------------------------

df <- data.frame(id = 1:5,
                 category = factor(letters[1:5]),
                 v1 = rnorm(5), v2 = rnorm(5))

# Automatic ID inference: `id` and `category` are non-numeric
melt(df)
#>    category variable      value
#> 1         a       id  1.0000000
#> 2         b       id  2.0000000
#> 3         c       id  3.0000000
#> 4         d       id  4.0000000
#> 5         e       id  5.0000000
#> 6         a       v1  0.8500435
#> 7         b       v1 -0.9253130
#> 8         c       v1  0.8935812
#> 9         d       v1 -0.9410097
#> 10        e       v1  0.5389521
#> 11        a       v2 -0.1819744
#> 12        b       v2  0.8917676
#> 13        c       v2  1.3292082
#> 14        d       v2 -0.1034661
#> 15        e       v2  0.6150646

# Explicit ID columns
melt(df, id.vars = c("id", "category"))
#>    id category variable      value
#> 1   1        a       v1  0.8500435
#> 2   2        b       v1 -0.9253130
#> 3   3        c       v1  0.8935812
#> 4   4        d       v1 -0.9410097
#> 5   5        e       v1  0.5389521
#> 6   1        a       v2 -0.1819744
#> 7   2        b       v2  0.8917676
#> 8   3        c       v2  1.3292082
#> 9   4        d       v2 -0.1034661
#> 10  5        e       v2  0.6150646

# Explicit measure columns
melt(df, measure.vars = c("v1", "v2"))
#>    id category variable      value
#> 1   1        a       v1  0.8500435
#> 2   2        b       v1 -0.9253130
#> 3   3        c       v1  0.8935812
#> 4   4        d       v1 -0.9410097
#> 5   5        e       v1  0.5389521
#> 6   1        a       v2 -0.1819744
#> 7   2        b       v2  0.8917676
#> 8   3        c       v2  1.3292082
#> 9   4        d       v2 -0.1034661
#> 10  5        e       v2  0.6150646


## --- Custom column names and na.rm --------------------------------

df2 <- data.frame(id = 1:3, x = c(1, NA, 3), y = c(4, 5, NA))

melt(df2, id.vars = "id",
     variable.name = "var", value.name = "val",
     na.rm = TRUE)
#>   id var val
#> 1  1   x   1
#> 2  3   x   3
#> 3  1   y   4
#> 4  2   y   5


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
#> [1] TRUE
```
