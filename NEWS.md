# dataprep 0.1.6

## Major performance improvements

Most routines have been rewritten in C++ via Rcpp, with optional
OpenMP parallelization and SIMD acceleration (AVX2 / AVX-512) on
supported hardware. Benchmarks against dataprep 0.1.5 on a
7,640-row dataset ([`data1`](reference/data1.html)) show:

  * [`obsedele()`](reference/obsedele.html)       :
    ~450x faster (11.4 s   -> 0.025 s)
  * [`condextr()`](reference/condextr.html)       :
    ~23x  faster (2.25 min -> 5.73 s)
  * [`optisolu()`](reference/optisolu.html)       :
    ~77x  faster (13.8 min -> 10.8 s)
  * [`shorvalu()`](reference/shorvalu.html)       :
    ~90x  faster (0.99 s   -> 0.011 s)
  * [`dataprep()`](reference/dataprep.html)       :
    ~16x  faster (1.28 min -> 4.9 s)

[`melt()`](reference/melt.html) and
[`dcast()`](reference/dcast.html) use a SIMD + OpenMP C++ backend.
On a 1,000,000 x 10 table, `melt()` completes in ~5 ms (median),
about 7-8x faster than `reshape2::melt`, ~24x faster than
`tidyr::pivot_longer`, and ~30x faster than `pandas.melt`.

See the
[reshape vignette](articles/dataprep-melt-dcast.html) for the full
comparison and the reproducible benchmark scripts shipped under
`inst/`.

## Bug fixes

  * [`obsedele()`](reference/obsedele.html) no longer over-deletes
    boundary rows. The old R implementation collapsed all selected
    columns into one vector before `rleid()`, which merged NA runs
    across columns and inflated their effective length. The new C++
    version scans each column independently and only removes grid
    points whose distance from both ends of a run exceeds `half`.
    Boundary rows where at least one side of the NA run is shorter
    than `half` are now correctly retained. This can change the
    result of [`condextr()`](reference/condextr.html) for datasets
    with irregular missing patterns; see
    the [cleaning vignette](articles/dataprep-cleaning.html) for
    details.
  * `dcast_cpp()` had a wrong R type when setting output column
    names (`Rf_mkString` instead of `Rf_mkChar`), which raised
    `SET_STRING_ELT() must be a 'CHARSXP' not a 'character'`.
  * [`prep_fit()`](reference/prep_fit.html) now stores the grouping
    column in the plan so that
    [`prep_transform()`](reference/prep_transform.html) can re-apply
    grouped operations correctly.

## New functions

Data cleaning:

  * [`balance_panel()`](reference/balance_panel.html) - balance a
    panel by filling or completing
  * [`bin_data()`](reference/bin_data.html) - discretize continuous
    variables
  * [`clean_strings()`](reference/clean_strings.html) - trim /
    case / regex cleaning of characters
  * [`deduplicate()`](reference/deduplicate.html) - exact and fuzzy
    duplicate removal
  * [`encode_categorical()`](reference/encode_categorical.html) -
    label / frequency / one-hot encoding
  * [`filter_high_cor()`](reference/filter_high_cor.html) - drop
    highly correlated variables
  * [`filter_low_var()`](reference/filter_low_var.html) - drop
    near-constant variables
  * [`phys_filter()`](reference/phys_filter.html) - physical range
    filtering
  * [`validate_data()`](reference/validate_data.html) - rule-based
    data validation
  * [`winsorize()`](reference/winsorize.html) - cap extreme values
  * [`zerona()`](reference/zerona.html) - replace zeros with NA

Imputation and transformation:

  * [`impute_missing()`](reference/impute_missing.html) - linear /
    LOCF / NOCB / mean / median
  * [`log_returns()`](reference/log_returns.html) - log returns
  * [`transform_data()`](reference/transform_data.html) - log /
    sqrt / Box-Cox / Yeo-Johnson and z-score / min-max / robust
    scaling

Diagnostics and reporting:

  * [`na_diagnose()`](reference/na_diagnose.html) - missing-value
    run statistics
  * [`data_report()`](reference/data_report.html) - compact data
    quality report
  * [`dry_run()`](reference/dry_run.html) - simulate preprocessing
    without changing data

