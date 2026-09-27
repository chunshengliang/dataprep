# Delete observations with excessive consecutive missing values

Delete observations with excessive consecutive missing values

## Usage

``` r
obsedele(
  data,
  cols = NULL,
  group = NULL,
  by = "min",
  half = 30,
  date_col = NULL,
  cores = NULL,
  verbose = FALSE
)
```

## Arguments

- data:

  A data frame.

- cols:

  Columns to check. If `NULL`, all numeric columns are used.

- group:

  Optional grouping column.

- by:

  Time unit (e.g. `"min"`, `"5 min"`, `"hour"`).

- half:

  Half window size in minutes.

- date_col:

  Time column.

- cores:

  Number of CPU cores.

- verbose:

  Logical.

## Value

A data frame with rows removed.

## Details

For every missing value in each selected column, the C++ backend
computes the time distance to the nearest non-missing anchor on the left
and on the right. A row is deleted when **any** selected column has
**both** distances exceed `half` minutes. When a run touches the series
boundary, the missing side is treated as `+Inf`, so boundary rows are
only deleted when the surviving side is also too far away.

This is a change from dataprep 0.1.5, which collapsed all selected
columns into one long vector before computing missing runs. The old
approach merged NA runs across columns and over-deleted boundary rows.
See
[`vignette("dataprep-migration")`](https://chunshengliang.github.io/dataprep/articles/dataprep-migration.md)
for the upgrade guide.

## Boundary behaviour

The comparison is inclusive: if an anchor is exactly `half` minutes
away, the row is retained. On the SMEAR I Varrio 2025 full-year dataset
this rule retains three rows that 0.1.5 removed.

## References

1\. Example data is from <https://smear.avaa.csc.fi/download>. It
includes particle number concentrations in SMEAR I Varrio forest.

## See also

[`varidele`](https://chunshengliang.github.io/dataprep/reference/varidele.md),
[`condextr`](https://chunshengliang.github.io/dataprep/reference/condextr.md),
[`na_diagnose`](https://chunshengliang.github.io/dataprep/reference/na_diagnose.md),
[`dataprep`](https://chunshengliang.github.io/dataprep/reference/dataprep.md).

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
df <- data.frame(
  date  = as.POSIXct("2024-01-01 00:00:00", tz = "UTC") + 0:9 * 600,
  group = rep(1L, 10),
  x     = c(1, NA, NA, NA, 5, NA, NA, 2, NA, 3)
)
obsedele(df, cols = "x", group = "group", half = 30)
#>                   date group  x
#> 1  2024-01-01 00:00:00     1  1
#> 2  2024-01-01 00:10:00     1 NA
#> 3  2024-01-01 00:20:00     1 NA
#> 4  2024-01-01 00:30:00     1 NA
#> 5  2024-01-01 00:40:00     1  5
#> 6  2024-01-01 00:50:00     1 NA
#> 7  2024-01-01 01:00:00     1 NA
#> 8  2024-01-01 01:10:00     1  2
#> 9  2024-01-01 01:20:00     1 NA
#> 10 2024-01-01 01:30:00     1  3
```
