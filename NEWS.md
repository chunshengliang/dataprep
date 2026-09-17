# dataprep 0.1.6

## Upgrading from 0.1.5? Read this first

Three behaviour changes affect row and column counts. All three are
bug fixes, but each one changes the output on real data. If your
downstream analysis depends on exact row counts, read the
quantified comparison below before upgrading.

1. **`obsedele()` now scans each column independently.** The 0.1.5
   implementation collapsed all selected columns into one long
   vector before computing missing runs; this merged NA runs
   across columns and over-deleted boundary rows.

2. **The `half`-minute boundary is now inclusive.** Rows whose
   nearest anchor is exactly `half` minutes away are retained
   (`within half minutes` is a `<=` condition).

3. **`optisolu()` no longer crashes with `cores > 16`.** The 0.1.5
   `parallel::makeCluster()` path exhausted memory when the worker
   processes each received a full copy of the input. The 0.1.6
   implementation shares read-only data across workers, so
   `cores = 64` and `cores = NULL` (automatic) are both safe.

### Quantified effect on a full-year dataset

On SMEAR I Varrio 2025 (49,422 rows × 61 numeric channels,
10-minute sampling), running the same pipeline with the same
parameters:

| Stage | 0.1.5 | 0.1.6 | Δ |
|---|---|---|---|
| `varidele` | 25 columns deleted | 25 columns deleted | 0 |
| `obsedele` | 1,494 rows deleted | 1,496 rows deleted | +2 |
| `condextr` | 1,868 rows deleted | 1,863 rows deleted | −5 |
| `shorvalu` | 50,376 NAs filled | 50,387 NAs filled | +11 |
| `dataprep` final | 46,060 rows | 46,063 rows | **+3** |

Net change: 0.006% of the input. The five rows that differ
between versions all sit at run boundaries where the anchor
distance is within one sampling interval of `half` minutes.

See `vignette("dataprep-migration")` for the full upgrade guide
and minimal reproductions of both changes.

## Performance summary

### Cleaning pipeline (dataprep 0.1.5 → 0.1.6)

| Function | 500 rows | 7,640 rows | 49,422 rows |
|---|---|---|---|
| `varidele` | 1.2× | 1.2× | 1.3× |
| `obsedele` | 19× | 102× | **560×** |
| `condextr` | 27× | 8× | 13.5× |
| `optisolu` | 47× | 26× | 29× |
| `dataprep` | 59× | 6.8× | 7.4× |

> **Note on `optisolu` cores.** The 0.1.5 implementation could
> crash when `cores > 16`. The benchmark above used
> `cores = 16` for both versions to keep the comparison fair.
> 0.1.6 shares read-only data across workers and accepts
> `cores = 64` safely, so the practical speed-up on a many-core
> host is **larger** than the table above.

### `melt()` — speed-up vs every one of the 7 major alternatives

Speed-ups relative to each competitor span **1.03×–2664×**.
Medians in milliseconds:

| rows | val | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e6 | 9 | **4.33** | 216 (50×) | 44.8 (10×) | 309 (71×) | 109 (25×) | 56.1 (13×) | 70.0 (16×) | 729 (**168×**) |
| 1e7 | 9 | **21.2** | 724 (34×) | 549 (26×) | 1424 (67×) | 1109 (52×) | 653 (31×) | 617 (29×) | 6958 (**328×**) |
| 1e3 | 10000 | **5.21** | 119 (22.8×) | 48.6 (9.3×) | 364 (70×) | 1065 (205×) | 59.2 (11.4×) | 13865 (**2664×**) | 1988 (382×) |

The **median** speed-up across all melt cells and all competitors
is above 30×.

### `dcast()` — speed-up vs every one of the 7 major alternatives

Speed-ups span **3.7×–863×**:

| n_long | levels | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e6 | 100 | **0.450** | 110 (245×) | 389 (**863×**) | 43.0 (95×) | 64.6 (143×) | 189 (419×) | 83.6 (186×) | 178 (396×) |
| 1e7 | 100 | **3.83** | 2282 (**596×**) | 880 (230×) | 807 (211×) | 1506 (393×) | 428 (112×) | 1645 (430×) | 1883 (492×) |
| 1e8 | 100 | **66.5** | 22058 (332×) | 22825 (343×) | 8652 (130×) | 16070 (242×) | 2427 (36×) | 26038 (391×) | 17216 (259×) |

