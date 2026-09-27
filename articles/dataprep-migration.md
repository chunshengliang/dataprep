# dataprep: upgrading from 0.1.5 to 0.1.7

``` r
library(dataprep)
```

## What changed

Three behaviour changes affect row and column counts. All three are bug
fixes, but each one changes the output on real data. If your downstream
analysis depends on exact row counts, read the quantified comparison in
the last section before upgrading.

## Interface changes

Besides the behaviour changes above, several functions changed their
argument interface between 0.1.5 and 0.1.7. The old `start` / `end` pair
was replaced by a single `cols` argument (character names, integer
indices, or logical mask):

| Function   | 0.1.5 arguments     | 0.1.7 arguments       |
|------------|---------------------|-----------------------|
| `varidele` | `start`, `end`      | `cols`                |
| `obsedele` | `start`, `end`      | `cols`                |
| `condextr` | `start`, `end`      | `cols`                |
| `percoutl` | `start`, `end`      | `cols`                |
| `optisolu` | `start`, `end`      | `cols`                |
| `dataprep` | `start`, `end`      | `cols`                |
| `descdata` | `start`, `end`      | `cols`                |
| `descplot` | `start`, `end`      | `cols`                |
| `percdata` | `start`, `end`      | `cols`                |
| `percplot` | `start`, `end`      | `cols`                |
| `shorvalu` | `start`, `end`      | `cols`                |
| `melt`     | `cols` (id columns) | `id` / `measure.vars` |

A call such as `varidele(data, 3, 15)` was interpreted as
`start = 3, end = 15` in 0.1.5, but in 0.1.7 it is parsed as `cols = 3`
(the `15` is dropped or becomes `fraction`). You must rewrite it as
`varidele(data, cols = 3:15)`. The same applies to every function in the
table.

- [`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.md)
  **now scans each column independently.** The 0.1.5 implementation
  collapsed all selected columns into one long vector before computing
  missing runs; this changed `NA` run boundaries and could both
  over-delete boundary rows and retain rows that should have been
  deleted. The 0.1.7 implementation scans each column independently: a
  row is deleted when *any* selected column has a missing run longer
  than `half` minutes on both sides.

- The **`half`-minute boundary is now inclusive.** Rows whose nearest
  anchor is exactly `half` minutes away are retained (“within `half`
  minutes” is a `<=` condition).

- [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
  **no longer crashes with `cores > 16`.** The 0.1.5
  [`parallel::makeCluster()`](https://rdrr.io/r/parallel/makeCluster.html)
  path exhausted memory when the worker processes each received a full
  copy of the input. The 0.1.7 implementation loads the package on each
  worker, exports the input data only once per worker, and runs each
  `(interval, times)` case in a separate task, so `cores = 64` and
  `cores = NULL` (automatic) are both safe. Note that the optimal
  parameter values returned by
  [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
  may differ slightly between 0.1.5 and 0.1.7 because the underlying
  outlier-marking and observation-deletion backends have changed. Re-run
  [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
  after upgrading if exact parameter values matter.

The full description is in `news(package = "dataprep")`. The design
reasoning behind the pipeline is in
[`vignette("dataprep-philosophy")`](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.md).

## Minimal reproduction of the boundary change

Consider five observations sampled every 10 minutes, with a valid value
only at the two ends:

``` r
df <- data.frame(
  date = as.POSIXct("2024-01-01 00:00:00", tz = "UTC") + 0:4 * 600,
  x    = c(1, NA, NA, NA, 5)
)

obsedele(df, cols = "x", half = 30)
#>                  date  x
#> 1 2024-01-01 00:00:00  1
#> 2 2024-01-01 00:10:00 NA
#> 3 2024-01-01 00:20:00 NA
#> 4 2024-01-01 00:30:00 NA
#> 5 2024-01-01 00:40:00  5
```

The three interior rows are each 10, 20, or 30 minutes from the nearest
valid anchor. Under 0.1.5 the third row was deleted because the
comparison was strict (`dl > half`); under 0.1.7 it is retained because
`dl == half` satisfies “within `half` minutes”.

## Minimal reproduction of the multi-column change

Consider two channels with mutually exclusive missing runs:

``` r
df <- data.frame(
  date = as.POSIXct("2024-01-01 00:00:00", tz = "UTC") + 0:9 * 600,
  x    = c(1, NA, NA, NA, NA, NA, NA, NA, NA, 5),
  y    = c(NA, NA, NA, NA, 2, NA, NA, NA, NA, NA)
)
nrow(obsedele(df, cols = c("x", "y"), half = 60))
#> [1] 10
```

Under 0.1.5, the `x` and `y` `NA` runs were merged before computing the
run length. This changed the run boundaries and could either over-delete
rows or retain rows that should have been deleted. Under 0.1.7 the two
channels are checked independently: a row is deleted when *any* selected
column has a run longer than `half` minutes on both sides.

## Implementation evolution

The retention criterion has been stable across releases, but the
implementation has improved steadily. The table below summarises the
three generations:

| Release | Strategy | Complexity |
|----|----|----|
| 0.1.0 | Borrowed running mean: expand the series onto a regular grid with [`tidyr::complete()`](https://tidyr.tidyverse.org/reference/complete.html), compute a 59-minute centred moving average on a temporary column, and use its emptiness pattern to flag long runs. | `O(grid length)` per subset |
| 0.1.5 | Run-length encoding: use [`data.table::rleid()`](https://rdrr.io/pkg/data.table/man/rleid.html) and `rowid()` to collapse consecutive `NA`s into runs, then compare each run length to the number of grid points covered by `half` minutes. | `O(n)` time, `O(n)` temporary storage |
| 0.1.7 | Anchor scan: for each missing value, look up the nearest non-missing anchor on each side and compare the two time distances directly. No grid, no run-length state. | `O(n)` time, `O(1)` extra allocation per column |

Each generation produces the same deletion decision on the same input,
but the constant factors shrink. The 0.1.7 anchor scan is the first
version that is fast enough to run interactively on full-year data: on
SMEAR I Varrio 2025 (49,422 rows × 61 numeric channels),
[`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.md)
now runs in 0.05 s against 11.6 s in 0.1.5 on Ubuntu 25.10 (about 232×),
and in 0.035 s against 22.5 s on Windows 11 Pro for Workstations (about
648×). See
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md)
for the full benchmark.

