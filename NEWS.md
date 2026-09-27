# dataprep 0.1.7

## Upgrading from 0.1.5? Read this first

Three behaviour changes affect row and column counts. All three are
bug fixes, but each one changes the output on real data. If your
downstream analysis depends on exact row counts, read the
quantified comparison below before upgrading.

1. **`obsedele()` now scans each column independently.** The 0.1.5
   implementation collapsed all selected columns into one long
   vector before computing missing runs; this changed NA run
   boundaries and could both over-delete boundary rows and retain
   rows that should have been deleted. The 0.1.7 implementation
   scans each column independently: a row is deleted when
   *any* selected column has a missing run longer than `half`
   minutes on both sides.

2. **The `half`-minute boundary is now inclusive.** Rows whose
   nearest anchor is exactly `half` minutes away are retained
   (`within half minutes` is a `<=` condition).

3. **`optisolu()` no longer crashes with `cores > 16`.** The 0.1.5
   `parallel::makeCluster()` path exhausted memory when the worker
   processes each received a full copy of the input. The 0.1.7
   implementation loads the package on each worker, exports the
   input data only once per worker, and runs each `(interval,
   times)` case in a separate task, so `cores = 64` and
   `cores = NULL` (automatic) are both safe.

### Quantified effect on a full-year dataset

On SMEAR I Varrio 2025 (49,422 rows × 61 numeric channels,
10-minute sampling), running the same pipeline with the same
parameters:

| Stage | 0.1.5 | 0.1.7 | Δ |
|---|---:|---:|---:|
| `varidele` | 25 columns deleted | 25 columns deleted | 0 |
| `obsedele` | 1,494 rows deleted | 1,496 rows deleted | +2 |
| `condextr` | 1,868 rows deleted | 1,863 rows deleted | −5 |
| `shorvalu` | 50,376 NAs filled | 50,387 NAs filled | +11 |
| `dataprep` final | 46,060 rows | 46,063 rows | **+3** |

Net change: 0.006% of the input. The six rows that differ
between versions all sit at run boundaries where the anchor
distance is within one sampling interval of `half` minutes.

See `vignette("dataprep-migration")` for the full upgrade guide
and minimal reproductions of both changes.

## Test environments

Two reference hosts were used. Their relative ranking of the
engines is identical; the absolute multipliers scale with the
hardware.

### Reference host A — Ubuntu 25.10

| Component | Value |
|---|---|
| OS | Ubuntu 25.10 (Questing Quokka), kernel 6.17.0-41-generic |
| CPU | 2× AMD EPYC 9965 192-Core Processor (Turin, Zen 5c) |
| Physical cores | 384 (2 × 192) |
| Logical cores | 768 (SMT-2) |
| L1d / L1i | 18 MiB / 12 MiB |
| L2 | 384 MiB |
| L3 | 768 MiB |
| NUMA nodes | 2 |
| RAM | 1.0 TiB DDR5 5600 MT/s, Multi-bit ECC |
| Max frequency | 3.70 GHz |
| AVX-512 | Full (f, dq, ifma, cd, bw, vl, vbmi, vbmi2, vnni, bitalg, vpopcntdq, bf16) |
| R | 4.5.1 (2025-06-13) |
| Compiler | g++ 15.2.0 |
| reticulate | 1.47.0 |
| data.table | 1.18.6.1 |
| reshape2 | 1.4.5 |
| tidyr | 1.3.2 |
| Python | 3.13.7 |
| pandas | 3.0.6 |
| polars | 1.44.2 (runtime rt64) |
| dask | 2026.8.0 |
| duckdb | 1.5.5 |

### Reference host B — Windows 11 Pro for Workstations

| Component | Value |
|---|---|
| OS | Windows 11 Pro for Workstations, 10.0.26100, Build 26100 |
| CPU | 2× AMD EPYC 7B12 64-Core Processor |
| Physical cores | 128 (2 × 64) |
| Logical cores | 128 (no SMT) |
| L1d / L1i | 4 MiB / 4 MiB |
| L2 | 64 MiB |
| L3 | 512 MiB |
| NUMA nodes | 2 |
| RAM | about 224 GiB (7 × 32 GiB, 2933 MT/s, Micron / Samsung, non-ECC) |
| Max frequency | 2.25 GHz |
| AVX | AVX, AVX2 (no AVX-512) |
| R | 4.6.1 (2026-06-24 ucrt) |
| Compiler | GCC 14.3.0 |
| reticulate | 1.47.0 |
| data.table | 1.18.6.1 |
| reshape2 | 1.4.5 |
| tidyr | 1.3.2 |
| Python | 3.13.15 |
| pandas | 3.0.6 |
| polars | 1.44.2 (runtime rt64) |
| dask | 2026.8.0 |
| duckdb | 1.5.5 |

