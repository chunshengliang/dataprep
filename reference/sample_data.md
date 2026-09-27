# Random sampling with optional stratification

Random sampling with optional stratification

## Usage

``` r
sample_data(
  data,
  group = NULL,
  size = NULL,
  frac = NULL,
  replace = FALSE,
  seed = NULL,
  verbose = FALSE
)
```

## Arguments

- data:

  A data frame.

- group:

  Grouping column.

- size:

  Sample size per group (see Details).

- frac:

  Sampling fraction per group (see Details).

- replace:

  Sample with replacement.

- seed:

  Random seed. If `NULL`, the current RNG state is used; if supplied,
  `set.seed(seed)` is called before sampling.

- verbose:

  Logical.

## Value

A sampled data frame.

## Details

If `group` is `NULL`, either `size` (absolute number of rows) or `frac`
(fraction of `nrow(data)`) must be supplied.

If `group` is provided, sampling is performed separately within each
group. With `size`, every group contributes the same absolute number of
rows (capped at the group's own size unless `replace = TRUE`). With
`frac`, every group contributes its own `round(frac * group_size)` rows
(at least 1), so groups of different sizes are sampled proportionally.
If `group` is provided but both `size` and `frac` are `NULL`, each group
contributes exactly one row.

## Examples

``` r
sample_data(mtcars, frac = 0.5)
#>     mpg cyl  disp  hp drat    wt  qsec vs am gear carb
#> 1  10.4   8 472.0 205 2.93 5.250 17.98  0  0    3    4
#> 2  33.9   4  71.1  65 4.22 1.835 19.90  1  1    4    1
#> 3  26.0   4 120.3  91 4.43 2.140 16.70  0  1    5    2
#> 4  19.7   6 145.0 175 3.62 2.770 15.50  0  1    5    6
#> 5  15.2   8 304.0 150 3.15 3.435 17.30  0  0    3    2
#> 6  14.3   8 360.0 245 3.21 3.570 15.84  0  0    3    4
#> 7  18.1   6 225.0 105 2.76 3.460 20.22  1  0    3    1
#> 8  10.4   8 460.0 215 3.00 5.424 17.82  0  0    3    4
#> 9  24.4   4 146.7  62 3.69 3.190 20.00  1  0    4    2
#> 10 19.2   6 167.6 123 3.92 3.440 18.30  1  0    4    4
#> 11 30.4   4  75.7  52 4.93 1.615 18.52  1  1    4    2
#> 12 22.8   4 108.0  93 3.85 2.320 18.61  1  1    4    1
#> 13 17.8   6 167.6 123 3.92 3.440 18.90  1  0    4    4
#> 14 15.8   8 351.0 264 4.22 3.170 14.50  0  1    5    4
#> 15 15.0   8 301.0 335 3.54 3.570 14.60  0  1    5    8
#> 16 21.5   4 120.1  97 3.70 2.465 20.01  1  0    3    1
sample_data(mtcars, size = 3, group = "cyl", seed = 123)
#>    mpg cyl  disp  hp drat    wt  qsec vs am gear carb
#> 1 19.7   6 145.0 175 3.62 2.770 15.50  0  1    5    6
#> 2 21.4   6 258.0 110 3.08 3.215 19.44  1  0    3    1
#> 3 17.8   6 167.6 123 3.92 3.440 18.90  1  0    4    4
#> 4 30.4   4  95.1 113 3.77 1.513 16.90  1  1    5    2
#> 5 24.4   4 146.7  62 3.69 3.190 20.00  1  0    4    2
#> 6 33.9   4  71.1  65 4.22 1.835 19.90  1  1    4    1
#> 7 13.3   8 350.0 245 3.73 3.840 15.41  0  0    3    4
#> 8 15.2   8 275.8 180 3.07 3.780 18.00  0  0    3    3
#> 9 17.3   8 275.8 180 3.07 3.730 17.60  0  0    3    3

# frac is per-group: large groups contribute more rows
n_by_cyl <- function(d) table(d$cyl)
n_by_cyl(sample_data(mtcars, frac = 0.5, group = "cyl", seed = 1))
#> 
#> 4 6 8 
#> 6 4 7 
```
