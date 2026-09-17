# dataprep <img src="man/figures/logo.png" align="right" height="120" alt="" />

> Efficient and flexible data preprocessing tools for R,
> with C++ / OpenMP / SIMD backends.

<!-- badges: start -->
<!-- badges: end -->

## In one paragraph

`dataprep` provides an opinionated, high-performance pipeline for
cleaning tabular and time-series data. The 0.1.6 release rewrites
the cleaning routines in C++ and delivers a **10–560× speedup**
over 0.1.5. The `melt()` and `dcast()` reshaping functions **achieve
a 1.03–2664× speedup** relative to every one of the seven major
alternatives in the R and Python ecosystems, at every tested scale
(from 1,000 to 100,000,000 rows), and produce output **identical**
to `reshape2`, `data.table`, `tidyr`, `pandas`, `polars`, `dask`,
and `duckdb`.

## Why dataprep

`dataprep` provides a coherent, opinionated pipeline for
preprocessing tabular and time-series data:

* **Variable deletion** by missing-value fraction (`varidele`).
* **Observation deletion** by consecutive missing runs (`obsedele`).
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

Most heavy routines are written in C++ with Rcpp. Since 0.1.6,
many operations are parallelized with OpenMP and vectorized with
AVX2 / AVX-512 when the hardware supports it.

## Design philosophy

The cleaning pipeline is organised around four sequential steps,
each addressing a distinct failure mode of high-resolution
environmental data:

<img src="man/figures/fig1_pipeline.png" alt="Four-step preprocessing pipeline" width="95%" />

1. **Variable deletion.** Drop size bins whose missing fraction
   exceeds a threshold, so downstream interpolation never has to
   extrapolate from far-away anchors.
2. **Observation deletion.** Drop rows whose selected columns
   contain a consecutive missing run longer than `half` minutes
   on **both** sides. Every remaining point then has a trustworthy
   anchor within `half` minutes.
3. **Conditional extremum outlier removal.** A single value can
   be a global maximum and still be legitimate, or vice versa.
   `condextr()` judges each candidate in context.

   <img src="man/figures/Outlier_Comparison.png" alt="Conditional extremum vs. traditional percentile deletion" width="95%" />

4. **Short-period grouping interpolation.** After steps 1–3,
   remaining `NA`s sit inside short gaps with a valid anchor
   within `half` minutes. `shorvalu()` interpolates within each
   short segment only.

   <img src="man/figures/Time_Series_Interpolation_Final.png" alt="Short-period grouping interpolation" width="95%" />

   Interpolating across a long gap silently mixes two physically
   distinct regimes and can create new outliers at the segment
   boundary. Grouping by short segments keeps the interpolation
   local.

Steps 1–3 are wrapped by `dataprep()` for one-call use. The design
reasoning is documented in full in
`vignette("dataprep-philosophy")`. `data1` in this package is the
**already-aggregated** seven-column version of the same dataset;
it is not a useful input for the cleaning pipeline.

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

# The size-bin columns are the ones whose names are numeric
# (1.00, 1.12, ..., 1000). The four non-size columns
# (`date`, `tconc`, `TPNC`, `monthyear`) are excluded by this
# pattern.
size_bins <- grep("^[-+]?[0-9]*\\.?[0-9]+$", names(data))

