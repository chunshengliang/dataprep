# Generate a simple data quality report

Computes a data quality summary and returns it invisibly as a list. When
`verbose = TRUE`, the summary is also printed to the console.

## Usage

``` r
data_report(data, cols = NULL, date_col = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame.

- cols:

  Optional column indices or names for the numeric columns to include in
  the report. If NULL, all numeric columns are used.

- date_col:

  Reserved for future use; currently ignored (passed through to
  [`na_diagnose`](https://chunshengliang.github.io/dataprep/reference/na_diagnose.md)).

- verbose:

  Logical; if `TRUE`, prints progress and timing messages.

## Value

Invisibly returns a list containing data dimensions, variable types,
missing value diagnosis, and descriptive statistics.

## References

1\. Example data is from <https://smear.avaa.csc.fi/download>. It
includes particle number concentrations in SMEAR I Varrio forest.

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
data_report(data[1:200, c(1, 4, 17:19)], cols = 3:5, date_col = 1)
```