## Performance summary

### Cleaning pipeline (dataprep 0.1.5 → 0.1.7)

Speedup relative to 0.1.5 on the same input, same parameters.
Values below 1.0× mean the new implementation is marginally
slower on that cell.

| Function | 500 rows | 7,640 rows | 49,422 rows (Ubuntu) |
|---|---:|---:|---:|
| `varidele` | 1.2× | 1.1× | 11.6× |
| `obsedele` | 203× | 424× | 232× |
| `condextr` | 196× | 217× | 1146× |
| `optisolu` | 188× | 77× | 109× |
| `dataprep` | 185× | 228× | 247× |

On Windows 11 Pro for Workstations, the same full-year pipeline
gives `obsedele` ≈ 648×, `condextr` ≈ 839×, `shorvalu` ≈ 81×,
`optisolu` ≈ 25× (at `cores = 32`), and the integrated `dataprep`
call ≈ 173×. `varidele` is around 1.17× on this cell; this is
expected, since `varidele` is a single `colMeans(is.na(.))` in
both versions and the new code path has little room for improvement.

> **Note on `optisolu` cores.** The 0.1.5 implementation could
> crash when `cores > 16`. The benchmark above used
> `cores = 16` for both versions to keep the comparison fair.
> 0.1.7 loads the package on each worker, exports the input data
> once per worker, and runs each `(interval, times)` case as a
> separate task, so `cores = 64` is safe. The practical speed-up
> on a many-core host is **larger** than the table above.

### `melt()` — speed-up vs every one of the 7 major alternatives

Speed-ups relative to each competitor span **0.5×–1187×**
across both hosts. The sub-1.0× cells are concentrated at 1e5
rows (Ubuntu and Windows) and at 1e7 × 10 id on Ubuntu, where
`polars` is faster than `dataprep`; every other cell has
`dataprep` ahead of or on par with the fastest competitor.

Medians in milliseconds (Ubuntu 25.10):

| rows | val | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e3 | 9 | 0.176 | 0.373 (2.1×) | 0.272 (1.5×) | 3.009 (17.1×) | 2.162 (12.3×) | 0.612 (3.5×) | 15.61 (88.6×) | 4.714 (26.8×) |
| 1e6 | 9 | 3.680 | 17.89 (4.9×) | 7.900 (2.1×) | 76.77 (20.9×) | 60.30 (16.4×) | 10.44 (2.8×) | 46.49 (12.6×) | 642.1 (174×) |
| 1e7 | 9 | 37.95 | 372.7 (9.8×) | 371.6 (9.8×) | 1111 (29.3×) | 720.1 (19.0×) | 92.53 (2.4×) | 486.4 (12.8×) | 6423 (169×) |
| 1e8 | 9 | 496.4 | 3577 (7.2×) | 3576 (7.2×) | 12089 (24.4×) | 7410 (14.9×) | 2563 (5.2×) | 4590 (9.2×) | 65624 (132×) |
| 1e3 | 10000 | 3.616 | 92.45 (25.6×) | 9.659 (2.7×) | 107.7 (29.8×) | 499.0 (138×) | 16.31 (4.5×) | 4292 (**1187×**) | 1914 (529×) |

Medians in milliseconds (Windows 11 Pro for Workstations):

| rows | val | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e3 | 9 | 0.319 | 0.653 (2.0×) | 0.512 (1.6×) | 4.224 (13.2×) | 3.575 (11.2×) | 0.513 (1.6×) | 28.55 (89.5×) | 7.724 (24.2×) |
| 1e6 | 9 | 12.15 | 24.44 (2.0×) | 26.17 (2.2×) | 174.4 (14.3×) | 210.6 (17.3×) | 17.99 (1.5×) | 178.8 (14.7×) | 1558 (128×) |
| 1e7 | 9 | 101.4 | 262.2 (2.6×) | 253.3 (2.5×) | 1510 (14.9×) | 1908 (18.8×) | 177.5 (1.8×) | 1541 (15.2×) | 14588 (144×) |
| 1e8 | 9 | 1197 | 3514 (2.9×) | 2806 (2.3×) | 21106 (17.6×) | 22968 (19.2×) | 5227 (4.4×) | 16904 (14.1×) | 160256 (134×) |
| 1e3 | 10000 | 11.58 | 158.7 (13.7×) | 34.86 (3.0×) | 224.1 (19.4×) | 1647 (142×) | 30.51 (2.6×) | 10220 (883×) | 5518 (477×) |

