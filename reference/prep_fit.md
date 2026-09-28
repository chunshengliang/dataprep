# Build a preprocessing plan on training data to prevent data leakage

Computes and stores all preprocessing parameters (such as missing
fractions, outlier thresholds, imputation methods, and scaling
statistics) using only the training data. The returned plan object can
then be applied to new data via `prep_transform` without re-estimating
any parameters, ensuring that information from the test set does not
leak into the preprocessing steps.

## Usage

``` r
prep_fit(data, steps = c("varidele", "obsedele", "outlier", "impute", "scale"),
         cols = NULL, group = NULL, date_col = NULL,
         fraction = 0.25, top = 0.995, bottom = 0.0025,
         by = "min", half = 30, method_outlier = "iqr",
         coef = 1.5, method_impute = "linear",
         scale_method = "zscore", verbose = FALSE)
```

## Arguments

- data:

  A data frame containing the training data. All preprocessing
  parameters are estimated from this data only.

- steps:

  A character vector specifying the preprocessing steps to include.
  Available steps are `"varidele"`, `"obsedele"`, `"outlier"`,
  `"impute"`, and `"scale"`. The default is all five steps in that
  order.

- cols:

  Column indices or names of numeric variables to be processed. If
  `NULL`, all numeric columns are selected.

- group:

  Optional grouping column index or name used for grouped outlier
  detection, imputation, or scaling.

- date_col:

  Optional time column index or name used by `obsedele` for time‑aware
  observation deletion.

- fraction:

  Missing fraction threshold for variable deletion in the `"varidele"`
  step.

- top:

  Top percentile used by the percentile outlier detection method.

- bottom:

  Bottom percentile used by the percentile outlier detection method.

- by:

  Time extension unit for observation deletion (see `obsedele`).

- half:

  Half window size in minutes for consecutive missing value deletion
  (see `obsedele`).

- method_outlier:

  Outlier detection method: `"iqr"`, `"mad"`, or `"percentile"`.

- coef:

  Coefficient for IQR or MAD outlier detection.

- method_impute:

  Missing value imputation method: `"linear"`, `"locf"`, `"nocb"`,
  `"mean"`, or `"median"`.

- scale_method:

  Scaling method used in the `"scale"` step: `"zscore"`, `"minmax"`, or
  `"robust"`. Other values fall back to identity scaling (center = 0,
  scale = 1).

- verbose:

  Logical; if `TRUE`, prints progress messages.

## Details

The function follows the *fit-transform* paradigm. All thresholds,
means, standard deviations, and group‑wise statistics are computed
solely from `data` (training set). Applying these saved parameters to
test data with `prep_transform` guarantees that no information from the
test set influences the preprocessing, thereby preventing data leakage.

## Value

A list of class `prep_plan` containing all parameters learned from the
training data. The list includes:

- `steps`: the ordered preprocessing steps.

- `params`: a list of parameters such as missing fractions, outlier
  thresholds, imputation method, and scaling center/scale values.

- `data_info`: original column names, selected column indices, and other
  metadata.

- `final_data`: the fully preprocessed training data (optional, useful
  for inspection).

## Examples

``` r
# Build a preprocessing plan using the first 100 rows as training data
plan <- prep_fit(data[1:100, c(1, 4, 47:49)], cols = 3:5, group = 2)
# Apply the plan to new data
newdata <- data[101:200, c(1, 4, 47:49)]
cleaned <- prep_transform(plan, newdata)
```