Time series:

  * [`create_lags()`](reference/create_lags.html) - grouped lag /
    lead columns
  * [`day_night_flag()`](reference/day_night_flag.html) - day /
    night indicator
  * [`season_flag()`](reference/season_flag.html) - season / month
    / quarter indicator
  * [`decompose_ts()`](reference/decompose_ts.html) - additive /
    multiplicative decomposition
  * [`detrend_ts()`](reference/detrend_ts.html) - linear detrending
  * [`remove_diurnal_cycle()`](reference/remove_diurnal_cycle.html) -
    subtract mean diurnal cycle
  * [`resample_time()`](reference/resample_time.html) - resample to
    coarser period
  * [`roll_apply()`](reference/roll_apply.html) - rolling statistics
    with alignment
  * [`drift_detect()`](reference/drift_detect.html) - rolling drift
    detection

Sampling and workflow:

  * [`sample_data()`](reference/sample_data.html) - simple and
    stratified sampling
  * [`prep_fit()`](reference/prep_fit.html) /
    [`prep_transform()`](reference/prep_transform.html) - fit /
    transform style preprocessing plan that prevents data leakage

Reshaping:

  * [`dcast()`](reference/dcast.html) - long-to-wide reshaping,
    paired with [`melt()`](reference/melt.html)

## API changes

  * Argument `cols` now consistently accepts names, integer
    indices, or logical masks across the package.
  * [`melt()`](reference/melt.html) gains `id.vars`,
    `measure.vars`, `variable.name`, `value.name`, `na.rm`,
    `cores`, `major`, `verbose`. `id.vars` is an alias of `id` for
    reshape2 / data.table compatibility.
  * [`dcast()`](reference/dcast.html) gains `formula`, `value.var`,
    `fill`, `fun.aggregate`, `na.rm`, `cores`, `verbose`.
  * [`obsedele()`](reference/obsedele.html) and
    [`condextr()`](reference/condextr.html) gain a `cores` argument
    for OpenMP control. `options(dataprep.cores = ...)` is also
    respected by [`melt()`](reference/melt.html) and
    [`dcast()`](reference/dcast.html).
  * [`descdata()`](reference/descdata.html) gains `cores`; `stats`
    accepts both numeric and character stat names.

## Deprecated / removed

  * `condextr_cpp()` and `condextr_inplace_cpp()` are no longer
    exported. Use [`condextr()`](reference/condextr.html), which
    calls `condextr_all_cpp()` internally.
  * `lin_interp_cpp()` and `lin_interp_matrix_cpp()` remain
    available; [`impute_missing(method = "linear")`](reference/impute_missing.html)
    is the recommended entry point.

## Environment variables (optional)

  * `DATAPREP_HUGEPAGE` = `none` (default) | `2mb` | `1gb`
    controls the hugepage mode of the C++ allocator used by
    [`melt()`](reference/melt.html) and
    [`dcast()`](reference/dcast.html).
  * `DATAPREP_POPULATE` = `1` pre-populates memory with
    `MADV_POPULATE_WRITE` to reduce first-touch latency.
  * `DATAPREP_RUN_BENCHMARK` = `1` enables the shipped benchmark
    scripts. They are disabled by default so that `R CMD check`
    does not execute them.

## See also

  * [Cleaning pipeline](articles/dataprep-cleaning.html) - full
    walkthrough of `varidele` / `obsedele` / `condextr` / `shorvalu`.
  * [Reshaping](articles/dataprep-melt-dcast.html) - melt / dcast
    usage and cross-engine benchmarks.
  * [Leakage-free workflow](articles/dataprep-workflow.html) -
    `prep_fit` / `prep_transform`.
  * [Descriptive statistics and plots](articles/dataprep-plots.html) -
    `descplot` / `percplot`.

# dataprep 0.1.5

  * Initial public release on CRAN.
  * Core cleaning pipeline: `varidele`, `obsedele`, `condextr`,
    `percoutl`, `optisolu`, `shorvalu`, `dataprep`.
  * Descriptive statistics and percentile helpers: `descdata`,
    `descplot`, `percdata`, `percplot`.
  * Example datasets: `data`, `data1`.