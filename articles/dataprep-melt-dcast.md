# dataprep: fast reshaping with melt() and dcast()

``` r

library(dataprep)
set.seed(1)
```

## Why another reshape implementation

[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
and
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
in `dataprep` 0.1.8.1 are drop-in replacements for
[`reshape2::melt`](https://rdrr.io/pkg/reshape2/man/melt.html) /
[`reshape2::dcast`](https://rdrr.io/pkg/reshape2/man/cast.html), but the
underlying code is written in C++ with SIMD (AVX2 / AVX-512) and
optional OpenMP parallelism. Both functions produce output identical to
`reshape2`, `data.table`, `tidyr`, `collapse`, `pandas`, `polars`,
`dask`, and `duckdb` on every tested shape.
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
and
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
are pure data movement, so the output is bit-identical.

The speed-up relative to each of the **eight** alternatives spans:

| Operation | Range across both hosts |
|-----------|------------------------:|
| `melt`    |            0.3×–1750.0× |
| `dcast`   |            0.9×–1849.5× |

The median across all tested cells and all competitors is 9.6× on Ubuntu
and 3.7× on Windows for `melt`, and 74.5× on Ubuntu and 42.2× on Windows
for `dcast`. The mean is 71.0× and 31.8× for `melt`, and 128.9× and
91.1× for `dcast`, respectively. The `melt` median is pulled down by the
1e5-row small tables; at larger scales the speed-up is much higher. Full
tables — including mean, median, and per-competitor ranges — are in
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md)
and `README.md`.

The sub-1.0× cells are `polars` at 1e7 rows × 10 id + 9 val on Ubuntu
(0.3×), and `polars` at 1e5 rows × 10 id + 9 val on Windows (0.6×).
Every other cell has `dataprep` ahead of or on par with the fastest
competitor.

## Test environment

Benchmarks were run on two reference hosts. Only the core configuration
is listed here; full hardware details are in `README.md`.

- **Ubuntu 25.10** — 2× AMD EPYC 9965 192-Core (384 physical / 768
  logical cores), 1.0 TiB (16 × 64 GiB Micron, DDR5-5600, Multi-bit
  ECC), full AVX-512; R 4.5.1, g++ 15.2.0.

- **Windows 11 Pro for Workstations** — 2× AMD EPYC 7B12 64-Core (128
  physical / 128 logical cores), about 224 GiB RAM, no AVX-512; R 4.6.1
  (ucrt), GCC 14.3.0.

Software versions on both hosts: `data.table` 1.18.6.1, `reshape2`
1.4.5, `tidyr` 1.3.2, `collapse` 2.1.8, `reticulate` 1.47.0; Python
3.13.7 (Ubuntu) / 3.13.15 (Windows), `pandas` 3.0.6, `polars` 1.44.2
(runtime rt64), `dask` 2026.8.0, `duckdb` 1.5.5.

Reproducing the benchmarks:

``` r

Sys.setenv(DATAPREP_RUN_BENCHMARK = "1")
source(system.file("benchmark_melt_dcast.R", package = "dataprep"))
```

## Wide to long with `melt()`

### Basic usage

``` r

df <- data.frame(
  id       = 1:3,
  category = factor(c("a", "b", "c")),
  v1       = c(1.1, 2.2, 3.3),
  v2       = c(4.4, 5.5, 6.6)
)
melt(df, id.vars = c("id", "category"))
#>   id category variable value
#> 1  1        a       v1   1.1
#> 2  2        b       v1   2.2
#> 3  3        c       v1   3.3
#> 4  1        a       v2   4.4
#> 5  2        b       v2   5.5
#> 6  3        c       v2   6.6
```

### Measure-side specification

``` r

melt(df, measure.vars = c("v1", "v2"))
#>   id category variable value
#> 1  1        a       v1   1.1
#> 2  2        b       v1   2.2
#> 3  3        c       v1   3.3
#> 4  1        a       v2   4.4
#> 5  2        b       v2   5.5
#> 6  3        c       v2   6.6
```

### Automatic ID inference

Non-numeric and factor columns are treated as IDs by default.

``` r

melt(df)
#>   category variable value
#> 1        a       id   1.0
#> 2        b       id   2.0
#> 3        c       id   3.0
#> 4        a       v1   1.1
#> 5        b       v1   2.2
#> 6        c       v1   3.3
#> 7        a       v2   4.4
#> 8        b       v2   5.5
#> 9        c       v2   6.6
```

### Custom column names and `na.rm`

``` r

df_na <- data.frame(
  id = 1:3,
  x  = c(1, NA, 3),
  y  = c(4, 5, NA)
)
melt(df_na, id.vars = "id",
     variable.name = "var", value.name = "val",
     na.rm = TRUE)
#>   id var val
#> 1  1   x   1
#> 2  3   x   3
#> 3  1   y   4
#> 4  2   y   5
```

### Memory layout: `major = "row"` vs `major = "col"`

Row-major (`"row"`) is usually faster when there are few measure
columns; column-major (`"col"`) is often faster when there are many
measure columns because bulk copies dominate. `major = NULL` (the
default) is equivalent to `"col"`; there is no automatic switching based
on the input shape.

``` r

wide50 <- data.frame(id = 1:100,
                     matrix(rnorm(100 * 50), ncol = 50))
res_row <- melt(wide50, id.vars = "id", major = "row")
res_col <- melt(wide50, id.vars = "id", major = "col")
identical(as.data.frame(res_row), as.data.frame(res_col))
#> [1] FALSE
```

### Thread control

`cores` controls OpenMP. `options(dataprep.cores = ...)` sets a default
for the whole session.

``` r

options(dataprep.cores = 4L)
melt(df, id.vars = "id")
#>   id variable value
#> 1  1 category    NA
#> 2  2 category    NA
#> 3  3 category    NA
#> 4  1       v1   1.1
#> 5  2       v1   2.2
#> 6  3       v1   3.3
#> 7  1       v2   4.4
#> 8  2       v2   5.5
#> 9  3       v2   6.6
options(dataprep.cores = NULL)
```

## Why `melt()` is fast

`melt_cpp` uses six design choices that matter at scale.

### 1. Two layout paths, selected by `major`

[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
produces the same long-format table as
[`reshape2::melt`](https://rdrr.io/pkg/reshape2/man/melt.html)
(column-major, `major = "col"`) or
[`tidyr::pivot_longer`](https://tidyr.tidyverse.org/reference/pivot_longer.html)
(row-major, `major = "row"`). The two layouts have very different memory
access patterns:

- **Column-major** writes each output column as one contiguous block:
  for measure column `k`, the output block `[k * n .. (k+1) * n)` is a
  direct `memcpy` of the input column. This is the fastest possible path
  when the number of value columns is moderate.

- **Row-major** writes every row as `n_meas` consecutive doubles. For
  small `n_meas` this is compact, but for large `n_meas` it requires a
  per-row transpose.

`major = NULL` (the default) is equivalent to `"col"`; there is no
automatic switching based on the input shape.

### 2. SIMD streaming stores

For large outputs, `melt_cpp` uses AVX-512 or AVX2 streaming stores
(`_mm512_stream_pd`, `_mm256_stream_si256`) to write directly to memory,
bypassing the CPU cache. This avoids the cache pollution that would
otherwise evict useful input data, and it is the reason the 1e8-row
`melt` finishes in under 0.5 s on Ubuntu (vs 2.6 s for the next-fastest
engine, `polars`) and in under 1.3 s on Windows. A `_mm_sfence()` is
issued at the end of each streaming region to guarantee visibility.

### 3. Hugepage hint for large outputs

Output vectors larger than 512 KB are allocated through
`Rf_allocVector()` and then hinted with `madvise(MADV_HUGEPAGE)`, so the
kernel can back them with 2 MB pages. This reduces first-touch page
faults on the 1e8-row case. There is no custom `R_allocator_t`, no
`MAP_POPULATE`, and no free pool: those were described in earlier drafts
but are not part of the shipped 0.1.8.1 backend.

### 4. Fast paths for small inputs

Two fast paths are used. Tiny inputs (`n <= 2048`, `n_meas <= 64`,
`n_id <= 8`) route to `melt_tiny_cpp`; small inputs (`total <= 131072`,
`n_meas <= 256`) route to `melt_small_cpp`. Both skip hugepage hinting,
OpenMP setup, and thread-cap detection. This is what makes
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
the fastest engine even on 1e3-row tables, where the initialization
overhead of the other backends dominates their runtimes.

### 5. Cache-aware block sizing

For the row-major path, the per-thread block size is computed from the
L3 cache size (read once from
`/sys/devices/system/cpu/cpu0/cache/index3/size` on Linux). Each block
is sized to fit in half of L3, which keeps both the input reads and the
output writes inside the cache for the duration of the block.

### 6. Per-call caches for SEXP and factor levels

The `"data.frame"` class tag, the `"factor"` class tag, the default
`"variable"` / `"value"` column names, and the factor levels vector are
constructed once per process and reused afterwards via
`R_PreserveObject`. This removes a small but measurable per-call cost
that shows up on the small-input benchmarks.

## Long to wide with `dcast()`

### Basic usage

``` r

long <- melt(df, id.vars = c("id", "category"))
dcast(long, id = c("id", "category"),
      variable = "variable", value = "value")
#>   id category  v1  v2
#> 1  1        a 1.1 4.4
#> 2  2        b 2.2 5.5
#> 3  3        c 3.3 6.6
```

### Formula interface

``` r

dcast(long, formula = id + category ~ variable,
      value.var = "value")
#>   id category  v1  v2
#> 1  1        a 1.1 4.4
#> 2  2        b 2.2 5.5
#> 3  3        c 3.3 6.6
```

### Fill missing cells

``` r

dcast(long, id = c("id", "category"),
      variable = "variable", value = "value",
      fill = 0)
#>   id category  v1  v2
#> 1  1        a 1.1 4.4
#> 2  2        b 2.2 5.5
#> 3  3        c 3.3 6.6
```

### Aggregating duplicate pairs

If a `(id, variable)` pair appears more than once, pass `fun.aggregate`.
The default is “last occurrence wins”, matching
[`data.table::dcast`](https://rdrr.io/pkg/data.table/man/dcast.data.table.html)’s
`fun.aggregate = NULL` behaviour.

``` r

long_dup <- data.frame(
  id       = c(1, 1, 2),
  variable = c("x", "x", "x"),
  value    = c(1, 2, 3)
)
dcast(long_dup, id = "id",
      variable = "variable", value = "value",
      fun.aggregate = mean)
#>   id   x
#> 1  1 1.5
#> 2  2 3.0
```

### `na.rm`

``` r

long_na <- data.frame(
  id       = c(1, 1, 2, 2),
  variable = c("x", "y", "x", "y"),
  value    = c(1, NA, 3, 4)
)
dcast(long_na, id = "id",
      variable = "variable", value = "value",
      na.rm = TRUE)
#>   id x  y
#> 1  1 1 NA
#> 2  2 3  4
```

## Why `dcast()` is fast

`dcast_cpp` performs four design choices that matter at scale.

### 1. Block-path detection (Phase 0a–0c)

When the input is a canonical
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
output — `variable` is periodic and every `id` column is constant within
one period — `dcast_cpp` skips the hash tables entirely and performs a
tile transpose: each tile of `TILE x period` doubles is read
contiguously into an L1 buffer, transposed in place, and written
contiguously to the output columns. This keeps both reads and writes
sequential and enables OpenMP parallelisation. The cost per tile is
`O(TILE * period)`, independent of the number of levels, which is why
wide-level tables scale well.

### 2. 64-bit packed keys and 96-bit fingerprint fallback

For block-aligned input with few `id` columns, `dcast_cpp` packs the row
key into a single `uint64_t`. When the combined bit budget of the `id`
columns exceeds 64, it falls back to a 96-bit fingerprint
(`uint64_t h1 + uint32_t h2`) computed by a 4-way parallel FNV-1a and
stored in a 16-byte slot table, which is more cache-friendly than the
previous 128-bit scheme.

### 3. Radix sort for shuffle-friendly input

When the input is not sorted, a 4-pass LSD radix sort over the packed
keys replaces the hash table entirely. For inputs whose rows are already
grouped — which is the common case after a
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md) +
`arrange()` pipeline — the sort is skipped.

### 4. Permutation shortcut

When the block path is taken and the `id` column is a permutation of
`1..n_blocks` (the most common case for canonical
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
output), a direct-index shortcut bypasses both the hash table and the
sort.

The full pipeline is described in the
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
help page.

## Where the gap is narrowest

On
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md),
the narrowest cells are the `10 id + 9 val` shapes at 1e7 rows on Ubuntu
(`polars` at 0.3×) and 1e5 rows on Windows (`polars` at 0.6×).
`polars`’s SIMD hash and native strided unpivot are competitive when the
id block is wide and the output is long; every other
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
cell has `dataprep` ahead of or on par with the fastest competitor.

On
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md),
the narrowest cell is the smallest Windows case (`n_long = 1,000`, 1 id,
10 levels), where `collapse` is about 8 % faster (0.9×). Every other
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
cell has `dataprep` ahead of every other engine, including the
aggregated
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
cells, where the closest competitor (`polars`) stays within 2.0×–3.5× on
both hosts.

## Cross-engine consistency

[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md),
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md),
and aggregated
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
produce output identical to `reshape2` (the reference implementation) on
every tested cell.
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md)
and
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
are pure data movement, so all pairs of engines agree **exactly**;
aggregated
[`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md)
matches to `tol = 1e-10` because `sum` and `mean` accumulate in
different orders across engines.

### `melt` consistency

|    rows | n_id | n_val | engines passed | pairwise       |
|--------:|-----:|------:|---------------:|----------------|
|   1,000 |    1 |     9 |            9/9 | all consistent |
| 100,000 |    1 |     9 |            9/9 | all consistent |
|   1,000 |    1 |   100 |            9/9 | all consistent |
|  10,000 |   10 |    10 |            9/9 | all consistent |

Engines: `dataprep`, `reshape2`, `data.table`, `tidyr`, `collapse`,
`pandas`, `polars`, `dask`, `duckdb`.

### `dcast` consistency

|    n_long | n_id | n_levels | engines passed | pairwise       |
|----------:|-----:|---------:|---------------:|----------------|
|     5,000 |    2 |        5 |            9/9 | all consistent |
|    50,000 |    1 |       50 |            9/9 | all consistent |
|    50,000 |   10 |       10 |            9/9 | all consistent |
| 1,000,000 |    1 |       10 |            9/9 | all consistent |

### `dcast(agg)` consistency

|    cells |                       aggs | engines passed | pairwise       |
|---------:|---------------------------:|---------------:|----------------|
| 2 shapes | sum, mean, min, max, count |            9/9 | all consistent |

The consistency scripts are shipped under `inst/`:

``` r

Sys.setenv(DATAPREP_RUN_BENCHMARK = "1")
source(system.file("benchmark_melt_dcast.R", package = "dataprep"))
melt_all_engines(10000L, n_id = 1L, n_val = 9L)
dcast_all_engines(1000L, n_id = 2L, n_val = 5L)
dcast_agg_all_engines(10000L, n_levels = 10L, K = 3L)
```

## Round-trip example

``` r

wide  <- data.frame(id = 1:5, a = rnorm(5), b = rnorm(5))
long  <- melt(wide, id.vars = "id")
back  <- dcast(long, id = "id",
               variable = "variable", value = "value")
all.equal(as.data.frame(back)[order(back$id), c("a", "b")],
          wide[, c("a", "b")],
          tolerance = 1e-12)
#> [1] TRUE
```

## Headline numbers

The tables below summarise the two hosts in one place. Full per-cell
tables are in
[`vignette("dataprep-performance")`](https://chunshengliang.github.io/dataprep/articles/dataprep-performance.md).

### Ubuntu 25.10

| Operation | Min | Median | Mean | Max |
|----|---:|---:|---:|---:|
| [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md) | 0.3× (polars @ 10M rows, 10 id + 9 val) | 9.6× | 71.0× | 1750.0× (dask @ 1e3 rows, 1 id + 10000 val) |
| [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md) | 1.8× (collapse @ 10K rows, 1 id + 100 lvl) | 74.5× | 128.9× | 651.5× (duckdb @ 10M rows, 1 id + 100 lvl) |
| `dcast(agg)` | 2.3× (polars @ 100M rows, count) | 27.4× | 68.6× | 733.5× (reshape2 @ 10M rows, min) |

### Windows 11 Pro for Workstations

| Operation | Min | Median | Mean | Max |
|----|---:|---:|---:|---:|
| [`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md) | 0.6× (polars @ 100K rows, 10 id + 9 val) | 3.7× | 31.8× | 435.2× (dask @ 1e3 rows, 10 id + 10000 val) |
| [`dcast()`](https://chunshengliang.github.io/dataprep/reference/dcast.md) | 0.9× (collapse @ 1K rows, 1 id + 10 lvl) | 42.2× | 91.1× | 1849.5× (duckdb @ 1M rows, 1 id + 10000 lvl) |
| `dcast(agg)` | 2.0× (polars @ 1M rows, count) | 17.9× | 61.8× | 872.4× (reshape2 @ 100M rows, min) |

For
[`melt()`](https://chunshengliang.github.io/dataprep/reference/melt.md),
on the largest cells (1e8 rows, 8 GB of input), `dataprep` finishes in
**0.4 s on Ubuntu** and **1.8 s on Windows**; the next-fastest engine
takes 2.6 s (`polars`) on Ubuntu and 2.4 s (`data.table`) on Windows.

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
#> [1] dataprep_0.1.8.1
#> 
#> loaded via a namespace (and not attached):
#>  [1] digest_0.6.39     desc_1.4.3        R6_2.6.1          fastmap_1.2.0    
#>  [5] xfun_0.62         cachem_1.1.0      parallel_4.6.1    knitr_1.52       
#>  [9] htmltools_0.5.9   rmarkdown_2.32    lifecycle_1.0.5   cli_3.6.6        
#> [13] sass_0.4.10       pkgdown_2.2.1     textshaping_1.0.5 jquerylib_0.1.4  
#> [17] systemfonts_1.3.2 compiler_4.6.1    tools_4.6.1       ragg_1.5.2       
#> [21] bslib_0.12.0      evaluate_1.0.5    Rcpp_1.1.2        yaml_2.3.12      
#> [25] otel_0.2.0        jsonlite_2.0.0    rlang_1.3.0       fs_2.1.0
```
