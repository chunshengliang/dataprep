# Package index

## Complete preprocessing workflow

One-call pipelines and fit/transform interfaces that combine several
steps while preventing data leakage.

- [`dataprep-package`](https://chunshengliang.github.io/dataprep/reference/dataprep-package.md)
  : dataprep: Fast, Efficient, and Versatile Data Preprocessing and
  Reshaping with C++, OpenMP & SIMD
- [`dataprep()`](https://chunshengliang.github.io/dataprep/reference/dataprep.md)
  : Data preprocessing with multiple steps in one function
- [`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
  : Build a preprocessing plan on training data to prevent data leakage
- [`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md)
  : Apply a preprocessing plan to new data
- [`dry_run()`](https://chunshengliang.github.io/dataprep/reference/dry_run.md)
  : Simulate preprocessing and report changes without modifying data
- [`data_report()`](https://chunshengliang.github.io/dataprep/reference/data_report.md)
  : Generate a simple data quality report

## Variable and observation deletion

Remove variables by missing fraction, remove observations by consecutive
missing runs, and diagnose the effect beforehand.

- [`varidele()`](https://chunshengliang.github.io/dataprep/reference/varidele.md)
  : Delete variables containing too many missing values
- [`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.md)
  : Delete observations with excessive consecutive missing values
- [`na_diagnose()`](https://chunshengliang.github.io/dataprep/reference/na_diagnose.md)
  : Diagnose missing value patterns in data
- [`balance_panel()`](https://chunshengliang.github.io/dataprep/reference/balance_panel.md)
  : Balance panel data

## Outlier removal and detection

Point-by-point weighted conditional extremum, traditional percentile
removal, mask-based detection, and winsorization.

- [`condextr()`](https://chunshengliang.github.io/dataprep/reference/condextr.md)
  : Remove outliers using point-by-point weighed outlier removal by
  conditional extremum
- [`percoutl()`](https://chunshengliang.github.io/dataprep/reference/percoutl.md)
  : Traditional percentile-based outlier removal
- [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
  : Find optimal combination of interval and times for condextr
- [`detect_outliers()`](https://chunshengliang.github.io/dataprep/reference/detect_outliers.md)
  : Detect outliers using multiple methods
- [`winsorize()`](https://chunshengliang.github.io/dataprep/reference/winsorize.md)
  : Winsorize outliers by capping extreme values
- [`phys_filter()`](https://chunshengliang.github.io/dataprep/reference/phys_filter.md)
  : Physical limit filtering

## Missing-value imputation

Short-period interpolation and general imputation strategies.

- [`shorvalu()`](https://chunshengliang.github.io/dataprep/reference/shorvalu.md)
  : Interpolation with values to refer to within short periods
- [`impute_missing()`](https://chunshengliang.github.io/dataprep/reference/impute_missing.md)
  : Impute missing values

## Variable selection and encoding

Drop redundant variables, encode categorical columns, and discretize
continuous variables.

- [`filter_high_cor()`](https://chunshengliang.github.io/dataprep/reference/filter_high_cor.md)
  : Remove highly correlated variables
- [`filter_low_var()`](https://chunshengliang.github.io/dataprep/reference/filter_low_var.md)
  : Remove low-variance (near-constant) variables
- [`encode_categorical()`](https://chunshengliang.github.io/dataprep/reference/encode_categorical.md)
  : Encode categorical variables
- [`bin_data()`](https://chunshengliang.github.io/dataprep/reference/bin_data.md)
  : Discretize continuous variables into bins

## Transformation and standardization

Log / Box-Cox / Yeo-Johnson transformations and z-score / min-max /
robust scaling.

- [`transform_data()`](https://chunshengliang.github.io/dataprep/reference/transform_data.md)
  : Transform and standardize numeric variables
- [`log_returns()`](https://chunshengliang.github.io/dataprep/reference/log_returns.md)
  : Logarithmic returns for financial time series
- [`zerona()`](https://chunshengliang.github.io/dataprep/reference/zerona.md)
  : Turn zeros to missing values

## Time series tools

Detrending, diurnal-cycle removal, rolling statistics, lags, resampling,
decomposition, drift detection, and time flags.

- [`detrend_ts()`](https://chunshengliang.github.io/dataprep/reference/detrend_ts.md)
  : Remove linear trend from time series
- [`remove_diurnal_cycle()`](https://chunshengliang.github.io/dataprep/reference/remove_diurnal_cycle.md)
  : Remove diurnal cycle
- [`roll_apply()`](https://chunshengliang.github.io/dataprep/reference/roll_apply.md)
  : Apply rolling window statistics
- [`create_lags()`](https://chunshengliang.github.io/dataprep/reference/create_lags.md)
  : Create lagged variables
- [`resample_time()`](https://chunshengliang.github.io/dataprep/reference/resample_time.md)
  : Resample time series to a coarser period
- [`decompose_ts()`](https://chunshengliang.github.io/dataprep/reference/decompose_ts.md)
  : Simple time series decomposition
- [`drift_detect()`](https://chunshengliang.github.io/dataprep/reference/drift_detect.md)
  : Sensor drift detection
- [`day_night_flag()`](https://chunshengliang.github.io/dataprep/reference/day_night_flag.md)
  : Day/night flag
- [`season_flag()`](https://chunshengliang.github.io/dataprep/reference/season_flag.md)
  : Season flag

## Reshaping

Fast wide-to-long and long-to-wide reshaping with SIMD + OpenMP C++
backends.

- [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
  : Fast wide-to-long data reshaping with flexible ID/measure
  specification
- [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
  : Cast a long-format data.frame into a wide format

## Data cleaning helpers

String cleaning, duplicate removal, and rule-based validation.

- [`clean_strings()`](https://chunshengliang.github.io/dataprep/reference/clean_strings.md)
  : Clean and standardize character columns
- [`deduplicate()`](https://chunshengliang.github.io/dataprep/reference/deduplicate.md)
  : Remove duplicate observations
- [`validate_data()`](https://chunshengliang.github.io/dataprep/reference/validate_data.md)
  : Validate data against a set of rules

## Sampling and summary

Stratified sampling, descriptive statistics, and percentile summaries
with matching plots.

- [`sample_data()`](https://chunshengliang.github.io/dataprep/reference/sample_data.md)
  : Random sampling with optional stratification
- [`descdata()`](https://chunshengliang.github.io/dataprep/reference/descdata.md)
  : Fast descriptive statistics
- [`descplot()`](https://chunshengliang.github.io/dataprep/reference/descplot.md)
  : View descriptive statistics via plot
- [`percdata()`](https://chunshengliang.github.io/dataprep/reference/percdata.md)
  : Calculate top and bottom percentiles of selected variables
- [`percplot()`](https://chunshengliang.github.io/dataprep/reference/percplot.md)
  : Plot top and bottom percentiles of selected variables

## Example datasets

- [`data`](https://chunshengliang.github.io/dataprep/reference/data.md)
  : Example data (particle number concentrations in SMEAR I Varrio
  forest)
- [`data1`](https://chunshengliang.github.io/dataprep/reference/data1.md)
  : Example data (aggregated particle number concentrations, SMEAR I
  Varrio forest)