## Quantified effect on a full-year dataset

On SMEAR I Varrio 2025 (49,422 rows × 61 numeric channels, 10-minute
sampling), running the same pipeline with the same parameters:

| Stage            |              0.1.5 |              0.1.7 |   Δ |
|------------------|-------------------:|-------------------:|----:|
| `varidele`       | 25 columns deleted | 25 columns deleted |   0 |
| `obsedele`       | 1,494 rows deleted | 1,496 rows deleted |  +2 |
| `condextr`       | 1,868 rows deleted | 1,863 rows deleted |  −5 |
| `shorvalu`       |  50,376 NAs filled |  50,387 NAs filled | +11 |
| `dataprep` final |        46,060 rows |        46,063 rows |  +3 |

Net change: 0.006% of the input. The six rows that differ between
versions all sit at run boundaries where the anchor distance is within
one sampling interval of `half` minutes.

## Where the difference comes from

The six differing rows fall into two groups:

- **Rows kept by 0.1.7, deleted by 0.1.5 (3 rows).** These are rows
  whose nearest anchor is exactly `half` minutes away. Under the 0.1.5
  strict comparison (`dl > half`) they were deleted; under the 0.1.7
  inclusive comparison (`dl >= half`) they are retained. Each of these
  rows has a valid anchor within one sampling interval of `half`, so
  retaining them is consistent with the physical constraint described in
  [`vignette("dataprep-philosophy")`](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.md).

- **Rows deleted by 0.1.7, kept by 0.1.5 (3 rows, overlapping with the
  above).** These are rows that pass the check on some columns but fail
  on at least one. Under 0.1.5 the merge-columns approach effectively
  widened the anchor window on these rows; under 0.1.7 each column is
  checked independently, so the row is deleted. These rows would have
  been interpolated across a gap longer than `half` minutes in at least
  one channel, which contradicts the design.

The net result is 3 additional rows in the final output.

## Migration checklist

