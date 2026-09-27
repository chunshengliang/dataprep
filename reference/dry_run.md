# Simulate preprocessing and report changes without modifying data

Simulates the effect of one or more preprocessing steps on a dataset and
returns a report describing which variables would be removed, how many
observations might be deleted, and other changes. The input data are not
altered.

## Usage

``` r
dry_run(data, steps = c("varidele", "obsedele", "outlier"),
        cols = NULL, group = NULL, date_col = NULL,
        fraction = 0.25, top = 0.995, bottom = 0.0025,
        by = "min", half = 30, method_outlier = "iqr", coef = 1.5,
        verbose = FALSE)
```

## Arguments

- data:

  A data frame to be simulated.

- steps:

  Character vector of steps to simulate. Currently supported:
  `"varidele"`, `"obsedele"`, and `"outlier"`.

- cols:

  Column indices or names of numeric variables to consider. If `NULL`,
  all numeric columns are used.

- group:

  Optional grouping column for outlier detection.

- date_col:

  Optional time column for observation deletion.

- fraction:

  Missing fraction threshold for variable deletion.

- top:

  Top percentile for outlier detection (percentile method).

- bottom:

  Bottom percentile for outlier detection (percentile method).

- by:

  Time unit for consecutive missing deletion (see `obsedele`).

- half:

  Half window size for consecutive missing deletion.

- method_outlier:

  Outlier detection method: `"iqr"`, `"mad"`, or `"percentile"`.

- coef:

  Coefficient for IQR or MAD outlier detection.

- verbose:

  Logical; if `TRUE`, prints progress message.

## Value

A list with the following components:

- `original_n`: number of rows in the original data.

- `original_ncol`: number of columns in the original data.

- `varidele`: a list containing removed column names and count (if step
  included).

- `obsedele`: a list with `rows_before`, `rows_after`, and `removed`.

- `outlier`: a list with `na_before`, `na_after`, and `added`.

- `final_n`: number of rows remaining after simulation, reflecting both
  variable deletion and observation deletion.

- `final_ncol`: number of columns remaining after simulation.

## Details

This function is useful for understanding the potential impact of
preprocessing without changing the dataset. It actually runs `varidele`,
`obsedele`, and `detect_outliers` on a copy of the input, and returns
precise per-step before/after counts. The caller's data frame is never
modified.

## Examples

``` r
dry_run(data[1:200, c(1, 4, 17:19)], cols = 3:5, steps = c("varidele", "obsedele", "outlier"))
#> $varidele
#> $varidele$removed_columns
#> [1] "3.98" "4.47" "5.01"
#> 
#> $varidele$removed_count
#> [1] 3
#> 
#> 
#> $obsedele
#> $obsedele$rows_before
#> [1] 200
#> 
#> $obsedele$rows_after
#> [1] 200
#> 
#> $obsedele$removed
#> [1] 0
#> 
#> $obsedele$note
#> [1] "Skipped: no numeric columns remain."
#> 
#> 
#> $outlier
#> $outlier$na_before
#> [1] 0
#> 
#> $outlier$na_after
#> [1] 0
#> 
#> $outlier$added
#> [1] 0
#> 
#> $outlier$note
#> [1] "Skipped: no numeric columns remain."
#> 
#> 
#> $original_n
#> [1] 200
#> 
#> $original_ncol
#> [1] 5
#> 
#> $final_n
#> [1] 200
#> 
#> $final_ncol
#> [1] 2
#> 
```
