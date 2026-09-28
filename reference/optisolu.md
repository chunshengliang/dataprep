# Find optimal combination of interval and times for condextr

Searches over a grid of `interval` and `times` values to find the
combination that yields better outlier removal than the traditional
percentile method, based on sample deletion ratio (SDR), outlier removal
ratio (ORR), and signal-to-noise ratio (SNR).

## Usage

``` r
optisolu(data, cols = NULL, group = NULL, interval = 35, times = 10,
top = 0.995, top.error = 0.1, top.magnitude = 0.2,
bottom = 0.0025, bottom.error = 0.2, bottom.magnitude = 0.4,
by = "min", half = 30, date_col = NULL, cores = NULL,
verbose = FALSE)
```

## Arguments

- data:

  A data frame containing numeric columns and optionally a grouping
  column.

- cols:

  Column indices or names of numeric variables.

- group:

  Grouping column index or name.

- interval:

  Maximum interval value to test (from 1 to `interval`).

- times:

  Maximum times value to test (from 1 to `times`).

- top:

  Top percentile threshold.

- top.error:

  Error margin for the top threshold.

- top.magnitude:

  Magnitude margin for the top threshold.

- bottom:

  Bottom percentile threshold.

- bottom.error:

  Error margin for the bottom threshold.

- bottom.magnitude:

  Magnitude margin for the bottom threshold.

- by:

  Time unit used when computing observation deletion.

- half:

  Half window size in minutes for observation deletion.

- date_col:

  Time column index or name.

- cores:

  Number of CPU cores.

- verbose:

  Logical; if `TRUE`, prints progress message.

## Details

This function can be computationally intensive. It compares
[`condextr`](https://chunshengliang.github.io/dataprep/reference/condextr.md)
results with
[`percoutl`](https://chunshengliang.github.io/dataprep/reference/percoutl.md)
results.

## Value

A data frame with columns: `case`, `interval`, `times`, `sdr`, `orr`,
`snr`, `index`, `relaindex`, and `optimal` (logical).

## References

1\. Example data is from <https://smear.avaa.csc.fi/download>. It
includes particle number concentrations in SMEAR I Varrio forest.

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
# \donttest{
optisolu(data[1:50, c(1, 4, 18:19)], cols = 3:4, group = 2,
         interval = 2, times = 1)
#>   case interval times  sdr orr      snr    index relaindex optimal
#> 1    1        1     1 0.14   0 1.554262 23.74734  34.26017   FALSE
#> 2    2        2     1 0.14   0 1.554262 23.74734  21.61576   FALSE
# }
```
