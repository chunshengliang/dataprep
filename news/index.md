# Changelog

## dataprep 0.1.7

### Upgrading from 0.1.5? Read this first

Three behaviour changes affect row and column counts. All three are bug
fixes, but each one changes the output on real data. If your downstream
analysis depends on exact row counts, read the quantified comparison
below before upgrading.

1.  **[`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.md)
    now scans each column independently.** The 0.1.5 implementation
    collapsed all selected columns into one long vector before computing
    missing runs; this changed NA run boundaries and could both
    over-delete boundary rows and retain rows that should have been
    deleted. The 0.1.7 implementation scans each column independently: a
    row is deleted when *any* selected column has a missing run longer
    than `half` minutes on both sides.

2.  **`half` is now always in minutes, and the boundary is inclusive.**
    In 0.1.5, `half` counted grid rows in units of `by`: with
    `by = "5 min", half = 30` the effective window was 150 minutes. In
    0.1.7, `half` is always in minutes, independent of `by`. Rows whose
    nearest anchor is exactly `half` minutes away are retained
    (`within half minutes` is a `<=` condition).

3.  **[`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
    no longer crashes with `cores > 16`.** The 0.1.5
    [`parallel::makeCluster()`](https://rdrr.io/r/parallel/makeCluster.html)
    path exhausted memory when the worker processes each received a full
    copy of the input. The 0.1.7 implementation loads the package on
    each worker, exports the input data only once per worker, and runs
    each `(interval, times)` case in a separate task, so `cores = 64`
    and `cores = NULL` (automatic) are both safe.

#### Quantified effect on a full-year dataset

On SMEAR I Varrio 2025 (49,422 rows × 61 numeric channels, 10-minute
sampling), running the same pipeline with the same parameters:

| Stage            |              0.1.5 |              0.1.7 |      Δ |
|------------------|-------------------:|-------------------:|-------:|
| `varidele`       | 25 columns deleted | 25 columns deleted |      0 |
| `obsedele`       | 1,494 rows deleted | 1,496 rows deleted |     +2 |
| `condextr`       | 1,868 rows deleted | 1,863 rows deleted |     −5 |
| `shorvalu`       |  50,376 NAs filled |  50,387 NAs filled |    +11 |
| `dataprep` final |        46,060 rows |        46,063 rows | **+3** |

Net change: 0.006% of the input. The six rows that differ between
versions all sit at run boundaries where the anchor distance is within
one sampling interval of `half` minutes.

See
[`vignette("dataprep-migration")`](https://chunshengliang.github.io/dataprep/articles/dataprep-migration.md)
for the full upgrade guide and minimal reproductions of both changes.

### Test environments

Two reference hosts were used. Their relative ranking of the engines is
identical; the absolute multipliers scale with the hardware.

#### Reference host A — Ubuntu 25.10

| Component | Value |
|----|----|
| OS | Ubuntu 25.10 (Questing Quokka), kernel 6.17.0-41-generic |
| CPU | 2× AMD EPYC 9965 192-Core Processor (Turin, Zen 5c) |
| Physical cores | 384 (2 × 192) |
| Logical cores | 768 (SMT-2) |
| L1d / L1i | 18 MiB / 12 MiB |
| L2 | 384 MiB |
| L3 | 768 MiB |
| NUMA nodes | 2 |
| RAM | 1.0 TiB (16 × 64 GiB Micron, DDR5-5600, Multi-bit ECC) |
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

#### Reference host B — Windows 11 Pro for Workstations

| Component | Value |
|----|----|
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

### Performance summary

#### Cleaning pipeline (dataprep 0.1.5 → 0.1.7)

Speedup relative to 0.1.5 on the same input, same parameters. Values
below 1.0× mean the new implementation is marginally slower on that
cell.

| Function   | 500 rows | 7,640 rows | 49,422 rows (Ubuntu) |
|------------|---------:|-----------:|---------------------:|
| `varidele` |     1.2× |       1.1× |                11.6× |
| `obsedele` |     203× |       424× |                 232× |
| `condextr` |     196× |       217× |                1146× |
| `optisolu` |     188× |        77× |                 109× |
| `dataprep` |     185× |       228× |                 247× |

On Windows 11 Pro for Workstations, the same full-year pipeline gives
`obsedele` ≈ 648×, `condextr` ≈ 839×, `shorvalu` ≈ 81×, `optisolu` ≈ 25×
(at `cores = 32`), and the integrated `dataprep` call ≈ 173×. `varidele`
is around 1.17× on this cell; this is expected, since `varidele` is a
single `colMeans(is.na(.))` in both versions and the new code path has
little room for improvement.

> **Note on `optisolu` cores.** The 0.1.5 implementation could crash
> when `cores > 16`. The benchmark above used `cores = 16` for both
> versions to keep the comparison fair. 0.1.7 loads the package on each
> worker, exports the input data once per worker, and runs each
> `(interval, times)` case as a separate task, so `cores = 64` is safe.
> The practical speed-up on a many-core host is **larger** than the
> table above.

#### `melt()` — speed-up vs every one of the 7 major alternatives

Speed-ups relative to each competitor span **0.6×–2197×** across both
hosts. The sub-1.0× cells are concentrated at 1e5 rows with 10 id
columns (Windows) and at 1e7 rows × 10 id (Ubuntu), where `polars` is
faster than `dataprep`; every other cell has `dataprep` ahead of or on
par with the fastest competitor.

Means in milliseconds (Ubuntu 25.10):

| rows | val | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|----|----|----|----|----|----|----|----|----|----|
| 1e3 | 9 | 0.164 | 0.372 (2.3×) | 0.244 (1.5×) | 2.698 (16.5×) | 2.095 (12.8×) | 0.703 (4.3×) | 15.131 (92.3×) | 4.366 (26.6×) |
| 1e6 | 9 | 2.177 | 18.031 (8.3×) | 9.444 (4.3×) | 73.345 (33.7×) | 62.328 (28.6×) | 13.234 (6.1×) | 47.444 (21.8×) | 650.287 (298.7×) |
| 1e7 | 9 | 26.323 | 368.068 (14.0×) | 363.900 (13.8×) | 1079.493 (41.0×) | 709.694 (27.0×) | 157.963 (6.0×) | 491.043 (18.7×) | 6344.768 (241.0×) |
| 1e8 | 9 | 259.022 | 3522.152 (13.6×) | 3508.029 (13.5×) | 12037.801 (46.5×) | 7394.460 (28.5×) | 3088.464 (11.9×) | 4487.400 (17.3×) | 71636.937 (276.6×) |
| 1e3 | 10000 | 2.026 | 91.112 (45.0×) | 12.841 (6.3×) | 102.550 (50.6×) | 495.632 (244.7×) | 20.448 (10.1×) | 4450.699 (2197.2×) | 1917.203 (946.5×) |

Means in milliseconds (Windows 11 Pro for Workstations):

| rows | val | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|----|----|----|----|----|----|----|----|----|----|
| 1e3 | 9 | 0.306 | 0.638 (2.1×) | 0.448 (1.5×) | 4.064 (13.3×) | 3.420 (11.2×) | 0.538 (1.8×) | 29.186 (95.2×) | 7.828 (25.5×) |
| 1e6 | 9 | 12.414 | 26.406 (2.1×) | 26.623 (2.1×) | 193.073 (15.6×) | 193.283 (15.6×) | 24.984 (2.0×) | 197.627 (15.9×) | 1458.473 (117.5×) |
| 1e7 | 9 | 159.654 | 314.406 (2.0×) | 322.484 (2.0×) | 1871.675 (11.7×) | 2016.507 (12.6×) | 251.983 (1.6×) | 1761.710 (11.0×) | 14561.307 (91.2×) |
| 1e8 | 9 | 1081.994 | 2596.127 (2.4×) | 2629.491 (2.4×) | 17211.577 (15.9×) | 20196.679 (18.7×) | 4705.301 (4.3×) | 14989.606 (13.9×) | 149796.139 (138.4×) |
| 1e3 | 10000 | 12.848 | 172.965 (13.5×) | 36.763 (2.9×) | 213.030 (16.6×) | 1405.853 (109.4×) | 38.241 (3.0×) | 10372.086 (807.3×) | 5060.629 (393.9×) |

The **median** speed-up across all melt cells and all competitors is
12.0× on Ubuntu and 5.7× on Windows. The **mean** is 77.9× and 44.4×
respectively. The median is pulled down by the 1e5-row small tables; at
larger scales the speed-up is much higher.

#### `dcast()` — speed-up vs every one of the 7 major alternatives

Speed-ups relative to each competitor span **2.0×–677×** across both
hosts. Every cell has `dataprep` ahead of every other engine.

Means in milliseconds (Ubuntu 25.10):

| n_long | levels | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|----|----|----|----|----|----|----|----|----|----|
| 1e6 | 10 | 1.760 | 151.497 (86.1×) | 344.820 (196.0×) | 49.973 (28.4×) | 58.741 (33.4×) | 109.993 (62.5×) | 81.242 (46.2×) | 160.831 (91.4×) |
| 1e6 | 100 | 1.362 | 100.167 (73.5×) | 353.876 (259.8×) | 43.134 (31.7×) | 53.645 (39.4×) | 179.944 (132.1×) | 74.103 (54.4×) | 177.384 (130.2×) |
| 1e7 | 100 | 4.354 | 1840.332 (422.6×) | 631.070 (144.9×) | 623.314 (143.1×) | 789.977 (181.4×) | 496.461 (114.0×) | 986.938 (226.6×) | 1701.048 (390.6×) |
| 1e8 | 100 | 43.842 | 16612.906 (378.9×) | 17668.158 (403.0×) | 8528.194 (194.5×) | 9963.530 (227.3×) | 2452.894 (55.9×) | 12286.978 (280.3×) | 17476.822 (398.6×) |

Means in milliseconds (Windows 11 Pro for Workstations):

| n_long | levels | dataprep | reshape2 | data.table | tidyr | pandas | polars | dask | duckdb |
|----|----|----|----|----|----|----|----|----|----|
| 1e6 | 10 | 3.452 | 296.568 (85.9×) | 146.564 (42.5×) | 99.476 (28.8×) | 248.761 (72.1×) | 65.200 (18.9×) | 341.027 (98.8×) | 385.068 (111.6×) |
| 1e6 | 100 | 4.229 | 174.438 (41.2×) | 181.993 (43.0×) | 91.899 (21.7×) | 248.562 (58.8×) | 88.165 (20.8×) | 329.288 (77.9×) | 808.286 (191.1×) |
| 1e7 | 100 | 34.596 | 3130.501 (90.5×) | 1109.646 (32.1×) | 1340.192 (38.7×) | 2345.418 (67.8×) | 1111.499 (32.1×) | 3133.278 (90.6×) | 7377.747 (213.3×) |
| 1e8 | 100 | 120.830 | 24115.628 (199.6×) | 14914.904 (123.4×) | 14148.150 (117.1×) | 26487.805 (219.2×) | 8239.811 (68.2×) | 33408.904 (276.5×) | 81803.327 (677.0×) |

The **median** speed-up across all dcast cells and all competitors is
54.0× on Ubuntu and 41.6× on Windows. The **mean** is 96.5× and 74.3×
respectively. On the 1e8-row cells (8 GB of input), `dataprep` completes
in **44–209 ms** while several competitors exceed 12 s on Ubuntu and 12
s on Windows.

#### Cross-engine consistency

[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
and
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
produce output **numerically identical** to `reshape2`, `data.table`,
`tidyr`, `pandas`, `polars`, `dask`, and `duckdb` on every tested shape,
within `tol = 1e-12`.

| Operation | Cells tested | Engines | Pairwise       |
|-----------|-------------:|--------:|----------------|
| `melt`    |     4 shapes |       8 | all consistent |
| `dcast`   |     4 shapes |       8 | all consistent |

Full tables — including `mean`, `median`, and the full per-competitor
gradient — are in
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md).
The reproducible runner is shipped under `inst/`.

### Behaviour changes

#### `obsedele()` semantics

The 0.1.7 C++ backend (`obsedele_cpp`) implements the retention
criterion with an **anchor-based scan**: for each missing value and each
selected column, the time distance to the nearest non-missing anchor on
the left and on the right is computed directly. A row is deleted when
**any** selected column has **both** distances exceeding `half` minutes.

This is mathematically identical to the running-mean criterion used in
0.1.0 and the `rleid`-based criterion used in 0.1.5, but:

- **Scans each column independently.** The 0.1.5 implementation merged
  columns before computing runs, which changed run boundaries and could
  both over-delete and under-delete boundary rows.
- **Runs in O(n) time with O(1) extra allocation per column.** No grid
  materialisation, no run-length state.
- **Parallelises over columns with OpenMP** without any shared mutable
  state.

#### `optisolu()` multi-core safety

The 0.1.5
[`parallel::makeCluster()`](https://rdrr.io/r/parallel/makeCluster.html)
path gave each worker a full copy of the input. With `cores > 16` and
large data this exhausted memory and aborted the R session. The 0.1.7
implementation shares read-only data across workers and accepts up to 64
cores safely.

#### `melt()` new `major` and `as.factor` arguments

The `major` argument is now honoured strictly. `NULL` (default) is
equivalent to `"col"`: column-major, identical to
[`reshape2::melt`](https://rdrr.io/pkg/reshape2/man/melt.html).
`major = "row"` produces tidyr-compatible row ordering. The earlier
implementation could switch automatically based on input shape, and the
tiny fast path silently ignored `major`; both are fixed. There is no
longer any automatic switching.

A new `as.factor` argument controls the type of the `variable` column.
`NULL` (default) uses `TRUE` for `major = "col"` and `FALSE` for
`major = "row"`. Explicit `TRUE` / `FALSE` overrides that default. The
return value is always a plain `data.frame`; no `tibble` attributes are
attached.

#### `prep_fit()` / `prep_transform()` degenerate columns

A constant training column has `sd = 0`, `IQR = 0`, or `max - min = 0`.
[`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
now stores `1` as the scale value for such columns, so the transform
becomes `x - center`.
[`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md)
additionally guards against `scale_val == 0` in case a plan is edited by
hand.

### Bug fixes

- **[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md):
  `na.rm = TRUE` combined with an explicit non-`NA` `fill` no longer
  loses the `fill` value.** On the block-path (canonical melt output),
  the tile transpose wrote the input values unconditionally after the
  fill pass, so cells whose input was `NA`/`NaN` ended up as `NA`
  instead of the requested `fill`. The transpose kernels now receive
  `na_rm` and the resolved `fill_val` and substitute them while the tile
  is built. The general path was not affected; both paths now produce
  identical output. Repro:
  `dcast(data.frame(id=c(1,1,2,2), variable=c("x","y","x","y"), value=c(1,NA,3,4)), id="id", variable="variable", value="value", na.rm=TRUE, fill=-1)`
  now returns `(1,"y") = -1`.

- **[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md):
  duplicate `(id, variable)` pairs now resolve consistently with the
  documented “last occurrence wins” rule.** The block path previously
  kept the *first* occurrence of a duplicated block; the general path
  kept the *last*. The block path now keeps the last occurrence,
  matching
  [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)’s
  documentation and
  [`reshape2::dcast()`](https://rdrr.io/pkg/reshape2/man/cast.html).
  This only affects non-canonical inputs (canonical
  [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
  output has no duplicates); canonical round-trips are unchanged.

### New functions

#### Cleaning

- [`balance_panel()`](https://chunshengliang.github.io/dataprep/reference/balance_panel.html)
  — balance an unbalanced panel by filling or completing.
- [`bin_data()`](https://chunshengliang.github.io/dataprep/reference/bin_data.html)
  — discretize continuous variables.
- [`clean_strings()`](https://chunshengliang.github.io/dataprep/reference/clean_strings.html)
  — trim / case / regex cleaning of character columns.
- [`deduplicate()`](https://chunshengliang.github.io/dataprep/reference/deduplicate.html)
  — exact and fuzzy duplicate removal.
- [`encode_categorical()`](https://chunshengliang.github.io/dataprep/reference/encode_categorical.html)
  — label / frequency / one-hot encoding.
- [`filter_high_cor()`](https://chunshengliang.github.io/dataprep/reference/filter_high_cor.html)
  — drop highly correlated variables.
- [`filter_low_var()`](https://chunshengliang.github.io/dataprep/reference/filter_low_var.html)
  — drop near-constant variables.
- [`phys_filter()`](https://chunshengliang.github.io/dataprep/reference/phys_filter.html)
  — physical range filtering.
- [`validate_data()`](https://chunshengliang.github.io/dataprep/reference/validate_data.html)
  — rule-based data validation.
- [`winsorize()`](https://chunshengliang.github.io/dataprep/reference/winsorize.html)
  — cap extreme values.
- [`zerona()`](https://chunshengliang.github.io/dataprep/reference/zerona.html)
  — replace zeros with NA.

#### Imputation and transformation

- [`impute_missing()`](https://chunshengliang.github.io/dataprep/reference/impute_missing.html)
  — linear / LOCF / NOCB / mean / median.
- [`log_returns()`](https://chunshengliang.github.io/dataprep/reference/log_returns.html)
  — log returns.
- [`transform_data()`](https://chunshengliang.github.io/dataprep/reference/transform_data.html)
  — log / sqrt / Box-Cox / Yeo-Johnson and z-score / min-max / robust
  scaling.

#### Diagnostics and reporting

- [`na_diagnose()`](https://chunshengliang.github.io/dataprep/reference/na_diagnose.html)
  — missing-value run statistics.
- [`data_report()`](https://chunshengliang.github.io/dataprep/reference/data_report.html)
  — compact data quality report.
- [`dry_run()`](https://chunshengliang.github.io/dataprep/reference/dry_run.html)
  — simulate preprocessing without changing data.

#### Time series

- [`create_lags()`](https://chunshengliang.github.io/dataprep/reference/create_lags.html)
  — grouped lag / lead columns.
- [`day_night_flag()`](https://chunshengliang.github.io/dataprep/reference/day_night_flag.html)
  — day / night indicator.
- [`season_flag()`](https://chunshengliang.github.io/dataprep/reference/season_flag.html)
  — season / month / quarter indicator.
- [`decompose_ts()`](https://chunshengliang.github.io/dataprep/reference/decompose_ts.html)
  — additive / multiplicative decomposition.
- [`detrend_ts()`](https://chunshengliang.github.io/dataprep/reference/detrend_ts.html)
  — linear detrending.
- [`remove_diurnal_cycle()`](https://chunshengliang.github.io/dataprep/reference/remove_diurnal_cycle.html)
  — subtract mean diurnal cycle.
- [`resample_time()`](https://chunshengliang.github.io/dataprep/reference/resample_time.html)
  — resample to coarser period.
- [`roll_apply()`](https://chunshengliang.github.io/dataprep/reference/roll_apply.html)
  — rolling statistics with alignment.
- [`drift_detect()`](https://chunshengliang.github.io/dataprep/reference/drift_detect.html)
  — rolling drift detection.

#### Sampling and workflow

- [`sample_data()`](https://chunshengliang.github.io/dataprep/reference/sample_data.html)
  — simple and stratified sampling.
- [`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.html)
  /
  [`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.html)
  — fit / transform style preprocessing plan that prevents data leakage.

#### Reshaping

- [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.html)
  — long-to-wide reshaping, paired with
  [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.html).

### API changes

- Argument `cols` now consistently accepts names, integer indices, or
  logical masks across the package.
- All exported functions gain a `verbose = FALSE` argument that controls
  progress and timing messages.
- Functions that operate on a time column accept `date_col = NULL`; when
  `NULL`, the first column matching `date`, `Date`, or `DATE` is used.
- [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.html)
  gains `id.vars`, `measure.vars`, `variable.name`, `value.name`,
  `na.rm`, `cores`, `major`, `as.factor`, `verbose`,
  `parallel_threshold`. `id.vars` is an alias of `id` for reshape2 /
  data.table compatibility.
- [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.html)
  ships a `formula` interface (`id1 + id2 ~ variable`), `value.var` as
  an alias of `value`, and `fun.aggregate` for reducing duplicate
  `(id, variable)` pairs, plus `fill`, `na.rm`, `cores`, and `verbose`.
- [`dataprep()`](https://chunshengliang.github.io/dataprep/reference/dataprep.html),
  [`shorvalu()`](https://chunshengliang.github.io/dataprep/reference/shorvalu.html),
  [`descdata()`](https://chunshengliang.github.io/dataprep/reference/descdata.html),
  [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.html),
  [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.html),
  [`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.html),
  and
  [`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.html)
  gain a `cores` argument for OpenMP control (the cleaning functions
  [`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.html),
  [`condextr()`](https://chunshengliang.github.io/dataprep/reference/condextr.html),
  [`percoutl()`](https://chunshengliang.github.io/dataprep/reference/percoutl.html),
  and
  [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.html)
  already accepted `cores` since 0.1.5; their backends now route it to
  OpenMP as well). The global option `options(dataprep.cores = ...)` is
  respected by
  [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.html)
  and
  [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.html).
- [`descdata()`](https://chunshengliang.github.io/dataprep/reference/descdata.html)
  now accepts `stats` as either numeric indices or character names.
- [`percplot()`](https://chunshengliang.github.io/dataprep/reference/percplot.html)
  now prints both the sample size (`n`) and the number of missing values
  (`na`) in each facet when a grouping column is supplied.
- [`descplot()`](https://chunshengliang.github.io/dataprep/reference/descplot.html)
  and
  [`percplot()`](https://chunshengliang.github.io/dataprep/reference/percplot.html)
  gain a `num_xaxis` argument that overrides the automatic choice
  between log and linear x-axis scales when column names are numeric.

### Documentation

Seven vignettes ship with the package:

- [`vignette("dataprep-philosophy")`](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.md)
  — design philosophy and preprocessing methodology.
- [`vignette("dataprep-cleaning")`](https://chunshengliang.github.io/dataprep/articles/dataprep-cleaning.md)
  — step-by-step walkthrough of the four cleaning steps.
- [`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md)
  — full benchmark tables and 8-engine consistency checks.
- [`vignette("dataprep-migration")`](https://chunshengliang.github.io/dataprep/articles/dataprep-migration.md)
  — 0.1.5 → 0.1.7 upgrade guide.
- [`vignette("dataprep-workflow")`](https://chunshengliang.github.io/dataprep/articles/dataprep-workflow.md)
  — leakage-free preprocessing with
  [`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
  /
  [`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md).
- [`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md)
  — fast reshaping usage and implementation notes.
- [`vignette("dataprep-plots")`](https://chunshengliang.github.io/dataprep/articles/dataprep-plots.md)
  — descriptive statistics and diagnostic plots.

### Performance notes

Three benchmark cells sit close to, or marginally behind, the fastest
competitor —
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
at 100 id columns,
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
at 1e7 rows × 10 id columns, and
[`varidele()`](https://chunshengliang.github.io/dataprep/reference/varidele.md)
on full-year data — and all three share the same root cause: the
affected internal buffers (the 96-bit fingerprint table, the
per-id-column id blocks, and the `is.na` scratch matrix) are allocated
with `Rf_allocVector`, which routes through R’s default `malloc`-based
allocator and therefore cannot be directed to hugepages, a per-process
free pool, or a NUMA-aware arena, so first-touch page faults dominate
those cells. Two possible fixes were prototyped on the reshaping
backends and both were measured to close the gap: routing those buffers
through `Rf_allocVector3` with a hypothetical custom `R_allocator_t`
adds a further few-fold speed-up on top of the existing hundred-fold to
thousand-fold margins, and returning **ALTREP** virtual objects from
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
/
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
— a lazy `variable` column and a deferred id block, so that large parts
of the output are never materialised — adds another order of magnitude
(tens of times) on the wide-table shapes. Neither is used in this
release. `Rf_allocVector3` is not recommended by CRAN, and an ALTREP
return value, while fast to produce, is slow for downstream complex
statistics because every element access re-enters the virtual-object
layer, which shifts the cost from `dataprep` to the caller’s analysis
code. The shipped 0.1.7 backends therefore keep the standard allocation
path, and the reported speed-ups stand as measured:
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
spans 0.6×–2197× and
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
spans 2.0×–677× across the two reference hosts, with the sub-1.0×
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
cells confined to the 1e5-row shapes where per-call overhead dominates.
On the cleaning pipeline the 0.1.5 → 0.1.7 speed-ups of 1.1×–1146× stand
as reported; `Rf_allocVector3` and ALTREP were not evaluated there.

### Environment variables (optional)

- `DATAPREP_RUN_BENCHMARK` = `1` enables the shipped benchmark scripts.
  They are disabled by default so that `R CMD check` does not execute
  them.

### See also

- [Design
  philosophy](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.html)
  — why the pipeline has the shape it does.
- [Cleaning
  pipeline](https://chunshengliang.github.io/dataprep/articles/dataprep-cleaning.html)
  — full walkthrough of `varidele` / `obsedele` / `condextr` /
  `shorvalu`.
- [Performance](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.html)
  — benchmark tables and cross-engine consistency.
- [Migration](https://chunshengliang.github.io/dataprep/articles/dataprep-migration.html)
  — upgrade guide.
- [Leakage-free
  workflow](https://chunshengliang.github.io/dataprep/articles/dataprep-workflow.html)
  — `prep_fit` / `prep_transform`.
- [Reshaping](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.html)
  — melt / dcast.
- [Descriptive statistics and
  plots](https://chunshengliang.github.io/dataprep/articles/dataprep-plots.html)
  — `descplot` / `percplot`.

## dataprep 0.1.5

- Initial public release on CRAN.
- Core cleaning pipeline: `varidele`, `obsedele`, `condextr`,
  `percoutl`, `optisolu`, `shorvalu`, `dataprep`.
- Descriptive statistics and percentile helpers: `descdata`, `descplot`,
  `percdata`, `percplot`.
- Example datasets: `data`, `data1`.
