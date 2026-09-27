# Apply a preprocessing plan to new data

Uses a preprocessing plan created by `prep_fit` to transform new data.
All parameters (thresholds, statistics, etc.) are taken from the plan
and are not re‑estimated, thus ensuring that the same preprocessing
logic is applied consistently and without data leakage.

## Usage

``` r
prep_transform(plan, newdata, verbose = FALSE)
```

## Arguments

- plan:

  A preprocessing plan object returned by `prep_fit`.

- newdata:

  A data frame containing the new data (e.g., test set) to be
  transformed. It must contain the same columns as the original training
  data used to build the plan.

- verbose:

  Logical; if `TRUE`, prints progress messages.

## Details

The function iterates through the steps stored in `plan` and applies the
corresponding operations using the saved parameters. For example, if the
`"outlier"` step used group‑specific thresholds, those exact thresholds
are applied to the new data grouped in the same way.

## Value

A data frame after applying the preprocessing steps specified in `plan`.
The output contains the same variables as the training data after
preprocessing.

## Examples

``` r
# Build plan
plan <- prep_fit(data[1:100, c(1, 4, 47:49)], cols = 3:5, group = 2)
# Transform new data
newdata <- data[101:200, c(1, 4, 47:49)]
cleaned <- prep_transform(plan, newdata)
```
