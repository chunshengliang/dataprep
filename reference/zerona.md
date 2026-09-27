# Turn zeros to missing values

Converts zero values to `NA`. Useful when zero represents an invalid
measurement or when a logarithmic scale is desired.

## Usage

``` r
zerona(x)
```

## Arguments

- x:

  A data frame, matrix, or vector containing zeros.

## Value

An object of the same class with zeros replaced by `NA`.

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
zerona(0:5)
#> [1] NA  1  2  3  4  5
zerona(cbind(a = 0:5, b = c(6:10, 0)))
#>       a  b
#> [1,] NA  6
#> [2,]  1  7
#> [3,]  2  8
#> [4,]  3  9
#> [5,]  4 10
#> [6,]  5 NA
```