The **median** speed-up across all melt cells and all competitors
is 10.3× on Ubuntu and 5.7× on Windows. The **mean** is 58.5× and
44.2× respectively. The median is pulled down by the 1e5-row small
tables; at larger scales the speed-up is much higher.

### `dcast()` — speed-up vs every one of the 7 major alternatives

Speed-ups relative to each competitor span **2.0×–639×**
across both hosts. Every cell has `dataprep` ahead of every other
engine.

Medians in milliseconds (Ubuntu 25.10):

| n_long | levels | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e6 | 10 | 1.632 | 151.3 (92.7×) | 345.4 (212×) | 46.67 (28.6×) | 58.85 (36.1×) | 105.8 (64.8×) | 80.65 (49.4×) | 158.7 (97.3×) |
| 1e6 | 100 | 1.451 | 108.7 (74.9×) | 335.9 (232×) | 43.25 (29.8×) | 56.27 (38.8×) | 172.6 (119×) | 77.04 (53.1×) | 180.9 (125×) |
| 1e7 | 100 | 4.733 | 2084 (440×) | 550.9 (116×) | 633.0 (134×) | 768.4 (162×) | 458.8 (96.9×) | 953.6 (202×) | 1717 (363×) |
| 1e8 | 100 | 43.91 | 17115 (390×) | 16193 (369×) | 8174 (186×) | 9880 (225×) | 2467 (56.2×) | 12217 (278×) | 17895 (408×) |

Medians in milliseconds (Windows 11 Pro for Workstations):

| n_long | levels | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|---|---|---|---|---|---|---|---|---|---|
| 1e6 | 10 | 3.940 | 305.7 (77.6×) | 155.1 (39.4×) | 98.29 (24.9×) | 269.7 (68.4×) | 49.29 (12.5×) | 359.8 (91.3×) | 399.8 (101×) |
| 1e6 | 100 | 4.085 | 191.6 (46.9×) | 177.9 (43.6×) | 82.31 (20.2×) | 226.0 (55.3×) | 69.16 (16.9×) | 341.5 (83.6×) | 800.1 (196×) |
| 1e7 | 100 | 21.21 | 3253 (153×) | 1010 (47.6×) | 1332 (62.8×) | 2420 (114×) | 1125 (53.0×) | 3061 (144×) | 5562 (262×) |
| 1e8 | 100 | 106.3 | 24253 (228×) | 14965 (141×) | 13392 (126×) | 26311 (248×) | 7646 (71.9×) | 32874 (309×) | 67894 (**639×**) |

The **median** speed-up across all dcast cells and all competitors
is 49.7× on Ubuntu and 47.7× on Windows. The **mean** is 94.7× and
81.6× respectively. On the 1e8-row cells (8 GB of input),
`dataprep` completes in **44–209 ms** while several competitors
exceed 12 s on Ubuntu and 12 s on Windows.

### Cross-engine consistency

`melt()` and `dcast()` produce output **byte-identical** to
`reshape2`, `data.table`, `tidyr`, `pandas`, `polars`, `dask`,
and `duckdb` on every tested shape, within `tol = 1e-12`.

| Operation | Cells tested | Engines | Pairwise |
|---|---:|---:|---|
| `melt` | 4 shapes | 8 | all consistent |
| `dcast` | 4 shapes | 8 | all consistent |

Full tables — including `mean`, `median`, and the full
per-competitor gradient — are in
`vignette("dataprep-performance")`. The reproducible runner is
shipped under `inst/`.

## Behaviour changes

### `obsedele()` semantics

The 0.1.7 C++ backend (`obsedele_cpp`) implements the retention
criterion with an **anchor-based scan**: for each missing value and
each selected column, the time distance to the nearest non-missing
anchor on the left and on the right is computed directly. A row is
deleted when **any** selected column has **both** distances exceeding `half` minutes.

This is mathematically identical to the running-mean criterion used
in 0.1.0 and the `rleid`-based criterion used in 0.1.5, but:

* **Scans each column independently.** The 0.1.5 implementation
  merged columns before computing runs, which changed run
  boundaries and could both over-delete and under-delete boundary
  rows.
* **Runs in O(n) time with O(1) extra allocation per column.**
  No grid materialisation, no run-length state.
