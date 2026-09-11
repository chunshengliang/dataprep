# dataprep <img src="man/figures/logo.png" align="right" height="120" alt="" />

> Efficient and flexible data preprocessing tools for R,
> with C++ / OpenMP / SIMD backends.

<!-- badges: start -->
<!-- badges: end -->

## Why dataprep

`dataprep` provides a coherent, opinionated pipeline for
preprocessing tabular and time-series data:

* **Variable deletion** by missing-value fraction.
* **Observation deletion** by consecutive missing runs.
* **Outlier removal** by point-by-point weighted conditional
  extremum (`condextr`) or by percentile (`percoutl`).
* **Missing-value imputation** within short periods (`shorvalu`)
  or by linear / LOCF / NOCB / mean / median (`impute_missing`).
* **Fast reshaping** between wide and long formats (`melt`,
  `dcast`) with SIMD + OpenMP.
* **Descriptive statistics, diagnostics, transformation,
  standardization, encoding, validation, and reporting.**
* **Time-series tools**: detrending, diurnal-cycle removal,
  rolling statistics, lags, resampling, decomposition, drift
  detection, day/night and season flags.
* **Fit / transform interfaces** (`prep_fit`, `prep_transform`)
  that prevent data leakage during preprocessing.

Most routines are written in C++ with Rcpp. Since 0.1.6, many
operations are parallelized with OpenMP and vectorized with
AVX2 / AVX-512 when the hardware supports it.

## Installation

```r
# from GitHub
# install.packages("remotes")
remotes::install_github("chunshengliang/dataprep")
```

The package requires a C++17 compiler (Rtools on Windows,
Xcode / clang on macOS, gcc on Linux).

## Quick start

```r
library(dataprep)

data(data1)             # 7,640 rows x 7 columns

res <- dataprep(
  data1,
  cols       = 3:5,     # Nucleation, Aitken, Accumulation
  group      = 2,       # monthyear
  interval   = 10,
  times      = 10,
  cores      = 4
)

head(res)
```
## Documentation

* [Descriptive statistics and diagnostic plots](articles/dataprep-plots.html)
* [Cleaning pipeline](articles/dataprep-cleaning.html)
* [Fast reshaping with melt() and dcast()](articles/dataprep-melt-dcast.html)
* [Leakage-free preprocessing workflow](articles/dataprep-workflow.html)
* [Function reference](reference/index.html)
* [Changelog](news/index.html)

## Performance

### Cleaning pipeline

On a 7,640-row dataset (`data1]`), dataprep 0.1.6 versus 0.1.5:

| Function     | 0.1.5      | 0.1.6       | Speed-up |
|--------------|------------|-------------|----------|
| `obsedele`   | 11.4 s     | 0.0253 s    | ~450x    |
| `condextr`   | 2.25 min   | 5.73 s      | ~23x     |
| `optisolu`   | 13.8 min   | 10.8 s      | ~77x     |
| `shorvalu`   | 0.99 s     | 0.011 s     | ~90x     |
| `dataprep`   | 1.28 min   | 4.9 s       | ~16x     |

### Wide-to-long reshape (`melt`)

1,000,000 rows x 10 columns, times = 20 (median, ms):

| Engine                | Median (ms) | Relative |
|-----------------------|-------------|----------|
| `dataprep::melt`      | 5.1         | 1.0x     |
| `polars` (Python)     | 22.1        | 4.3x     |
| `data.table::melt`    | 30.2        | 5.9x     |
| `reshape2::melt`      | 38.8        | 7.6x     |
| `tidyr::pivot_longer` | 123.3       | 24.2x    |
| `pandas.melt`         | 157.2       | 30.8x    |

10,000,000 rows x 10 columns, median (ms):

| Engine                | Median (ms) | Relative |
|-----------------------|-------------|----------|
| `dataprep::melt`      | 46.5        | 1.0x     |
| `polars` (Python)     | 232.5       | 5.0x     |
| `reshape2::melt`      | 517.8       | 11.1x    |
| `data.table::melt`    | 536.6       | 11.5x    |
| `tidyr::pivot_longer` | 1363.2      | 29.3x    |
| `pandas.melt`         | 1580.9      | 34.0x    |

See `inst/benchmark_helpers.R`, `inst/melt_benchmark.R`, and
`inst/dcast_benchmark.R` for the reproducible scripts. The 8-engine
consistency check confirms that `dataprep::melt` / `dcast` produce
identical results to `reshape2`, `data.table`, `tidyr`, `pandas`,
`polars`, `dask`, and `duckdb`.

## Function overview

### Cleaning

* `varidele()` - remove variables by missing fraction
* `obsedele()` - remove observations by consecutive missing runs
* `condextr()` - point-by-point weighted conditional extremum
* `percoutl()` - traditional percentile removal
* `detect_outliers()` - IQR / MAD / percentile masks
* `winsorize()` - cap extreme values
* `phys_filter()` - physical range filter
* `filter_high_cor()` / `filter_low_var()` - variable selection
* `deduplicate()` - exact / fuzzy duplicate removal
* `validate_data()` - rule-based validation
* `balance_panel()` - panel balancing

### Missing values and imputation

* `na_diagnose()` - NA run statistics
* `impute_missing()` - linear / LOCF / NOCB / mean / median
* `shorvalu()` - short-period linear interpolation

### Transformation

* `transform_data()` - log / sqrt / Box-Cox / Yeo-Johnson,
  z-score / min-max / robust
* `log_returns()` - log returns
* `bin_data()` - equal-width / equal-frequency / custom binning
* `encode_categorical()` - label / frequency / one-hot

### Time series

* `create_lags()` - grouped lag / lead columns
* `roll_apply()` - rolling statistics
* `resample_time()` - resample to hour / day / month
* `detrend_ts()` - linear detrending
* `remove_diurnal_cycle()` - subtract mean diurnal cycle
* `decompose_ts()` - additive / multiplicative decomposition
* `drift_detect()` - rolling drift detection
* `day_night_flag()` / `season_flag()` - time flags

### Reshaping

* `melt()` - wide to long, SIMD + OpenMP backend
* `dcast()` - long to wide, SIMD backend

### Workflow and reporting

* `descdata()` / `descplot()` - descriptive statistics
* `percdata()` / `percplot()` - percentile summaries
* `data_report()` - compact data quality report
* `dry_run()` - simulate preprocessing
* `prep_fit()` / `prep_transform()` - fit / transform pipeline
* `sample_data()` - stratified sampling

## Citation

If you use `dataprep` in published work, please cite:

> Liang, C.-S., Wu, H., Li, H.-Y., Zhang, Q., Li, Z. & He, K.-B.
> (2020). Efficient data preprocessing tools for atmospheric
> particle number size distributions.
> *Science of the Total Environment*, 741, 140923.
> <https://doi.org/10.1016/j.scitotenv.2020.140923>

## License

GPL (>= 2)
