# Remove low-variance (near-constant) variables

Removes numeric columns whose variance (or standard deviation) falls
below a specified threshold.

## Usage

``` r
filter_low_var(data, cols = NULL, cutoff = 0.01,
               method = "var", verbose = FALSE)
```

## Arguments

- data:

  A data frame.

- cols:

  Column indices or names of numeric variables to check. If `NULL`, all
  numeric columns are used.

- cutoff:

  Variance (or standard deviation) threshold. Columns with statistic
  `<= cutoff` are removed.

- method:

  Statistic to use: `"var"` for variance, `"sd"` for standard deviation.

- verbose:

  Logical; if `TRUE`, prints removed column names and their variance.

## Details

Variance is computed with NA values removed. For `method = "sd"`, the
standard deviation is compared **directly** against `cutoff`; no
internal squaring is performed.

## Value

A data frame with low-variance columns removed.

## Examples

``` r
data <- data.frame(
  id = 1:100,
  const = rep(5, 100),
  noise = rnorm(100, sd = 0.005)
)
filter_low_var(data, cutoff = 0.001)
#>      id
#> 1     1
#> 2     2
#> 3     3
#> 4     4
#> 5     5
#> 6     6
#> 7     7
#> 8     8
#> 9     9
#> 10   10
#> 11   11
#> 12   12
#> 13   13
#> 14   14
#> 15   15
#> 16   16
#> 17   17
#> 18   18
#> 19   19
#> 20   20
#> 21   21
#> 22   22
#> 23   23
#> 24   24
#> 25   25
#> 26   26
#> 27   27
#> 28   28
#> 29   29
#> 30   30
#> 31   31
#> 32   32
#> 33   33
#> 34   34
#> 35   35
#> 36   36
#> 37   37
#> 38   38
#> 39   39
#> 40   40
#> 41   41
#> 42   42
#> 43   43
#> 44   44
#> 45   45
#> 46   46
#> 47   47
#> 48   48
#> 49   49
#> 50   50
#> 51   51
#> 52   52
#> 53   53
#> 54   54
#> 55   55
#> 56   56
#> 57   57
#> 58   58
#> 59   59
#> 60   60
#> 61   61
#> 62   62
#> 63   63
#> 64   64
#> 65   65
#> 66   66
#> 67   67
#> 68   68
#> 69   69
#> 70   70
#> 71   71
#> 72   72
#> 73   73
#> 74   74
#> 75   75
#> 76   76
#> 77   77
#> 78   78
#> 79   79
#> 80   80
#> 81   81
#> 82   82
#> 83   83
#> 84   84
#> 85   85
#> 86   86
#> 87   87
#> 88   88
#> 89   89
#> 90   90
#> 91   91
#> 92   92
#> 93   93
#> 94   94
#> 95   95
#> 96   96
#> 97   97
#> 98   98
#> 99   99
#> 100 100
```