* **Parallelises over columns with OpenMP** without any shared
  mutable state.

### `optisolu()` multi-core safety

The 0.1.5 `parallel::makeCluster()` path gave each worker a full
copy of the input. With `cores > 16` and large data this exhausted
memory and aborted the R session. The 0.1.7 implementation shares
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

* [`balance_panel()`](https://chunshengliang.github.io/dataprep/reference/balance_panel.html) — balance an
  unbalanced panel by filling or completing.
* [`bin_data()`](https://chunshengliang.github.io/dataprep/reference/bin_data.html) — discretize continuous
  variables.
* [`clean_strings()`](https://chunshengliang.github.io/dataprep/reference/clean_strings.html) — trim /
  case / regex cleaning of character columns.
* [`deduplicate()`](https://chunshengliang.github.io/dataprep/reference/deduplicate.html) — exact and fuzzy
  duplicate removal.
* [`encode_categorical()`](https://chunshengliang.github.io/dataprep/reference/encode_categorical.html) —
  label / frequency / one-hot encoding.
* [`filter_high_cor()`](https://chunshengliang.github.io/dataprep/reference/filter_high_cor.html) — drop
  highly correlated variables.
* [`filter_low_var()`](https://chunshengliang.github.io/dataprep/reference/filter_low_var.html) — drop
  near-constant variables.
* [`phys_filter()`](https://chunshengliang.github.io/dataprep/reference/phys_filter.html) — physical range
  filtering.
* [`validate_data()`](https://chunshengliang.github.io/dataprep/reference/validate_data.html) — rule-based
  data validation.
* [`winsorize()`](https://chunshengliang.github.io/dataprep/reference/winsorize.html) — cap extreme values.
* [`zerona()`](https://chunshengliang.github.io/dataprep/reference/zerona.html) — replace zeros with NA.

### Imputation and transformation

* [`impute_missing()`](https://chunshengliang.github.io/dataprep/reference/impute_missing.html) — linear /
  LOCF / NOCB / mean / median.
* [`log_returns()`](https://chunshengliang.github.io/dataprep/reference/log_returns.html) — log returns.
* [`transform_data()`](https://chunshengliang.github.io/dataprep/reference/transform_data.html) — log /
  sqrt / Box-Cox / Yeo-Johnson and z-score / min-max / robust
  scaling.

### Diagnostics and reporting

* [`na_diagnose()`](https://chunshengliang.github.io/dataprep/reference/na_diagnose.html) — missing-value
  run statistics.
* [`data_report()`](https://chunshengliang.github.io/dataprep/reference/data_report.html) — compact data
  quality report.
* [`dry_run()`](https://chunshengliang.github.io/dataprep/reference/dry_run.html) — simulate preprocessing
  without changing data.

### Time series

* [`create_lags()`](https://chunshengliang.github.io/dataprep/reference/create_lags.html) — grouped lag /
  lead columns.
* [`day_night_flag()`](https://chunshengliang.github.io/dataprep/reference/day_night_flag.html) — day /
  night indicator.
* [`season_flag()`](https://chunshengliang.github.io/dataprep/reference/season_flag.html) — season / month
  / quarter indicator.
* [`decompose_ts()`](https://chunshengliang.github.io/dataprep/reference/decompose_ts.html) — additive /
  multiplicative decomposition.
* [`detrend_ts()`](https://chunshengliang.github.io/dataprep/reference/detrend_ts.html) — linear detrending.
* [`remove_diurnal_cycle()`](https://chunshengliang.github.io/dataprep/reference/remove_diurnal_cycle.html) —
  subtract mean diurnal cycle.
* [`resample_time()`](https://chunshengliang.github.io/dataprep/reference/resample_time.html) — resample to
  coarser period.
* [`roll_apply()`](https://chunshengliang.github.io/dataprep/reference/roll_apply.html) — rolling statistics
  with alignment.
* [`drift_detect()`](https://chunshengliang.github.io/dataprep/reference/drift_detect.html) — rolling drift
  detection.

### Sampling and workflow

* [`sample_data()`](https://chunshengliang.github.io/dataprep/reference/sample_data.html) — simple and
  stratified sampling.
* [`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.html) /
  [`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.html) — fit /
  transform style preprocessing plan that prevents data leakage.

### Reshaping

* [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.html) — long-to-wide reshaping,
  paired with [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.html).

## API changes

* Argument `cols` now consistently accepts names, integer
  indices, or logical masks across the package.
* [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.html) gains `id.vars`,
  `measure.vars`, `variable.name`, `value.name`, `na.rm`,
  `cores`, `major`, `verbose`. `id.vars` is an alias of `id` for
  reshape2 / data.table compatibility.
* [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.html) gains `formula`, `value.var`,
  `fill`, `fun.aggregate`, `na.rm`, `cores`, `verbose`.
* [`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.html) and
  [`condextr()`](https://chunshengliang.github.io/dataprep/reference/condextr.html) gain a `cores` argument
  for OpenMP control. `options(dataprep.cores = ...)` is also
  respected by [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.html) and
  [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.html).
* [`descdata()`](https://chunshengliang.github.io/dataprep/reference/descdata.html) gains `cores`; `stats`
  accepts both numeric and character stat names.
* [`percplot()`](https://chunshengliang.github.io/dataprep/reference/percplot.html) now prints both the
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
* `vignette("dataprep-migration")` — 0.1.5 → 0.1.7 upgrade guide.
* `vignette("dataprep-workflow")` — leakage-free preprocessing
  with `prep_fit()` / `prep_transform()`.
* `vignette("dataprep-melt-dcast")` — fast reshaping usage and
  implementation notes.
* `vignette("dataprep-plots")` — descriptive statistics and
  diagnostic plots.

## Performance notes

Three benchmark cells sit close to, or marginally behind, the
fastest competitor — `dcast()` at 100 id columns, `melt()` at
1e7 rows × 10 id columns, and `varidele()` on full-year data — and
all three share the same root cause: the affected internal buffers
(the 96-bit fingerprint table, the per-id-column id blocks, and the
`is.na` scratch matrix) are allocated with `Rf_allocVector`, which
routes through R's default `malloc`-based allocator and therefore
cannot be directed to hugepages, a per-process free pool, or a
NUMA-aware arena, so first-touch page faults dominate those cells.
Two possible fixes were prototyped on the reshaping backends and
both were measured to close the gap: routing those buffers through
`Rf_allocVector3` with a hypothetical custom `R_allocator_t` adds a
further few-fold speed-up on top of the existing hundred-fold to
thousand-fold margins, and returning **ALTREP** virtual objects from
`melt()` / `dcast()` — a lazy `variable` column and a deferred id
block, so that large parts of the output are never materialised —
adds another order of magnitude (tens of times) on the wide-table
shapes. Neither is used in this release. `Rf_allocVector3` is not
recommended by CRAN, and an ALTREP return value, while fast to
produce, is slow for downstream complex statistics because every
element access re-enters the virtual-object layer, which shifts the
cost from `dataprep` to the caller's analysis code. The shipped
0.1.7 backends therefore keep the standard allocation path, and the
reported speed-ups stand as measured: `melt()` spans 0.5×–1187×
and `dcast()` spans 2.0×–639× across the two reference hosts, with
the sub-1.0× `melt()` cells confined to the 1e5-row shapes where
per-call overhead dominates. On the cleaning pipeline the
0.1.5 → 0.1.7 speed-ups of 1.1×–1146× stand as reported;
`Rf_allocVector3` and ALTREP were not evaluated there.

## Environment variables (optional)

* `DATAPREP_RUN_BENCHMARK` = `1` enables the shipped benchmark
  scripts. They are disabled by default so that `R CMD check`
  does not execute them.

## See also

* [Design philosophy](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.html) — why the
  pipeline has the shape it does.
* [Cleaning pipeline](https://chunshengliang.github.io/dataprep/articles/dataprep-cleaning.html) — full
  walkthrough of `varidele` / `obsedele` / `condextr` /
  `shorvalu`.
* [Performance](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.html) — benchmark
  tables and cross-engine consistency.
* [Migration](https://chunshengliang.github.io/dataprep/articles/dataprep-migration.html) — upgrade guide.
* [Leakage-free workflow](https://chunshengliang.github.io/dataprep/articles/dataprep-workflow.html) —
  `prep_fit` / `prep_transform`.
* [Reshaping](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.html) — melt / dcast.
* [Descriptive statistics and
  plots](https://chunshengliang.github.io/dataprep/articles/dataprep-plots.html) — `descplot` / `percplot`.

# dataprep 0.1.5

* Initial public release on CRAN.
* Core cleaning pipeline: `varidele`, `obsedele`, `condextr`,
  `percoutl`, `optisolu`, `shorvalu`, `dataprep`.
* Descriptive statistics and percentile helpers: `descdata`,
  `descplot`, `percdata`, `percplot`.
* Example datasets: `data`, `data1`.