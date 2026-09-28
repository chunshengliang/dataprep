# Data preprocessing with multiple steps in one function

Performs variable deletion (`varidele`), observation deletion
(`obsedele`), conditional extremum outlier removal (`condextr`), and
short-period interpolation (`shorvalu`) in sequence.

## Usage

``` r
dataprep(data, cols = NULL, group = NULL, optimal = FALSE,
interval = 10, times = 10, fraction = 0.25,
top = 0.995, top.error = 0.1, top.magnitude = 0.2,
bottom = 0.0025, bottom.error = 0.2, bottom.magnitude = 0.4, by = "min",
half = 30, intervals = 30, date_col = NULL, cores = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame containing numeric columns and optionally a grouping
  column.

- cols:

  Column indices or names of numeric variables to process.

- group:

  Grouping column index or name.

- optimal:

  Logical; if `TRUE`, `optisolu` is used to find optimal `interval` and
  `times`.

- interval:

  Number of outlier-marking rounds between two observation-deletion
  rounds inside `condextr`.

- times:

  Number of observation-deletion rounds in `condextr`.

- fraction:

  Missing proportion threshold for variable deletion.

- top, top.error, top.magnitude, bottom, bottom.error, bottom.magnitude:

  Outlier removal parameters.

- by:

  Time unit for observation deletion. An invalid unit string raises an
  error.

- half:

  Half window size in minutes for observation deletion.

- intervals:

  Time gap for interpolation periods.

- date_col:

  Time column index or name. If `NULL`, automatically detected.

- cores:

  Number of CPU cores passed to
  [`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.md),
  [`condextr()`](https://chunshengliang.github.io/dataprep/reference/condextr.md),
  and
  [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md).
  `NULL` (default) lets each backend choose based on data size.

- verbose:

  Logical; if `TRUE`, prints timing and deletion/interpolation counts.

## Value

A preprocessed data frame.

## References

1\. Example data is from <https://smear.avaa.csc.fi/download>. It
includes particle number concentrations in SMEAR I Varrio forest.

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
dataprep(data[1:60, c(1, 4, 18:19)], cols = 3:4, group = 2, interval = 2, times = 1, cores = 1)
#>                   date    monthyear       4.47      5.01
#> 1  2019-12-31 16:00:00 January 2020  74.778500  24.83750
#> 2  2019-12-31 16:10:00 January 2020  74.778500  24.83750
#> 3  2019-12-31 16:20:00 January 2020  74.778500  24.83750
#> 4  2019-12-31 16:30:00 January 2020  74.778500  24.83750
#> 5  2019-12-31 16:40:00 January 2020   6.865100  44.71760
#> 6  2019-12-31 16:50:00 January 2020   6.810320  44.36072
#> 7  2019-12-31 17:00:00 January 2020   6.755540  44.00384
#> 8  2019-12-31 17:10:00 January 2020   6.700760  43.64696
#> 9  2019-12-31 17:20:00 January 2020   6.645980  43.29008
#> 10 2019-12-31 17:30:00 January 2020   6.591200  42.93320
#> 11 2019-12-31 17:40:00 January 2020   9.044067  58.91050
#> 12 2019-12-31 17:50:00 January 2020  11.496933  74.88780
#> 13 2019-12-31 18:00:00 January 2020  13.949800  90.86510
#> 14 2019-12-31 18:10:00 January 2020  85.265200  85.80843
#> 15 2019-12-31 18:20:00 January 2020 156.580600  80.75177
#> 16 2019-12-31 18:30:00 January 2020 227.896000  75.69510
#> 17 2019-12-31 18:40:00 January 2020 207.972000  69.07735
#> 18 2019-12-31 18:50:00 January 2020 188.048000  62.45960
#> 19 2019-12-31 19:00:00 January 2020 168.124000  55.84185
#> 20 2019-12-31 19:10:00 January 2020 148.200000  49.22410
#> 21 2019-12-31 19:20:00 January 2020 135.934117  45.15003
#> 22 2019-12-31 19:40:00 January 2020 123.668233  41.07597
#> 23 2019-12-31 19:50:00 January 2020 111.402350  37.00190
#> 24 2019-12-31 20:00:00 January 2020  99.136467  32.92783
#> 25 2019-12-31 20:10:00 January 2020  86.870583  28.85377
#> 26 2019-12-31 20:20:00 January 2020  74.604700  24.77970
#> 27 2019-12-31 20:30:00 January 2020  74.778500  24.83750
#> 28 2019-12-31 20:40:00 January 2020  40.999250  35.93335
#> 29 2019-12-31 20:50:00 January 2020   7.220000  47.02920
#> 30 2019-12-31 21:00:00 January 2020  39.688250  35.49790
#> 31 2019-12-31 21:10:00 January 2020  72.156500  23.96660
#> 32 2019-12-31 21:20:00 January 2020  75.611000  64.48590
#> 33 2019-12-31 21:30:00 January 2020  72.500300  24.08080
#> 34 2019-12-31 21:40:00 January 2020  73.668120  24.46868
#> 35 2019-12-31 21:50:00 January 2020  74.835940  24.85656
#> 36 2019-12-31 22:00:00 January 2020  76.003760  25.24444
#> 37 2019-12-31 22:10:00 January 2020  77.171580  25.63232
#> 38 2019-12-31 22:20:00 January 2020  78.339400  26.02020
#> 39 2019-12-31 22:30:00 January 2020 114.422700  58.60465
#> 40 2019-12-31 22:40:00 January 2020 150.506000  91.18910
#> 41 2019-12-31 22:50:00 January 2020 150.506000  91.18910
#> 42 2019-12-31 23:00:00 January 2020 150.506000  91.18910
#> 43 2019-12-31 23:10:00 January 2020 150.506000  91.18910
#> 44 2020-01-01 00:10:00 January 2020  76.502400 111.10200
#> 45 2020-01-01 00:20:00 January 2020  76.502400 111.10200
#> 46 2020-01-01 00:30:00 January 2020  76.502400 111.10200
#> 47 2020-01-01 00:40:00 January 2020  76.502400 111.10200
#> 48 2020-01-01 00:50:00 January 2020  76.434925  89.65660
#> 49 2020-01-01 01:00:00 January 2020  76.367450  68.21120
#> 50 2020-01-01 01:10:00 January 2020  76.299975  46.76580
#> 51 2020-01-01 01:20:00 January 2020  76.232500  25.32040
#> 52 2020-01-01 01:30:00 January 2020   6.607000  43.03640
#> 53 2020-01-01 01:40:00 January 2020   6.974900  45.43250
#> 54 2020-01-01 01:50:00 January 2020   6.974900  45.43250
```
