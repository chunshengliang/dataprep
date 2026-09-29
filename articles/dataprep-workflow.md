# dataprep: a leakage-free preprocessing workflow

``` r

library(dataprep)
set.seed(1)

# The size-bin columns are the ones whose names are numeric
# (1.00, 1.12, ..., 1000). This helper returns their integer
# positions, excluding the four non-size columns (`date`,
# `tconc`, `TPNC`, `monthyear`).
size_bin_cols <- function(x) {
  grep("^[-+]?[0-9]*\\.?[0-9]+$", names(x))
}
```

## The data-leakage problem

Standard preprocessing steps such as outlier detection, imputation, and
scaling are often implemented as one-shot functions. If you apply them
to training and test data separately, the test set ends up using its own
statistics, which leaks information from the test set into the pipeline.

`dataprep` 0.1.7 solves this with a two-step interface:

- [`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
  estimates every parameter (missing fraction, outlier thresholds,
  imputation method, scaling centre/scale) from the **training data
  only**, and returns a `prep_plan`.

- [`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md)
  applies the plan to new data without re-estimating anything.

The design mirrors `recipes::prep()` / `recipes::bake()` and
`caret::preProcess()` / `caret::predict()`, but the underlying
operations are the same C++ backends used by
[`varidele()`](https://chunshengliang.github.io/dataprep/reference/varidele.md),
[`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.md),
[`detect_outliers()`](https://chunshengliang.github.io/dataprep/reference/detect_outliers.md),
[`impute_missing()`](https://chunshengliang.github.io/dataprep/reference/impute_missing.md),
and
[`transform_data()`](https://chunshengliang.github.io/dataprep/reference/transform_data.md).

> **Note on `data1`.** `data1` is the already-aggregated seven-column
> version of `data`. It has no long missing runs and no obvious
> outliers, so it is not a meaningful input for the cleaning steps. All
> examples below therefore use the full `data` table.

## A minimal example

``` r

train <- data[1:5000, c("date", "monthyear", "7.94", "8.91", "10")]
test  <- data[5001:6000, c("date", "monthyear", "7.94", "8.91", "10")]

plan <- prep_fit(
  train,
  cols       = 3:5,
  group      = 2,
  steps      = c("varidele", "outlier", "impute", "scale"),
  fraction   = 0.5,
  method_outlier = "iqr",
  method_impute  = "linear",
  scale_method   = "zscore"
)

str(plan, max.level = 2)
#> List of 4
#>  $ steps     : chr [1:4] "varidele" "outlier" "impute" "scale"
#>  $ params    :List of 12
#>   ..$ varidele_keep      : Named logi [1:3] FALSE FALSE TRUE
#>   .. ..- attr(*, "names")= chr [1:3] "7.94" "8.91" "10"
#>   ..$ varidele_keep_names: chr "10"
#>   ..$ varidele_cols_after: chr "10"
#>   ..$ outlier_method     : chr "iqr"
#>   ..$ outlier_group      : num 2
#>   ..$ outlier_thresholds :List of 2
#>   ..$ impute_method      : chr "linear"
#>   ..$ impute_group       : num 2
#>   ..$ scale_method       : chr "zscore"
#>   ..$ scale_center       :List of 1
#>   ..$ scale_scale        :List of 1
#>   ..$ scale_group        : num 2
#>  $ data_info :List of 4
#>   ..$ original_names: chr [1:5] "date" "monthyear" "7.94" "8.91" ...
#>   ..$ col_idx       : int [1:3] 3 4 5
#>   ..$ date_col      : NULL
#>   ..$ group         : num 2
#>  $ final_data:'data.frame':  5000 obs. of  3 variables:
#>   ..$ date     : POSIXct[1:5000], format: "2019-12-31 16:00:00" "2019-12-31 16:10:00" ...
#>   ..$ monthyear: chr [1:5000] "January 2020" "January 2020" "January 2020" "January 2020" ...
#>   ..$ 10       : num [1:5000, 1] -0.248 -0.716 -0.215 0.181 -0.248 ...
#>   .. ..- attr(*, "dimnames")=List of 2
```

Apply the plan to the test set:

``` r

test_clean <- prep_transform(plan, test)
head(test_clean)
#>                     date monthyear         10
#> 5001 2020-07-13 06:10:00 July 2020         NA
#> 5002 2020-07-13 06:20:00 July 2020         NA
#> 5003 2020-07-13 06:30:00 July 2020 -0.3849741
#> 5004 2020-07-13 06:40:00 July 2020 -0.4269420
#> 5005 2020-07-13 06:50:00 July 2020 -0.4689099
#> 5006 2020-07-13 07:00:00 July 2020 -0.5108778
```

Note that `test_clean` has the same columns as the training data after
the plan’s `varidele` step, and its scaling uses the training set’s
centre / scale — not its own.

## What is stored in the plan

``` r

names(plan)
#> [1] "steps"      "params"     "data_info"  "final_data"
names(plan$params)
#>  [1] "varidele_keep"       "varidele_keep_names" "varidele_cols_after"
#>  [4] "outlier_method"      "outlier_group"       "outlier_thresholds" 
#>  [7] "impute_method"       "impute_group"        "scale_method"       
#> [10] "scale_center"        "scale_scale"         "scale_group"
```

- `params$varidele_keep` — logical mask of columns to keep
- `params$outlier_thresholds` — lower / upper bounds per column (or per
  column-group)
- `params$impute_method` — imputation method string
- `params$scale_center`, `params$scale_scale` — per-column centring and
  scaling constants
- `data_info` — original column names, indices, and the grouping column,
  used to realign columns and re-apply grouped operations when `test` is
  fed in with a different order

### A note on degenerate columns

A constant training column has `sd = 0`, `IQR = 0`, or `max - min = 0`.
Storing `0` as `scale_val` would make
[`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md)
divide by zero.
[`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
stores `1` for such columns instead, so the transform becomes
`x - center` (equivalently `x - x`), and
[`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md)
additionally guards against `scale_val == 0` in case a plan is edited by
hand.

## Adding or removing steps

[`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
accepts an ordered `steps` vector. Any subset of the following is
allowed, and the order is respected as given:

``` r

steps = c("varidele", "obsedele", "outlier", "impute", "scale")
```

- **`varidele`** — drop columns whose training-set missing fraction is
  above `fraction`.
- **`obsedele`** — drop rows with long consecutive `NA` runs. This step
  does not store any threshold; it re-runs the anchor scan on the new
  data using the `by` and `half` values from training.
- **`outlier`** — detect and replace outliers with `NA` using IQR, MAD,
  or percentile thresholds estimated on the training set.
- **`impute`** — fill remaining `NA`s using LOCF, NOCB, linear, mean, or
  median.
- **`scale`** — z-score, min-max, robust, centre, or scale.

If a step’s parameter is not needed (e.g. `obsedele` does not learn
anything from the training data that would be reused later), the plan
simply stores the call arguments and re-runs the same operation on the
test data.

## Column-order independence

[`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md)
realigns the new data by column name, not by position. If `test` has its
columns in a different order from `train`, the plan still applies
correctly:

``` r

test_reordered <- test[, c("date", "10", "8.91", "7.94", "monthyear")]
test_reordered_clean <- prep_transform(plan, test_reordered)
identical(names(test_reordered_clean), names(test_clean))
#> [1] FALSE
```

## Missing-column detection

If `newdata` is missing a required column,
[`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md)
raises an error listing the missing names, rather than silently
producing wrong output:

``` r

test_missing <- test[, c("date", "monthyear", "7.94", "8.91")]
prep_transform(plan, test_missing)
#> Error in `prep_transform()`:
#> ! newdata is missing required columns: 10
```

    #> Error: newdata is missing required columns: 10

## Workflow with `dataprep()`

For exploratory analysis where leakage is not a concern, the one-call
[`dataprep()`](https://chunshengliang.github.io/dataprep/reference/dataprep.md)
wrapper chains the four standard steps on the full `data` table:

``` r

res <- dataprep(
  data[1:1000, ],
  cols     = size_bin_cols(data[1:1000, ]),
  group    = 4,
  interval = 5,
  times    = 3
)
dim(res)
#> [1] 875  39
```

[`dataprep()`](https://chunshengliang.github.io/dataprep/reference/dataprep.md)
and
[`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
share the same underlying backends, but they make different promises:

| Aspect | [`dataprep()`](https://chunshengliang.github.io/dataprep/reference/dataprep.md) | [`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md) / [`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md) |
|----|----|----|
| Use case | exploration, one-shot cleaning | train / test split, deployment |
| Output | cleaned data frame | `prep_plan` object + cleaned data |
| Leakage | re-estimates thresholds on every call | estimates once, applies everywhere |
| Rows | keeps all rows whose anchors are adequate | same, but row sets can differ between train and test |
| Speed | one-shot, no overhead | a small per-step overhead from plan bookkeeping |

## Reporting

[`data_report()`](https://chunshengliang.github.io/dataprep/reference/data_report.md)
is read-only, so it works on either `data` or `data1`. We use `data1`
here for a compact output.

``` r

data_report(data1, cols = 3:7, verbose = TRUE)
#> ========== Data Quality Report ==========
#> Dimensions: 7640 rows x 7 columns
#> 
#> Variable type distribution:
#> types
#> character   numeric   POSIXct 
#>         1         5         1 
#> 
#> Missing value diagnosis (numeric columns):
#>       variable    n na na_frac na_runs max_run
#> 1   Nucleation 7640  0       0       0       0
#> 2       Aitken 7640  0       0       0       0
#> 3 Accumulation 7640  0       0       0       0
#> 4        tconc 7640  0       0       0       0
#> 5         TPNC 7640  0       0       0       0
#> 
#> Descriptive statistics (numeric columns):
#>      variables    n na     mean       sd    median   trimmed        min
#> 1   Nucleation 7640  0 123.8971 240.3190  53.81075  73.76004 0.05491765
#> 2       Aitken 7640  0 414.8573 477.1444 277.35785 331.77821 0.44507100
#> 3 Accumulation 7640  0 244.5512 242.2446 164.14900 207.35865 0.96263700
#> 4        tconc 7640  0 783.0922 706.7581 691.80400 693.28794 3.52890000
#> 5         TPNC 7640  0 783.3057 706.7105 691.63857 693.57869 3.11677730
#>        max      IQR
#> 1 4137.091  99.1508
#> 2 4159.520 500.0387
#> 3 1154.862 328.2945
#> 4 6495.960 926.9235
#> 5 6474.645 927.0629
#> 
#> Time used by data_report: 0.00958 secs
invisible(data_report(data1, cols = 3:7))
```

## Full applied workflow

A complete train / test workflow with the full cleaning pipeline:

``` r

# 1. Inspect the raw data
data_report(data, cols = size_bin_cols(data), verbose = TRUE)

# 2. Fit a plan on the training split
train <- data[1:5000, ]
plan  <- prep_fit(
  train,
  cols     = size_bin_cols(train),
  group    = 4,
  steps    = c("varidele", "obsedele", "outlier", "impute", "scale"),
  fraction = 0.5,
  method_outlier = "iqr",
  method_impute  = "linear",
  scale_method   = "zscore"
)

# 3. Apply the same plan to the test split
test       <- data[5001:6000, ]
test_clean <- prep_transform(plan, test)

# 4. Model on the cleaned training set,
#    predict on the cleaned test set
fit  <- lm(`7.94` ~ `8.91` + `10`,
           data = plan$final_data)
pred <- predict(fit, newdata = test_clean)
```

The key point is that `plan` is a self-contained object. It can be saved
to disk (`saveRDS(plan, "plan.rds")`) and loaded in a later session
(`plan <- readRDS("plan.rds")`) without any dependence on the training
data.

## When NOT to preprocess

Not every dataset needs the full pipeline:

1.  **Already-aggregated data.** `data1` is the seven-column aggregate
    of `data`. Running `varidele` / `obsedele` / `condextr` / `shorvalu`
    on it would do nothing useful.

2.  **Models that tolerate missing values.** Gradient boosting, random
    forests, and XGBoost handle `NA` natively. If your model does, you
    can skip `impute` and keep the `NA`s.

3.  **Gaps shorter than the physical mixing time.** When the aerosol is
    well-mixed, a few missing points can be interpolated with negligible
    error, so `obsedele` can be relaxed by increasing `half`.

See
[`vignette("dataprep-philosophy")`](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.md)
for the full reasoning behind each of these cases.

## Test environment

The examples in this vignette are executed on Windows 11 Pro for
Workstations (R 4.6.1 ucrt, GCC 14.3.0) with a 2× AMD EPYC 7B12 64-Core
processor and about 224 GiB RAM, and on Ubuntu 25.10 (R 4.5.1, g++
15.2.0) with a 2× AMD EPYC 9965 192-Core processor (384 physical / 768
logical cores), 1.0 TiB (16 × 64 GiB Micron, DDR5-5600, Multi-bit ECC)
and full AVX-512. Full hardware details are in `README.md`.

## Where to go next

- **Design philosophy** —
  [`vignette("dataprep-philosophy")`](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.md).
  Why the pipeline has the shape it does, and how each step enforces a
  physical constraint.

- **Cleaning pipeline walkthrough** —
  [`vignette("dataprep-cleaning")`](https://chunshengliang.github.io/dataprep/articles/dataprep-cleaning.md).
  Step-by-step execution on a real dataset.

- **Performance and cross-engine consistency** —
  [`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md).
  Full benchmark tables and 8-engine consistency checks.

- **Upgrading from 0.1.5 to 0.1.7** —
  [`vignette("dataprep-migration")`](https://chunshengliang.github.io/dataprep/articles/dataprep-migration.md).
  Behaviour changes and the migration checklist.

- **Fast reshaping** —
  [`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md).

## Session info

``` r

sessionInfo()
#> R version 4.6.1 (2026-06-24)
#> Platform: x86_64-pc-linux-gnu
#> Running under: Ubuntu 24.04.5 LTS
#> 
#> Matrix products: default
#> BLAS:   /usr/lib/x86_64-linux-gnu/openblas-pthread/libblas.so.3 
#> LAPACK: /usr/lib/x86_64-linux-gnu/openblas-pthread/libopenblasp-r0.3.26.so;  LAPACK version 3.12.0
#> 
#> locale:
#>  [1] LC_CTYPE=C.UTF-8       LC_NUMERIC=C           LC_TIME=C.UTF-8       
#>  [4] LC_COLLATE=C.UTF-8     LC_MONETARY=C.UTF-8    LC_MESSAGES=C.UTF-8   
#>  [7] LC_PAPER=C.UTF-8       LC_NAME=C              LC_ADDRESS=C          
#> [10] LC_TELEPHONE=C         LC_MEASUREMENT=C.UTF-8 LC_IDENTIFICATION=C   
#> 
#> time zone: UTC
#> tzcode source: system (glibc)
#> 
#> attached base packages:
#> [1] stats     graphics  grDevices utils     datasets  methods   base     
#> 
#> other attached packages:
#> [1] dataprep_0.1.7
#> 
#> loaded via a namespace (and not attached):
#>  [1] vctrs_0.7.3       cli_3.6.6         knitr_1.52        rlang_1.3.0      
#>  [5] xfun_0.61         otel_0.2.0        textshaping_1.0.5 jsonlite_2.0.0   
#>  [9] glue_1.8.1        htmltools_0.5.9   ragg_1.5.2        sass_0.4.10      
#> [13] rmarkdown_2.32    evaluate_1.0.5    jquerylib_0.1.4   fastmap_1.2.0    
#> [17] yaml_2.3.12       lifecycle_1.0.5   compiler_4.6.1    fs_2.1.0         
#> [21] Rcpp_1.1.2        systemfonts_1.3.2 digest_0.6.39     R6_2.6.1         
#> [25] pillar_1.11.1     parallel_4.6.1    bslib_0.12.0      tools_4.6.1      
#> [29] pkgdown_2.2.1     cachem_1.1.0      desc_1.4.3
```
