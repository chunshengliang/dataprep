# Detect outliers using multiple methods

Identifies outliers in selected columns using one of several methods:
IQR-based, Median Absolute Deviation (MAD), or percentile-based. The
function can either return a logical mask indicating outlier positions
or replace outliers with NA.

## Usage

``` r
detect_outliers(data, cols = NULL, method = "iqr",
                top = 0.995, bottom = 0.0025, coef = 1.5,
                group = NULL, mask_only = TRUE, verbose = FALSE)
```

## Arguments

- data:

  A data frame, matrix, or numeric vector.

- cols:

  The column indices or names of selected variables. If NULL, all
  columns are used.

- method:

  Detection method. One of "iqr" (default), "mad", "percentile".

- top:

  The top percentile threshold for percentile method.

- bottom:

  The bottom percentile threshold for percentile method.

- coef:

  The coefficient for IQR or MAD method. For IQR, values beyond Q1 -
  coef\*IQR and Q3 + coef\*IQR are outliers. For MAD, values with \|z\|
  \> coef are outliers.

- group:

  Optional grouping column for group-wise detection.

- mask_only:

  Logical. If TRUE (default), returns a logical matrix of outlier
  positions. If FALSE, returns data with outliers replaced by NA.

- verbose:

  Logical; if `TRUE`, prints progress message.

## Details

The IQR method uses Tukey's fences: values outside \[Q1 - coef\*IQR,
Q3 + coef\*IQR\] are considered outliers. The MAD method uses robust
z-scores: \|0.6745\*(x - median)/MAD\| \> coef. The percentile method
flags values above the top percentile or below the bottom percentile.

## Value

If mask_only = TRUE, a logical matrix with TRUE indicating outliers. If
mask_only = FALSE, a data frame with outliers set to NA.

## Examples

``` r
# Return mask
mask <- detect_outliers(data[1:100, c(1, 4, 17:19)], cols = 3:5, method = "iqr")
# Replace outliers with NA
cleaned <- detect_outliers(data[1:100, c(1, 4, 17:19)], cols = 3:5,
                           method = "mad", mask_only = FALSE)
```
