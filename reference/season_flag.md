# Season flag

Creates season, month, or quarter indicator from a time column. Useful
for grouped analysis and preprocessing by season.

## Usage

``` r
season_flag(data, date_col = NULL, type = c("season", "month", "quarter"),
            verbose = FALSE)
```

## Arguments

- data:

  A data frame with a time column.

- date_col:

  Time column index or name. If `NULL`, the first column matching `date`
  is used.

- type:

  Type of flag: `"season"` (winter, spring, summer, autumn), `"month"`
  (abbreviated month), or `"quarter"` (Q1-Q4).

- verbose:

  Logical; if `TRUE`, prints progress message.

## Value

A character vector or factor containing the requested flag.

## Examples

``` r
season_flag(data[1:200, c(1, 4, 17:19)], date_col = 1, type = "season")
#>   [1] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>   [9] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [17] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [25] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [33] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [41] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [49] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [57] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [65] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [73] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [81] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [89] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#>  [97] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [105] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [113] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [121] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [129] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [137] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [145] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [153] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [161] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [169] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [177] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [185] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
#> [193] "winter" "winter" "winter" "winter" "winter" "winter" "winter" "winter"
```