cleaned <- dataprep(
  data,
  cols       = size_bins,
  group      = 4,        # monthyear
  interval   = 10,
  times      = 10,
  intervals  = 30
)
dim(cleaned)
```

## Performance

`melt()` and `dcast()` are benchmarked against **all 7 major
alternatives** across **10 shapes and 6 scales** (1,000 to
100,000,000 rows). Every cell is measured with `microbenchmark`
using an adaptive `times` rule. The numbers below are **medians
in milliseconds**; the value in parentheses is `dataprep`'s
speedup relative to that competitor.

> **Test environment.** Dual-socket AMD EPYC 9965 (384 physical
> cores, 768 logical threads), 768 MiB L3 cache, 1 TiB RAM,
> AVX-512 full support, Ubuntu 25.10. See
> `vignette("dataprep-performance")` for the full configuration
> and a discussion of how the hardware shapes the absolute
> multipliers.

### `melt()` — wide to long

| rows | val | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e6 | 9 | **4.33** | 216 (50×) | 44.8 (10×) | 309 (71×) | 109 (25×) | 56.1 (13×) | 70.0 (16×) | 729 (**168×**) |
| 1e7 | 9 | **21.2** | 724 (34×) | 549 (26×) | 1424 (67×) | 1109 (52×) | 653 (31×) | 617 (29×) | 6958 (**328×**) |
| 1e8 | 9 | **540** | 4988 (9.2×) | 4843 (9.0×) | 12480 (23×) | 11246 (21×) | 3970 (7.4×) | 6184 (11×) | 69000 (128×) |

### `melt()` — wide to long, wide value side (1e3 rows, 1 id)

| n_val | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|
| 100 | **0.098** | 1.09 (11×) | 0.127 (1.3×) | 2.41 (25×) | 5.94 (60×) | 0.672 (6.8×) | 52.7 (**536×**) | 17.9 (182×) |
| 1000 | **0.786** | 7.78 (9.9×) | 1.14 (1.4×) | 11.4 (14.5×) | 57.2 (73×) | 6.06 (7.7×) | 537 (**683×**) | 184 (234×) |
| 10000 | **5.21** | 119 (22.8×) | 48.6 (9.3×) | 364 (70×) | 1065 (205×) | 59.2 (11.4×) | 13865 (**2664×**) | 1988 (382×) |

The `1e3 × 10000` cell is the widest gap in the entire benchmark
suite: `dataprep` returns in **5.21 ms**, `dask` in **13.87 s**.

### `dcast()` — long to wide, 1 id

| n_long | levels | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e6 | 100 | **0.450** | 110 (245×) | 389 (**863×**) | 43.0 (95×) | 64.6 (143×) | 189 (419×) | 83.6 (186×) | 178 (396×) |
| 1e7 | 100 | **3.83** | 2282 (**596×**) | 880 (230×) | 807 (211×) | 1506 (393×) | 428 (112×) | 1645 (430×) | 1883 (492×) |
| 1e8 | 100 | **66.5** | 22058 (332×) | 22825 (343×) | 8652 (130×) | 16070 (242×) | 2427 (36×) | 26038 (391×) | 17216 (259×) |

### `dcast()` — long to wide, 10 id, 1e6 rows, 10 levels

| n_id | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|
| 1 | **0.904** | 355 (393×) | 438 (485×) | 62.8 (69×) | 78.5 (87×) | 104 (115×) | 96.5 (107×) | 153 (169×) |
| 10 | **2.80** | 2298 (**819×**) | 478 (171×) | 77.5 (27.6×) | 290 (103×) | 135 (48×) | 369 (131×) | 161 (57×) |
| 100 | **42.0** | 20034 (477×) | 691 (16.4×) | 397 (9.5×) | 2456 (58×) | 154 (**3.7×**) | 3064 (73×) | 211 (5.0×) |

### Summary of speedups

| Operation | Smallest speedup | Largest speedup |
|---|---|---|
| `melt()` | 1.03× (data.table @ 1e5 × 1 id + 9 val) | **2664×** (dask @ 1e3 × 1 id + 10000 val) |
| `dcast()` | 3.7× (polars @ 1e6 × 100 id) | **863×** (data.table @ 1e6 × 1 id + 100 levels) |

The **median** speedup across all tested cells and all competitors
is **above 30×** for `melt` and **above 60×** for `dcast`. On the
largest cells (1e8 rows, 8 GB of input), `dataprep` is the only
engine that completes within 1 second.

Complete tables — including `mean`, `median`, and the full
per-competitor gradient — are in
`vignette("dataprep-performance")`.

## Cross-engine consistency

`melt()` and `dcast()` produce output **identical** to `reshape2`,
`data.table`, `tidyr`, `pandas`, `polars`, `dask`, and `duckdb` on
every tested shape, within `tol = 1e-12`:

| Operation | Cells tested | Engines | Pairwise |
|---|---|---|---|
| `melt` | 4 shapes | 8 | all consistent |
| `dcast` | 4 shapes | 8 | all consistent |

Reproducible scripts ship under `inst/` and are disabled by
default so that `R CMD check` does not run them:

```r
Sys.setenv(DATAPREP_RUN_BENCHMARK = "1")
source(system.file("melt_benchmark.R",  package = "dataprep"))
source(system.file("dcast_benchmark.R", package = "dataprep"))
```

## When not to preprocess

The pipeline above assumes that the input is high-resolution
instrument data with intermittent gaps and occasional outliers.
Three cases where you should not run the full pipeline:

* **Already-aggregated data.** `data1` in this package is the
  result of aggregating the 60 size bins of `data` into three
  modes. It has no long gaps and no obvious outliers, so
  `varidele`, `obsedele`, `condextr`, and `shorvalu` have nothing
  to do.
* **Models that tolerate missing values.** Gradient boosting,
  random forests, and XGBoost handle `NA` natively.
* **Gaps shorter than the physical mixing time.** When the
  aerosol is well-mixed, a few missing points can be interpolated
  with negligible error.

See `vignette("dataprep-philosophy")` for the full reasoning.

## Function overview

### Cleaning

* `varidele()` — remove variables by missing fraction
* `obsedele()` — remove observations by consecutive missing runs
* `condextr()` — point-by-point weighted conditional extremum
* `percoutl()` — traditional percentile removal
* `detect_outliers()` — IQR / MAD / percentile masks
* `winsorize()` — cap extreme values
* `phys_filter()` — physical range filter
* `filter_high_cor()` / `filter_low_var()` — variable selection
* `deduplicate()` — exact / fuzzy duplicate removal
* `validate_data()` — rule-based validation
* `balance_panel()` — panel balancing

### Missing values and imputation

* `na_diagnose()` — NA run statistics
* `impute_missing()` — linear / LOCF / NOCB / mean / median
* `shorvalu()` — short-period linear interpolation

### Transformation

* `transform_data()` — log / sqrt / Box-Cox / Yeo-Johnson,
  z-score / min-max / robust
* `log_returns()` — log returns
* `bin_data()` — equal-width / equal-frequency / custom binning
* `encode_categorical()` — label / frequency / one-hot

### Time series

* `create_lags()` — grouped lag / lead columns
* `roll_apply()` — rolling statistics
* `resample_time()` — resample to hour / day / month
* `detrend_ts()` — linear detrending
* `remove_diurnal_cycle()` — subtract mean diurnal cycle
* `decompose_ts()` — additive / multiplicative decomposition
* `drift_detect()` — rolling drift detection
* `day_night_flag()` / `season_flag()` — time flags

### Reshaping

* `melt()` — wide to long, SIMD + OpenMP backend
* `dcast()` — long to wide, block-path strided copy

### Workflow and reporting

* `descdata()` / `descplot()` — descriptive statistics
* `percdata()` / `percplot()` — percentile summaries
* `data_report()` — compact data quality report
* `dry_run()` — simulate preprocessing
* `prep_fit()` / `prep_transform()` — fit / transform pipeline
* `sample_data()` — stratified sampling

## Documentation

* [Design philosophy and preprocessing
  methodology](articles/dataprep-philosophy.html) — why the
  pipeline has the shape it does.
* [Cleaning pipeline](articles/dataprep-cleaning.html) —
  step-by-step walkthrough of `varidele` / `obsedele` /
  `condextr` / `shorvalu`.
* [Performance and cross-engine
  consistency](articles/dataprep-performance.html) — full
  benchmark tables and consistency checks.
* [Upgrading from 0.1.5 to
  0.1.6](articles/dataprep-migration.html) — behaviour changes
  and migration checklist.
* [Leakage-free workflow](articles/dataprep-workflow.html) —
  `prep_fit()` / `prep_transform()`.
* [Fast reshaping with `melt()` and
  `dcast()`](articles/dataprep-melt-dcast.html).
* [Descriptive statistics and
  plots](articles/dataprep-plots.html).
* [Function reference](reference/index.html).
* [Changelog](news/index.html).

## Funding

This work was supported by the National Natural Science Foundation
of China (No. 12301674).

## Citation

If you use `dataprep` in published work, please cite:

> Liang, C.-S., Wu, H., Li, H.-Y., Zhang, Q., Li, Z. & He, K.-B.
> (2020). Efficient data preprocessing, episode classification, and
> source apportionment of particle number concentrations.
> *Science of the Total Environment*, 741, 140923.
> <https://doi.org/10.1016/j.scitotenv.2020.140923>

## License

GPL (>= 2)