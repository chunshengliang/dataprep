# Changelog

## dataprep 0.1.8.1

### Reshaping backends

- [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
  and
  [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
  receive further C++ / SIMD / OpenMP optimisations.
- Benchmark harness updated to v15:
  - warmup excluded;
  - adaptive iterations;
  - sentinel vector;
  - mixed-type id columns by default;
  - median is the primary metric;
  - `collapse` added as the 9th engine;
  - aggregated dcast consistency and benchmark added.

### Documentation

- `README.md`,
  [`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md),
  [`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md),
  `man/melt.Rd`, and `man/dcast.Rd` updated to 0.1.8.1.

## dataprep 0.1.8.1

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
    deleted. The 0.1.8.1 implementation scans each column independently:
    a row is deleted when *any* selected column has a missing run longer
    than `half` minutes on both sides.

2.  **`half` is now always in minutes, and the boundary is inclusive.**
    In 0.1.5, `half` counted grid rows in units of `by`: with
    `by = "5 min", half = 30` the effective window was 150 minutes. In
    0.1.8.1, `half` is always in minutes, independent of `by`. Rows
    whose nearest anchor is exactly `half` minutes away are retained
    (`within half minutes` is a `<=` condition).

3.  **[`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
    no longer crashes with `cores > 16`.** The 0.1.5
    [`parallel::makeCluster()`](https://rdrr.io/r/parallel/makeCluster.html)
    path exhausted memory when the worker processes each received a full
    copy of the input. The 0.1.8.1 implementation loads the package on
    each worker, exports the input data only once per worker, and runs
    each `(interval, times)` case in a separate task, so `cores = 64`
    and `cores = NULL` (automatic) are both safe.

#### Quantified effect on a full-year dataset

On SMEAR I Varrio 2025 (49,422 rows × 61 numeric channels, 10-minute
sampling), running the same pipeline with the same parameters:

| Stage            |              0.1.5 |            0.1.8.1 |      Δ |
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

#### Cleaning pipeline (dataprep 0.1.5 → 0.1.8.1)

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
> versions to keep the comparison fair. 0.1.8.1 loads the package on
> each worker, exports the input data once per worker, and runs each
> `(interval, times)` case as a separate task, so `cores = 64` is safe.
> The practical speed-up on a many-core host is **larger** than the
> table above.

#### `melt()` — speed-up vs every one of the 8 major alternatives

Speed-ups relative to each competitor span **0.3×–1750.0×** across both
hosts. The sub-1.0× cells are concentrated on the `1 id + 9 val` and
`10 id + 9 val` shapes at 1e5–1e7 rows, where `polars` is faster than
`dataprep`.

Medians in milliseconds (Ubuntu 25.10):

| rows | val | dataprep | reshape2 | data.table | tidyr | collapse | pandas | polars | dask | duckdb |
|----|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1e3 | 9 | 0.152 | 0.410 (2.7×) | 0.260 (1.7×) | 2.805 (18.4×) | 0.175 (1.1×) | 2.195 (14.4×) | 0.594 (3.9×) | 15.1 (99.0×) | 5.435 (35.7×) |
| 1e6 | 9 | 1.441 | 8.145 (5.7×) | 7.937 (5.5×) | 65.8 (45.7×) | 6.542 (4.5×) | 60.7 (42.2×) | 10.6 (7.4×) | 46.1 (32.0×) | 758.1 (526.2×) |
| 1e7 | 9 | 38.0 | 368.8 (9.7×) | 368.8 (9.7×) | 1094 (28.7×) | 317.2 (8.3×) | 718.5 (18.9×) | 93.0 (2.4×) | 507.2 (13.3×) | 7418 (195.0×) |
| 1e8 | 9 | 398.6 | 3592 (9.0×) | 3584 (9.0×) | 11788 (29.6×) | 3632 (9.1×) | 7388 (18.5×) | 2597 (6.5×) | 4550 (11.4×) | 87982 (220.7×) |
| 1e3 | 10000 | 2.176 | 78.3 (36.0×) | 9.710 (4.5×) | 93.5 (43.0×) | 9.285 (4.3×) | 501.2 (230.3×) | 16.3 (7.5×) | 3809 (1750.0×) | 2048 (940.9×) |

Medians in milliseconds (Windows 11 Pro for Workstations):

| rows | val | dataprep | reshape2 | data.table | tidyr | collapse | pandas | polars | dask | duckdb |
|----|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1e3 | 9 | 0.300 | 0.693 (2.3×) | 0.516 (1.7×) | 4.015 (13.4×) | 0.313 (1.0×) | 3.570 (11.9×) | 0.597 (2.0×) | 29.3 (97.7×) | 7.877 (26.3×) |
| 1e6 | 9 | 18.0 | 23.9 (1.3×) | 23.6 (1.3×) | 157.4 (8.7×) | 23.2 (1.3×) | 182.8 (10.2×) | 17.8 (1.0×) | 185.9 (10.3×) | 1546 (85.9×) |
| 1e7 | 9 | 152.7 | 236.9 (1.6×) | 238.0 (1.6×) | 1501 (9.8×) | 235.1 (1.5×) | 1885 (12.3×) | 162.5 (1.1×) | 1515 (9.9×) | 14435 (94.5×) |
| 1e8 | 9 | 1773 | 3295 (1.9×) | 2419 (1.4×) | 20809 (11.7×) | 2516 (1.4×) | 22699 (12.8×) | 5510 (3.1×) | 15429 (8.7×) | 161372 (91.0×) |
| 1e3 | 10000 | 31.7 | 168.6 (5.3×) | 32.7 (1.0×) | 201.7 (6.4×) | 34.7 (1.1×) | 1412 (44.6×) | 30.0 (0.9×) | 10253 (323.6×) | 5112 (161.4×) |

The **median** speed-up across all melt cells and all competitors is
9.6× on Ubuntu and 3.7× on Windows. The **mean** is 71.0× and 31.8×
respectively.

#### `dcast()` — speed-up vs every one of the 8 major alternatives

Speed-ups relative to each competitor span **0.9×–1849.5×** across both
hosts. Every cell has `dataprep` ahead of every other engine, with the
single exception of the smallest Windows case (`n_long = 1,000`, 1 id,
10 levels), where `collapse` is about 8 % faster.

Medians in milliseconds (Ubuntu 25.10):

| n_long | levels | dataprep | reshape2 | data.table | tidyr | collapse | pandas | polars | dask | duckdb |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1e6 | 10 | 1.075 | 139.2 (129.5×) | 323.7 (301.0×) | 46.5 (43.3×) | 4.917 (4.6×) | 56.4 (52.5×) | 94.2 (87.6×) | 81.7 (76.0×) | 242.0 (225.0×) |
| 1e6 | 100 | 0.585 | 102.8 (175.6×) | 346.7 (592.4×) | 44.2 (75.5×) | 5.021 (8.6×) | 54.7 (93.5×) | 176.1 (300.9×) | 76.2 (130.3×) | 258.8 (442.2×) |
| 1e7 | 100 | 3.845 | 2016 (524.4×) | 595.9 (155.0×) | 684.3 (178.0×) | 52.7 (13.7×) | 742.5 (193.1×) | 474.5 (123.4×) | 941.3 (244.8×) | 2505 (651.5×) |
| 1e8 | 100 | 37.9 | 16777 (443.0×) | 18782 (495.9×) | 8285 (218.8×) | 499.3 (13.2×) | 9587 (253.1×) | 2509 (66.3×) | 12155 (320.9×) | 24486 (646.5×) |

Medians in milliseconds (Windows 11 Pro for Workstations):

| n_long | levels | dataprep | reshape2 | data.table | tidyr | collapse | pandas | polars | dask | duckdb |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1e6 | 10 | 2.977 | 239.6 (80.5×) | 154.7 (52.0×) | 112.8 (37.9×) | 14.2 (4.8×) | 239.5 (80.5×) | 53.0 (17.8×) | 359.4 (120.8×) | 385.6 (129.6×) |
| 1e6 | 100 | 3.999 | 173.3 (43.3×) | 179.5 (44.9×) | 82.1 (20.5×) | 14.2 (3.6×) | 226.4 (56.6×) | 71.8 (18.0×) | 308.5 (77.1×) | 800.0 (200.0×) |
| 1e7 | 100 | 32.7 | 2855 (87.3×) | 1119 (34.2×) | 1220 (37.3×) | 156.7 (4.8×) | 2259 (69.1×) | 1217 (37.2×) | 2967 (90.7×) | 7389 (226.0×) |
| 1e8 | 100 | 105.9 | 22379 (211.3×) | 14723 (139.0×) | 12879 (121.6×) | 1477 (13.9×) | 25000 (236.0×) | 8290 (78.3×) | 31519 (297.5×) | 82310 (777.0×) |

The **median** speed-up across all dcast cells and all competitors is
74.5× on Ubuntu and 42.2× on Windows. The **mean** is 128.9× and 91.1×
respectively.

#### Aggregated `dcast()` — speed-up vs every one of the 8 major alternatives

Speed-ups relative to each competitor span **2.0×–872.4×** across both
hosts. Five aggregation functions (`sum`, `mean`, `min`, `max`, `count`)
and five scales (1e4 to 1e8 rows) are tested.

The **median** speed-up across all aggregated dcast cells and all
competitors is 27.4× on Ubuntu and 17.9× on Windows. The **mean** is
68.6× and 61.8× respectively.

#### Cross-engine consistency

[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md),
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md),
and aggregated
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
produce output **numerically identical** to `reshape2`, `data.table`,
`tidyr`, `collapse`, `pandas`, `polars`, `dask`, and `duckdb` on every
tested shape.
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
and
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
are pure data movement, so their output is **bit-identical**; aggregated
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
matches to `tol = 1e-10` because `sum` and `mean` accumulate in
different orders across engines.

| Operation    |      Cells tested | Engines | Pairwise       |
|--------------|------------------:|--------:|----------------|
| `melt`       |          4 shapes |       9 | all consistent |
| `dcast`      |          4 shapes |       9 | all consistent |
| `dcast(agg)` | 2 shapes × 5 aggs |       9 | all consistent |

### Behaviour changes

#### `obsedele()` semantics

The 0.1.8.1 C++ backend (`obsedele_cpp`) implements the retention
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
large data this exhausted memory and aborted the R session. The 0.1.8.1
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
  — full benchmark tables and 9-engine consistency checks.
- [`vignette("dataprep-migration")`](https://chunshengliang.github.io/dataprep/articles/dataprep-migration.md)
  — 0.1.5 → 0.1.8.1 upgrade guide.
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
code.

The shipped 0.1.8.1 backends therefore keep the standard allocation
path, and the reported speed-ups stand as measured:
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
spans 0.3×–1750.0×,
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
spans 0.9×–1849.5×, and aggregated
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
spans 2.0×–872.4× across the two reference hosts, with the sub-1.0×
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
cells confined to the 1e7-row (Ubuntu) and 1e5-row (Windows) shapes with
10 id columns, where `polars` is faster than `dataprep`. On the cleaning
pipeline the 0.1.5 → 0.1.8.1 speed-ups of 1.1×–1146× stand as reported;
`Rf_allocVector3` and ALTREP were not evaluated there.

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
