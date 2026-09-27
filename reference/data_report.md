# Generate a simple data quality report

Prints a summary of data dimensions, variable types, missing value
patterns, and basic descriptive statistics for numeric columns. The
report is printed to the console and also returned invisibly as a list.

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

  Optional time column for time gap analysis.

- verbose:

  Logical; if `TRUE`, prints progress and timing messages.

## Value

Invisibly returns a list containing data dimensions, variable types,
missing value diagnosis, and descriptive statistics.

## References

1\. Example data is from https://smear.avaa.csc.fi/download. It includes
particle number concentrations in SMEAR I Varrio forest.

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
data_report(data[1:200, c(1, 4, 17:19)], cols = 3:5, date_col = 1)
```
