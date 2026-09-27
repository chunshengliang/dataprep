# Diagnose missing value patterns in data

Provides a summary of missing values for selected columns, including the
number and fraction of missing values, the number of consecutive missing
runs, and the longest consecutive missing run length (in number of
observations).

## Usage

``` r
na_diagnose(data, cols = NULL, date_col = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame or matrix containing variables to diagnose.

- cols:

  The column indices or names of selected variables. If `NULL`, all
  numeric columns are used.

- date_col:

  Reserved for future use; currently ignored.

- verbose:

  Logical; if `TRUE`, prints progress messages.

## Value

A data frame with columns: `variable`, `n` (total observations), `na`
(number of missing values), `na_frac` (missing fraction), `na_runs`
(number of consecutive missing runs), `max_run` (longest consecutive
missing run length in number of observations).

## Examples

``` r
na_diagnose(data[1:500, c(1, 4, 17:19)], cols = 3:5)
#>   variable   n  na na_frac na_runs max_run
#> 1     3.98 500 351   0.702      82      26
#> 2     4.47 500 312   0.624      70      21
#> 3     5.01 500 312   0.624      70      21
```
