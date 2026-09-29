# Cast a long-format data.frame into a wide format

Structural inverse of
[`melt`](https://chunshengliang.github.io/dataprep/reference/melt.md): a
long-format data frame with one row per `(id, variable)` pair is
reshaped into a wide-format data frame with one row per `id` combination
and one column per `variable` level. The C++ backend builds compact
integer lookup tables for both the row keys (id columns) and the column
keys (variable column), then writes values by output column in strictly
sequential order.

## Usage

``` r
dcast(data, id = NULL, formula = NULL,
      variable = NULL, value = NULL, value.var = NULL,
      fill = NA_real_, fun.aggregate = NULL,
      na.rm = FALSE, cores = 0L, verbose = FALSE)
```

## Arguments

- data:

  A data frame in long format. It must contain one column identifying
  the variable (e.g. `"variable"`), one column holding the value (e.g.
  `"value"`), and one or more id columns.

- id:

  Identifier columns. Character names, integer indices, a logical mask,
  or `NULL`. If `NULL`, all columns other than the variable and value
  columns are used as ids.

- formula:

  Optional formula of the form `id1 + id2 ~ variable`. When supplied,
  the left-hand side overrides `id` and the right-hand side overrides
  `variable`.

- variable:

  Name (or index) of the column identifying the variable. If `NULL`, a
  column named `"variable"`, `"variables"`, `"Variable"`, `"Variables"`,
  `"VARIABLE"`, or `"VARIABLES"` is used when exactly one such column
  exists.

- value:

  Name (or index) of the column holding the values. If `NULL`, a column
  named `"value"`, `"values"`, `"Value"`, `"Values"`, `"VALUE"`, or
  `"VALUES"` is used when exactly one such column exists.

- value.var:

  Alias for `value`. Used only when `value` is `NULL`; if both are
  supplied, `value` takes precedence.

- fill:

  Value used to fill cells for `(id, variable)` pairs that do not appear
  in the input. Cells that appear in the input with value `NA` keep `NA`
  unless `na.rm = TRUE`. Default `NA_real_`.

- fun.aggregate:

  Optional function used to reduce duplicate `(id, variable)` pairs.
  Called with a single argument (the vector of values for that pair)
  plus `na.rm = TRUE`. Without it, the last occurrence wins.

- na.rm:

  Logical; if `TRUE`, rows whose value is `NA` or `NaN` are skipped
  during scatter, so the corresponding output cell keeps the `fill`
  value. Default `FALSE`.

- cores:

  Number of OpenMP threads. `0` (default) lets the C++ backend choose
  based on data size.

- verbose:

  Logical; if `TRUE`, prints timing information.

## Details

`dcast()` is the structural inverse of
[`melt`](https://chunshengliang.github.io/dataprep/reference/melt.md).
The C++ backend detects canonical
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
output automatically (the variable column is periodic and every id
column is constant within one period) and switches to a *block-path tile
transpose*: each tile of `TILE x period` doubles is read contiguously
into an L1 buffer, transposed in place, and written contiguously to the
output columns. This keeps both reads and writes sequential and enables
OpenMP parallelisation. The cost per tile is `O(TILE * period)`,
independent of the number of levels, which is why wide-level tables
scale well.

When the input is not block-aligned, `dcast()` builds an open-addressing
hash of 64-bit packed row keys. If the combined bit budget of the id
columns exceeds 64, it falls back to a 96-bit fingerprint (`uint64_t` +
`uint32_t`) computed by a 4-way parallel FNV-1a and stored in a 16-byte
slot table. For shuffle-friendly input, a 4-pass LSD radix sort over the
packed keys replaces the hash table entirely; and when the block path is
taken and the id column is a permutation of `1..n_blocks`, a
direct-index shortcut bypasses both.

The output is identical to
[`reshape2::dcast`](https://rdrr.io/pkg/reshape2/man/cast.html),
[`data.table::dcast`](https://rdrr.io/pkg/data.table/man/dcast.data.table.html),
[`tidyr::pivot_wider`](https://tidyr.tidyverse.org/reference/pivot_wider.html),
`pandas.pivot`, `polars.pivot`, `duckdb PIVOT`, and `dask` on every
tested shape, within `tol = 1e-12`. See
[`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md)
for the implementation notes and
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md)
for the benchmark tables.

On the canonical long-to-wide shape, `dcast()` is faster than every
tested alternative. The speed-up relative to `reshape2`, `data.table`,
`tidyr`, `pandas`, `polars`, `dask`, and `duckdb` spans `2.0x` (against
`reshape2` on 1e3 rows and 10 levels, Ubuntu 25.10) to `677x` (against
`duckdb` on 1e8 rows and 100 levels, Windows 11 Pro for Workstations).
The median across all tested cells and all competitors is `54.0x` on
Ubuntu 25.10 and `41.6x` on Windows 11 Pro for Workstations; the mean is
`96.5x` and `74.3x`, respectively.

## Value

A wide-format data frame with one row per unique `id` combination and
one column per unique value of the variable column, plus the id columns.
Column names are the string representation of the variable values.

## References

Liang, C.-S., Wu, H., Li, H.-Y., Zhang, Q., Li, Z. & He, K.-B. (2020).
Efficient data preprocessing, episode classification, and source
apportionment of particle number concentrations. *Science of the Total
Environment*, 741, 140923.
[doi:10.1016/j.scitotenv.2020.140923](https://doi.org/10.1016/j.scitotenv.2020.140923)

## Author

Chun-Sheng Liang <chun-shengliang@qq.com>

## See also

[`melt`](https://chunshengliang.github.io/dataprep/reference/melt.md)
for the inverse operation.
[`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md)
for usage notes and implementation.

## Examples

``` r
## --- Basic usage --------------------------------------------------

long <- data.frame(
  id       = rep(1:3, each = 2),
  variable = rep(c("x", "y"), 3),
  value    = c(1, 2, 3, 4, 5, 6)
)
dcast(long, id = "id", variable = "variable", value = "value")
#>   id x y
#> 1  1 1 2
#> 2  2 3 4
#> 3  3 5 6


## --- Formula interface --------------------------------------------

dcast(long, formula = id ~ variable)
#>   id x y
#> 1  1 1 2
#> 2  2 3 4
#> 3  3 5 6


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
#>   year city temp rain
#> 1 2020    A 15.2  210
#> 2 2020    B 14.8  180
#> 3 2021    A 16.1  230
#> 4 2021    B 15.5  195

# Formula form (equivalent)
dcast(long2, formula = year + city ~ variable)
#>   year city temp rain
#> 1 2020    A 15.2  210
#> 2 2020    B 14.8  180
#> 3 2021    A 16.1  230
#> 4 2021    B 15.5  195


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
#>   id x y
#> 1  1 1 2
#> 2  2 3 0


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
#>   id   x
#> 1  1 1.5
#> 2  2 3.0

# sum and length work the same way
dcast(long4, id = "id",
      variable = "variable", value = "value",
      fun.aggregate = sum)
#>   id x
#> 1  1 3
#> 2  2 3


## --- Skip NA values during scatter --------------------------------

long5 <- data.frame(
  id       = c(1, 1, 2, 2),
  variable = c("x", "y", "x", "y"),
  value    = c(1, NA, 3, 4)
)

# Default: NA stays as NA
dcast(long5, id = "id",
      variable = "variable", value = "value")
#>   id x  y
#> 1  1 1 NA
#> 2  2 3  4

# na.rm = TRUE: (1, "y") is skipped, cell keeps the fill value
dcast(long5, id = "id",
      variable = "variable", value = "value",
      na.rm = TRUE, fill = -1)
#>   id x  y
#> 1  1 1 -1
#> 2  2 3  4


## --- Round-trip with melt() ---------------------------------------

wide <- data.frame(id = 1:3, a = c(1.5, 2.5, 3.5), b = c(4.5, 5.5, 6.5))
back <- dcast(melt(wide, id.vars = "id"),
              id = "id", variable = "variable", value = "value")
back[order(back$id), ]
#>   id   a   b
#> 1  1 1.5 4.5
#> 2  2 2.5 5.5
#> 3  3 3.5 6.5


## --- Larger example: 1000 rows, 50 levels -------------------------

set.seed(1)
n_rows   <- 1000L
n_levels <- 50L
long_big <- data.frame(
  id       = rep(seq_len(n_rows / n_levels), each = n_levels),
  variable = rep(sprintf("v%02d", seq_len(n_levels)),
                 times = n_rows / n_levels),
  value    = rnorm(n_rows)
)
wide_big <- dcast(long_big, id = "id",
                  variable = "variable", value = "value")
dim(wide_big)
#> [1] 20 51
```