Re-run any downstream analysis that uses the output of
[`obsedele()`](https://chunshengliang.github.io/dataprep/reference/obsedele.md),
[`condextr()`](https://chunshengliang.github.io/dataprep/reference/condextr.md),
[`percoutl()`](https://chunshengliang.github.io/dataprep/reference/percoutl.md),
or
[`dataprep()`](https://chunshengliang.github.io/dataprep/reference/dataprep.md).
The three-row difference on SMEAR I Varrio 2025 is representative of the
magnitude you should expect on other datasets (well under 0.1% of rows).

If you use
[`prep_fit()`](https://chunshengliang.github.io/dataprep/reference/prep_fit.md)
/
[`prep_transform()`](https://chunshengliang.github.io/dataprep/reference/prep_transform.md),
re-fit the plan on the new training data. The stored thresholds are not
affected by the change, but the row indices that fed into them are.

If you were limiting
[`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
to `cores <= 16` as a workaround for the 0.1.5 crash, remove the cap.
0.1.7 accepts up to 64. Also re-run
[`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
because the returned optimal parameters may differ slightly from 0.1.5.

If downstream behaviour depends on specific boundary rows, verify the
difference with
[`dplyr::anti_join()`](https://dplyr.tidyverse.org/reference/filter-joins.html)
between the 0.1.5 and 0.1.7 outputs. The example below shows how.

``` r
result_015 <- dataprep::dataprep(data, cols = 5:65, group = 4)
result_017 <- dataprep(data, cols = 5:65, group = 4)

kept_only_by_017 <- dplyr::anti_join(result_017, result_015, by = "date")
kept_only_by_015 <- dplyr::anti_join(result_015, result_017, by = "date")
```

## What did not change

For users who only call
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
and
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md),
or who only use `dataprep` for descriptive statistics (`descdata`,
`na_diagnose`, `percdata`, `percplot`, `descplot`), there is no
behaviour change. Those functions have been re-implemented in 0.1.7 for
speed (`melt` and `dcast`) or reorganised internally (`data_report`,
`dry_run`), but the output on every tested input is byte-identical to
0.1.5 except where noted in the news file.

## Performance reference

The 0.1.7 release rewrites every heavy cleaning routine in C++. The
table below compares against 0.1.5 on three dataset sizes from the same
source (SMEAR I Varrio forest). All numbers are speed-up ratios (0.1.5
time / 0.1.7 time); a value below 1.0× means 0.1.7 is slightly slower on
that cell.

| Function   | 500 rows | 7,640 rows | 49,422 rows (Ubuntu 25.10) |
|------------|---------:|-----------:|---------------------------:|
| `varidele` |     1.1× |       1.1× |                      11.6× |
| `obsedele` |     203× |       424× |                       232× |
| `condextr` |     196× |       217× |                      1146× |
| `optisolu` |     188× |        77× |                       109× |
| `dataprep` |     185× |       228× |                       247× |

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
> The practical speed-up on a many-core host is larger than the table
> above. Also note that the optimal parameter values returned by
> [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
> may differ slightly between versions; re-run
> [`optisolu()`](https://chunshengliang.github.io/dataprep/reference/optisolu.md)
> after upgrading if exact values matter.

## Test environments

Benchmarks and checks were run on two reference hosts. Only the core
configuration is listed here; full details are in
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md).

- **Ubuntu 25.10** — R 4.5.1, g++ 15.2.0; 2× AMD EPYC 9965 192-Core (384
  physical / 768 logical cores), 1.0 TiB DDR5, full AVX-512.

- **Windows 11 Pro for Workstations** — R 4.6.1 (ucrt), GCC 14.3.0; 2×
  AMD EPYC 7B12 64-Core (128 physical / 128 logical cores), about 224
  GiB RAM.

## Where to go next

- **Design philosophy** —
  [`vignette("dataprep-philosophy")`](https://chunshengliang.github.io/dataprep/articles/dataprep-philosophy.md).
  Why the pipeline has the shape it does.

- **Cleaning pipeline walkthrough** —
  [`vignette("dataprep-cleaning")`](https://chunshengliang.github.io/dataprep/articles/dataprep-cleaning.md).
  Step-by-step execution on a real dataset.

- **Performance and cross-engine consistency** —
  [`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md).
  Full benchmark tables and 8-engine consistency checks.

- **Fast reshaping** —
  [`vignette("dataprep-melt-dcast")`](https://chunshengliang.github.io/dataprep/articles/dataprep-melt-dcast.md).

## Session info

``` r
sessionInfo()
#> R version 4.5.1 (2025-06-13)
#> Platform: x86_64-pc-linux-gnu
#> Running under: Ubuntu 25.10
#> 
#> Matrix products: default
#> BLAS:   /usr/lib/x86_64-linux-gnu/openblas-openmp/libblas.so.3 
#> LAPACK: /usr/lib/x86_64-linux-gnu/openblas-openmp/libopenblasp-r0.3.30.so;  LAPACK version 3.12.0
#> 
#> locale:
#>  [1] LC_CTYPE=zh_CN.UTF-8       LC_NUMERIC=C              
#>  [3] LC_TIME=zh_CN.UTF-8        LC_COLLATE=zh_CN.UTF-8    
#>  [5] LC_MONETARY=zh_CN.UTF-8    LC_MESSAGES=zh_CN.UTF-8   
#>  [7] LC_PAPER=zh_CN.UTF-8       LC_NAME=C                 
#>  [9] LC_ADDRESS=C               LC_TELEPHONE=C            
#> [11] LC_MEASUREMENT=zh_CN.UTF-8 LC_IDENTIFICATION=C       
#> 
#> time zone: Asia/Shanghai
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
#>  [5] xfun_0.61         otel_0.2.0        generics_0.1.4    textshaping_1.0.5
#>  [9] jsonlite_2.0.0    glue_1.8.1        htmltools_0.5.9   ragg_1.5.2       
#> [13] sass_0.4.10       rmarkdown_2.32    tibble_3.3.1      evaluate_1.0.5   
#> [17] jquerylib_0.1.4   fastmap_1.2.0     yaml_2.3.12       lifecycle_1.0.5  
#> [21] compiler_4.5.1    dplyr_1.2.1       fs_2.1.0          pkgconfig_2.0.3  
#> [25] htmlwidgets_1.6.4 Rcpp_1.1.2        rstudioapi_0.19.0 systemfonts_1.3.2
#> [29] digest_0.6.39     R6_2.6.1          tidyselect_1.2.1  pillar_1.11.1    
#> [33] parallel_4.5.1    magrittr_2.0.5    bslib_0.12.0      tools_4.5.1      
#> [37] pkgdown_2.2.1     cachem_1.1.0      desc_1.4.3
```