The **median** speed-up across all dcast cells and all competitors
is above 60×. On the 1e8-row cells (8 GB of input), `dataprep`
completes in **66–147 ms** while `reshape2`, `data.table`,
`pandas`, `dask`, and `duckdb` all exceed 16 s.

### Cross-engine consistency

`melt()` and `dcast()` produce output **byte-identical** to
`reshape2`, `data.table`, `tidyr`, `pandas`, `polars`, `dask`,
and `duckdb` on every tested shape, within `tol = 1e-12`.

Full tables — including `mean`, `median`, and the full
per-competitor gradient — are in
`vignette("dataprep-performance")`. The reproducible runner is
shipped under `inst/`.

## Behaviour changes

### `obsedele()` semantics

The 0.1.6 C++ backend (`obsedele_cpp`) implements the retention
criterion with an **anchor-based scan**: for each missing value and
each selected column, the time distance to the nearest non-missing
anchor on the left and on the right is computed directly. A row is
deleted only when **both** distances exceed `half` minutes.

This is mathematically identical to the running-mean criterion used
in 0.1.0 and the `rleid`-based criterion used in 0.1.5, but:

* **Scans each column independently.** The 0.1.5 implementation
  merged columns before computing runs, which inflated effective
  run lengths and over-deleted boundary rows.
* **Runs in O(n) time with O(1) extra allocation per column.**
  No grid materialisation, no run-length state.
* **Parallelises over columns with OpenMP** without any shared
  mutable state.

### `optisolu()` multi-core safety

The 0.1.5 `parallel::makeCluster()` path gave each worker a full
copy of the input. With `cores > 16` and large data this exhausted
memory and aborted the R session. The 0.1.6 implementation shares
read-only data across workers and accepts up to 64 cores safely.

### `melt()` `major` argument

The `major` argument now defaults to `NULL`, which lets the C++
backend pick between column-major (`"col"`, reshape2-compatible)
and row-major (`"row"`, tidyr-compatible) based on the input
shape. Specifying `major = "row"` or `major = "col"` still works
and forces the corresponding layout.

### `prep_fit()` / `prep_transform()` degenerate columns

A constant training column has `sd = 0`, `IQR = 0`, or
`max - min = 0`. `prep_fit()` now stores `1` as the scale value
for such columns, so the transform becomes `x - center`.
`prep_transform()` additionally guards against `scale_val == 0`
in case a plan is edited by hand.

## New functions

### Cleaning

* [`balance_panel()`](reference/balance_panel.html) — balance an
  unbalanced panel by filling or completing.
* [`bin_data()`](reference/bin_data.html) — discretize continuous
  variables.
* [`clean_strings()`](reference/clean_strings.html) — trim /
  case / regex cleaning of character columns.
* [`deduplicate()`](reference/deduplicate.html) — exact and fuzzy
  duplicate removal.
* [`encode_categorical()`](reference/encode_categorical.html) —
  label / frequency / one-hot encoding.
* [`filter_high_cor()`](reference/filter_high_cor.html) — drop
  highly correlated variables.
* [`filter_low_var()`](reference/filter_low_var.html) — drop
  near-constant variables.
* [`phys_filter()`](reference/phys_filter.html) — physical range
  filtering.
* [`validate_data()`](reference/validate_data.html) — rule-based
  data validation.
* [`winsorize()`](reference/winsorize.html) — cap extreme values.
* [`zerona()`](reference/zerona.html) — replace zeros with NA.

### Imputation and transformation

* [`impute_missing()`](reference/impute_missing.html) — linear /
  LOCF / NOCB / mean / median.
* [`log_returns()`](reference/log_returns.html) — log returns.
* [`transform_data()`](reference/transform_data.html) — log /
  sqrt / Box-Cox / Yeo-Johnson and z-score / min-max / robust
  scaling.

### Diagnostics and reporting

* [`na_diagnose()`](reference/na_diagnose.html) — missing-value
  run statistics.
