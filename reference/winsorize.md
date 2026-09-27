# Winsorize outliers by capping extreme values

Replaces extreme values in selected columns with the specified top and
bottom quantile thresholds, rather than deleting them. This is useful
for reducing the influence of outliers while preserving sample size.

## Usage

``` r
winsorize(data, cols = NULL, top = 0.995, bottom = 0.0025,
          group = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame, matrix, or numeric vector.

- cols:

  The column indices or names of selected variables. If NULL, all
  columns are used.

- top:

  The top quantile threshold. Values above this are capped at the
  threshold.

- bottom:

  The bottom quantile threshold. Values below this are capped at the
  threshold.

- group:

  Optional grouping column for group-wise winsorization.

- verbose:

  Logical; if `TRUE`, prints progress message.

## Value

A data frame with extreme values capped.

## Examples

``` r
winsorize(data[1:100, c(1, 4, 17:19)], cols = 3:5)
#>                    date    monthyear     3.98       4.47      5.01
#> 1   2020-01-01 00:00:00 January 2020       NA         NA        NA
#> 2   2020-01-01 00:10:00 January 2020       NA         NA        NA
#> 3   2020-01-01 00:20:00 January 2020       NA         NA        NA
#> 4   2020-01-01 00:30:00 January 2020  42.9722  74.778500  24.83750
#> 5   2020-01-01 00:40:00 January 2020       NA   6.865100  44.71760
#> 6   2020-01-01 00:50:00 January 2020  61.2601         NA        NA
#> 7   2020-01-01 01:00:00 January 2020 115.8960         NA        NA
#> 8   2020-01-01 01:10:00 January 2020       NA         NA        NA
#> 9   2020-01-01 01:20:00 January 2020  61.2601         NA        NA
#> 10  2020-01-01 01:30:00 January 2020  60.9405   6.592109  42.93320
#> 11  2020-01-01 01:40:00 January 2020       NA         NA        NA
#> 12  2020-01-01 01:50:00 January 2020       NA         NA        NA
#> 13  2020-01-01 02:00:00 January 2020       NA  13.949800  90.86510
#> 14  2020-01-01 02:10:00 January 2020       NA         NA        NA
#> 15  2020-01-01 02:20:00 January 2020  61.2601         NA        NA
#> 16  2020-01-01 02:30:00 January 2020 130.9630 218.996150  75.69510
#> 17  2020-01-01 02:40:00 January 2020       NA         NA        NA
#> 18  2020-01-01 02:50:00 January 2020       NA         NA        NA
#> 19  2020-01-01 03:00:00 January 2020       NA         NA        NA
#> 20  2020-01-01 03:10:00 January 2020 133.1902 148.200000  49.22410
#> 21  2020-01-01 03:20:00 January 2020  61.2601         NA        NA
#> 22  2020-01-01 03:40:00 January 2020       NA         NA        NA
#> 23  2020-01-01 03:50:00 January 2020       NA         NA        NA
#> 24  2020-01-01 04:00:00 January 2020       NA         NA        NA
#> 25  2020-01-01 04:10:00 January 2020       NA         NA        NA
#> 26  2020-01-01 04:20:00 January 2020  42.8723  74.604700  24.77970
#> 27  2020-01-01 04:30:00 January 2020  42.9722  74.778500  24.83750
#> 28  2020-01-01 04:40:00 January 2020       NA         NA        NA
#> 29  2020-01-01 04:50:00 January 2020       NA   7.220000  47.02920
#> 30  2020-01-01 05:00:00 January 2020       NA         NA        NA
#> 31  2020-01-01 05:10:00 January 2020  96.5514  72.156500  23.97317
#> 32  2020-01-01 05:20:00 January 2020  39.7904  75.611000  64.48590
#> 33  2020-01-01 05:30:00 January 2020  96.7000  72.500300  24.08080
#> 34  2020-01-01 05:40:00 January 2020       NA         NA        NA
#> 35  2020-01-01 05:50:00 January 2020       NA         NA        NA
#> 36  2020-01-01 06:00:00 January 2020       NA         NA        NA
#> 37  2020-01-01 06:10:00 January 2020       NA         NA        NA
#> 38  2020-01-01 06:20:00 January 2020  45.0185  78.339400  26.02020
#> 39  2020-01-01 06:30:00 January 2020  61.2601         NA        NA
#> 40  2020-01-01 06:40:00 January 2020  82.6597 150.506000  91.18910
#> 41  2020-01-01 06:50:00 January 2020       NA         NA        NA
#> 42  2020-01-01 07:00:00 January 2020  61.2601         NA        NA
#> 43  2020-01-01 07:10:00 January 2020       NA         NA        NA
#> 44  2020-01-01 07:20:00 January 2020  57.7803         NA        NA
#> 45  2020-01-01 07:30:00 January 2020       NA         NA        NA
#> 46  2020-01-01 07:40:00 January 2020       NA         NA        NA
#> 47  2020-01-01 07:50:00 January 2020  61.2601         NA        NA
#> 48  2020-01-01 08:00:00 January 2020       NA         NA        NA
#> 49  2020-01-01 08:10:00 January 2020       NA         NA        NA
#> 50  2020-01-01 08:20:00 January 2020       NA         NA        NA
#> 51  2020-01-01 08:30:00 January 2020       NA         NA        NA
#> 52  2020-01-01 08:40:00 January 2020  36.2432  76.502400 111.10200
#> 53  2020-01-01 08:50:00 January 2020       NA         NA        NA
#> 54  2020-01-01 09:00:00 January 2020       NA         NA        NA
#> 55  2020-01-01 09:10:00 January 2020  61.2601         NA        NA
#> 56  2020-01-01 09:20:00 January 2020  94.8684  76.232500  25.32040
#> 57  2020-01-01 09:30:00 January 2020       NA   6.607000  43.03640
#> 58  2020-01-01 09:40:00 January 2020       NA   6.974900  45.43250
#> 59  2020-01-01 09:50:00 January 2020       NA         NA        NA
#> 60  2020-01-01 10:20:00 January 2020       NA         NA        NA
#> 61  2020-01-01 10:30:00 January 2020       NA         NA        NA
#> 62  2020-01-01 10:40:00 January 2020       NA         NA        NA
#> 63  2020-01-01 10:50:00 January 2020       NA         NA        NA
#> 64  2020-01-01 11:00:00 January 2020       NA         NA        NA
#> 65  2020-01-01 11:10:00 January 2020       NA         NA        NA
#> 66  2020-01-01 11:20:00 January 2020       NA         NA        NA
#> 67  2020-01-01 11:30:00 January 2020 122.5200         NA        NA
#> 68  2020-01-01 11:40:00 January 2020       NA         NA        NA
#> 69  2020-01-01 11:50:00 January 2020       NA         NA        NA
#> 70  2020-01-01 12:00:00 January 2020       NA         NA        NA
#> 71  2020-01-01 12:10:00 January 2020       NA         NA        NA
#> 72  2020-01-01 12:20:00 January 2020 129.7860 148.343000  49.27180
#> 73  2020-01-01 12:30:00 January 2020       NA         NA        NA
#> 74  2020-01-01 12:40:00 January 2020       NA         NA        NA
#> 75  2020-01-01 12:50:00 January 2020       NA         NA        NA
#> 76  2020-01-01 13:00:00 January 2020       NA         NA        NA
#> 77  2020-01-01 13:40:00 January 2020       NA         NA        NA
#> 78  2020-01-01 13:50:00 January 2020       NA         NA        NA
#> 79  2020-01-01 14:00:00 January 2020       NA         NA        NA
#> 80  2020-01-01 14:10:00 January 2020       NA         NA        NA
#> 81  2020-01-01 14:20:00 January 2020       NA   6.974900  45.43250
#> 82  2020-01-01 14:30:00 January 2020       NA         NA        NA
#> 83  2020-01-01 14:40:00 January 2020       NA  21.183000 134.88903
#> 84  2020-01-01 14:50:00 January 2020       NA  13.949800  90.86510
#> 85  2020-01-01 15:00:00 January 2020       NA         NA        NA
#> 86  2020-01-01 15:10:00 January 2020       NA         NA        NA
#> 87  2020-01-01 15:20:00 January 2020 122.5200         NA        NA
#> 88  2020-01-01 15:30:00 January 2020       NA   7.233200  47.11520
#> 89  2020-01-01 15:40:00 January 2020  42.9722  74.778500  24.83750
#> 90  2020-01-01 15:50:00 January 2020       NA         NA        NA
#> 91  2020-01-01 16:00:00 January 2020       NA         NA        NA
#> 92  2020-01-01 16:10:00 January 2020       NA         NA        NA
#> 93  2020-01-01 16:20:00 January 2020       NA         NA        NA
#> 94  2020-01-01 16:30:00 January 2020       NA         NA        NA
#> 95  2020-01-01 16:40:00 January 2020       NA         NA        NA
#> 96  2020-01-01 16:50:00 January 2020       NA         NA        NA
#> 97  2020-01-01 17:00:00 January 2020       NA         NA        NA
#> 98  2020-01-01 17:10:00 January 2020       NA         NA        NA
#> 99  2020-01-01 17:20:00 January 2020       NA         NA        NA
#> 100 2020-01-01 17:30:00 January 2020       NA         NA        NA
```