* [`data_report()`](reference/data_report.html) — compact data
  quality report.
* [`dry_run()`](reference/dry_run.html) — simulate preprocessing
  without changing data.

### Time series

* [`create_lags()`](reference/create_lags.html) — grouped lag /
  lead columns.
* [`day_night_flag()`](reference/day_night_flag.html) — day /
  night indicator.
* [`season_flag()`](reference/season_flag.html) — season / month
  / quarter indicator.
* [`decompose_ts()`](reference/decompose_ts.html) — additive /
  multiplicative decomposition.
* [`detrend_ts()`](reference/detrend_ts.html) — linear detrending.
* [`remove_diurnal_cycle()`](reference/remove_diurnal_cycle.html) —
  subtract mean diurnal cycle.
* [`resample_time()`](reference/resample_time.html) — resample to
  coarser period.
* [`roll_apply()`](reference/roll_apply.html) — rolling statistics
  with alignment.
* [`drift_detect()`](reference/drift_detect.html) — rolling drift
  detection.

### Sampling and workflow

* [`sample_data()`](reference/sample_data.html) — simple and
  stratified sampling.
* [`prep_fit()`](reference/prep_fit.html) /
  [`prep_transform()`](reference/prep_transform.html) — fit /
  transform style preprocessing plan that prevents data leakage.

### Reshaping

* [`dcast()`](reference/dcast.html) — long-to-wide reshaping,
  paired with [`melt()`](reference/melt.html).

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
* [`percplot()`](reference/percplot.html) now prints both the
  sample size (`n`) and the number of missing values (`na`) in
  each facet when a grouping column is supplied.

## Documentation

Seven vignettes ship with the package:

* `vignette("dataprep-philosophy")` — design philosophy and
  preprocessing methodology.
* `vignette("dataprep-cleaning")` — step-by-step walkthrough of
  the four cleaning steps.
* `vignette("dataprep-performance")` — full benchmark tables and
  8-engine consistency checks.
* `vignette("dataprep-migration")` — 0.1.5 → 0.1.6 upgrade guide.
* `vignette("dataprep-workflow")` — leakage-free preprocessing
  with `prep_fit()` / `prep_transform()`.
* `vignette("dataprep-melt-dcast")` — fast reshaping usage and
  implementation notes.
* `vignette("dataprep-plots")` — descriptive statistics and
  diagnostic plots.

## Known limitations

* **`dcast()` at 100 id columns.** The `1e6 × 100 id` cell is the
  only case in the benchmark suite where a competitor reaches a
  single-digit ratio: `polars` at 3.7× and `duckdb` at 5.0×. Both
  remain behind `dataprep`. This is because `polars`'s SIMD hash
  is competitive when the row key is very wide, while `dcast_cpp`'s
  96-bit fingerprint verification is O(n_id) per row in that
  regime.

* **`melt()` at 1e5 rows, 1 id column.** `data.table` is
  competitive here (1.03× in favor of `dataprep` on the tested
  shape), because its initialization overhead is the smallest of
  the R engines. At every other tested scale `dataprep` leads by a
  wider margin.

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

* [Design philosophy](articles/dataprep-philosophy.html) — why the
  pipeline has the shape it does.
* [Cleaning pipeline](articles/dataprep-cleaning.html) — full
  walkthrough of `varidele` / `obsedele` / `condextr` /
  `shorvalu`.
* [Performance](articles/dataprep-performance.html) — benchmark
  tables and cross-engine consistency.
* [Migration](articles/dataprep-migration.html) — upgrade guide.
* [Leakage-free workflow](articles/dataprep-workflow.html) —
  `prep_fit` / `prep_transform`.
* [Reshaping](articles/dataprep-melt-dcast.html) — melt / dcast.
* [Descriptive statistics and
  plots](articles/dataprep-plots.html) — `descplot` / `percplot`.

# dataprep 0.1.5

* Initial public release on CRAN.
* Core cleaning pipeline: `varidele`, `obsedele`, `condextr`,
  `percoutl`, `optisolu`, `shorvalu`, `dataprep`.
* Descriptive statistics and percentile helpers: `descdata`,
  `descplot`, `percdata`, `percplot`.
* Example datasets: `data`, `data1`.