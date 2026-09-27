# Remove linear trend from time series

Fits a linear trend (intercept + slope) to each selected numeric column
(or vector) and returns the residuals. This is useful for removing
long-term trends before correlation analysis.

## Usage

``` r
detrend_ts(data, cols = NULL, date_col = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame, matrix, or numeric vector.

- cols:

  Column indices or names to detrend. If NULL, all numeric columns are
  used.

- date_col:

  Optional time column (not strictly required for linear detrending).

- verbose:

  Logical; if `TRUE`, prints progress message.

## Value

A data frame or vector with the linear trend removed.

## Examples

``` r
detrend_ts(data[1:100, c(1, 4, 17:19)], cols = 3:5)
#>                    date    monthyear       3.98       4.47       5.01
#> 1   2019-12-31 16:00:00 January 2020         NA         NA         NA
#> 2   2019-12-31 16:10:00 January 2020         NA         NA         NA
#> 3   2019-12-31 16:20:00 January 2020         NA         NA         NA
#> 4   2019-12-31 16:30:00 January 2020 -24.922105  -7.256783 -18.187112
#> 5   2019-12-31 16:40:00 January 2020         NA -74.699645   1.437224
#> 6   2019-12-31 16:50:00 January 2020  -7.077163         NA         NA
#> 7   2019-12-31 17:00:00 January 2020  47.337258         NA         NA
#> 8   2019-12-31 17:10:00 January 2020         NA         NA         NA
#> 9   2019-12-31 17:20:00 January 2020  -7.741601         NA         NA
#> 10  2019-12-31 17:30:00 January 2020  -8.282680 -72.620852  -1.625996
#> 11  2019-12-31 17:40:00 January 2020         NA         NA         NA
#> 12  2019-12-31 17:50:00 January 2020         NA         NA         NA
#> 13  2019-12-31 18:00:00 January 2020         NA -63.850636  45.538612
#> 14  2019-12-31 18:10:00 January 2020         NA         NA         NA
#> 15  2019-12-31 18:20:00 January 2020  -9.070476         NA         NA
#> 16  2019-12-31 18:30:00 January 2020  60.410945 151.507180  29.601320
#> 17  2019-12-31 18:40:00 January 2020         NA         NA         NA
#> 18  2019-12-31 18:50:00 January 2020         NA         NA         NA
#> 19  2019-12-31 19:00:00 January 2020         NA         NA         NA
#> 20  2019-12-31 19:10:00 January 2020  62.085028  73.693334   2.107264
#> 21  2019-12-31 19:20:00 January 2020 -10.399351         NA         NA
#> 22  2019-12-31 19:40:00 January 2020         NA         NA         NA
#> 23  2019-12-31 19:50:00 January 2020         NA         NA         NA
#> 24  2019-12-31 20:00:00 January 2020         NA         NA         NA
#> 25  2019-12-31 20:10:00 January 2020         NA         NA         NA
#> 26  2019-12-31 20:20:00 January 2020 -29.894547   2.921266 -23.871721
#> 27  2019-12-31 20:30:00 January 2020 -30.016127   3.565605 -24.069685
#> 28  2019-12-31 20:40:00 January 2020         NA         NA         NA
#> 29  2019-12-31 20:50:00 January 2020         NA -63.051818  -2.389513
#> 30  2019-12-31 21:00:00 January 2020         NA         NA         NA
#> 31  2019-12-31 21:10:00 January 2020  22.677156   2.825759 -25.963641
#> 32  2019-12-31 21:20:00 January 2020 -34.305323   6.750798  14.299895
#> 33  2019-12-31 21:30:00 January 2020  22.382798   4.110636 -26.360969
#> 34  2019-12-31 21:40:00 January 2020         NA         NA         NA
#> 35  2019-12-31 21:50:00 January 2020         NA         NA         NA
#> 36  2019-12-31 22:00:00 January 2020         NA         NA         NA
#> 37  2019-12-31 22:10:00 January 2020         NA         NA         NA
#> 38  2019-12-31 22:20:00 January 2020 -30.406098  12.302429 -25.700389
#> 39  2019-12-31 22:30:00 January 2020 -14.385977         NA         NA
#> 40  2019-12-31 22:40:00 January 2020   6.792143  85.410107  38.956983
#> 41  2019-12-31 22:50:00 January 2020         NA         NA         NA
#> 42  2019-12-31 23:00:00 January 2020 -15.050415         NA         NA
#> 43  2019-12-31 23:10:00 January 2020         NA         NA         NA
#> 44  2019-12-31 23:20:00 January 2020 -18.973173         NA         NA
#> 45  2019-12-31 23:30:00 January 2020         NA         NA         NA
#> 46  2019-12-31 23:40:00 January 2020         NA         NA         NA
#> 47  2019-12-31 23:50:00 January 2020 -16.157811         NA         NA
#> 48  2020-01-01 00:00:00 January 2020         NA         NA         NA
#> 49  2020-01-01 00:10:00 January 2020         NA         NA         NA
#> 50  2020-01-01 00:20:00 January 2020         NA         NA         NA
#> 51  2020-01-01 00:30:00 January 2020         NA         NA         NA
#> 52  2020-01-01 00:40:00 January 2020 -42.528707  17.052970  55.800714
#> 53  2020-01-01 00:50:00 January 2020         NA         NA         NA
#> 54  2020-01-01 01:00:00 January 2020         NA         NA         NA
#> 55  2020-01-01 01:10:00 January 2020 -17.929645         NA         NA
#> 56  2020-01-01 01:20:00 January 2020  15.457176  18.665224 -31.003942
#> 57  2020-01-01 01:30:00 January 2020         NA -50.489737 -13.543706
#> 58  2020-01-01 01:40:00 January 2020         NA -49.651299 -11.403370
#> 59  2020-01-01 01:50:00 January 2020         NA         NA         NA
#> 60  2020-01-01 02:20:00 January 2020         NA         NA         NA
#> 61  2020-01-01 02:30:00 January 2020         NA         NA         NA
#> 62  2020-01-01 02:40:00 January 2020         NA         NA         NA
#> 63  2020-01-01 02:50:00 January 2020         NA         NA         NA
#> 64  2020-01-01 03:00:00 January 2020         NA         NA         NA
#> 65  2020-01-01 03:10:00 January 2020         NA         NA         NA
#> 66  2020-01-01 03:20:00 January 2020         NA         NA         NA
#> 67  2020-01-01 03:30:00 January 2020  40.672505         NA         NA
#> 68  2020-01-01 03:40:00 January 2020         NA         NA         NA
#> 69  2020-01-01 03:50:00 January 2020         NA         NA         NA
#> 70  2020-01-01 04:00:00 January 2020         NA         NA         NA
#> 71  2020-01-01 04:10:00 January 2020         NA         NA         NA
#> 72  2020-01-01 04:20:00 January 2020  46.831109  98.304342 -11.144767
#> 73  2020-01-01 04:30:00 January 2020         NA         NA         NA
#> 74  2020-01-01 04:40:00 January 2020         NA         NA         NA
#> 75  2020-01-01 04:50:00 January 2020         NA         NA         NA
#> 76  2020-01-01 05:00:00 January 2020         NA         NA         NA
#> 77  2020-01-01 05:40:00 January 2020         NA         NA         NA
#> 78  2020-01-01 05:50:00 January 2020         NA         NA         NA
#> 79  2020-01-01 06:00:00 January 2020         NA         NA         NA
#> 80  2020-01-01 06:10:00 January 2020         NA         NA         NA
#> 81  2020-01-01 06:20:00 January 2020         NA -38.828911 -17.285944
#> 82  2020-01-01 06:30:00 January 2020         NA         NA         NA
#> 83  2020-01-01 06:40:00 January 2020         NA -23.679733  74.750028
#> 84  2020-01-01 06:50:00 January 2020         NA -30.442395  27.379364
#> 85  2020-01-01 07:00:00 January 2020         NA         NA         NA
#> 86  2020-01-01 07:10:00 January 2020         NA         NA         NA
#> 87  2020-01-01 07:20:00 January 2020  36.242920         NA         NA
#> 88  2020-01-01 07:30:00 January 2020         NA -35.276840 -17.393592
#> 89  2020-01-01 07:40:00 January 2020 -43.747838  32.738998 -39.927056
#> 90  2020-01-01 07:50:00 January 2020         NA         NA         NA
#> 91  2020-01-01 08:00:00 January 2020         NA         NA         NA
#> 92  2020-01-01 08:10:00 January 2020         NA         NA         NA
#> 93  2020-01-01 08:20:00 January 2020         NA         NA         NA
#> 94  2020-01-01 08:30:00 January 2020         NA         NA         NA
#> 95  2020-01-01 08:40:00 January 2020         NA         NA         NA
#> 96  2020-01-01 08:50:00 January 2020         NA         NA         NA
#> 97  2020-01-01 09:00:00 January 2020         NA         NA         NA
#> 98  2020-01-01 09:10:00 January 2020         NA         NA         NA
#> 99  2020-01-01 09:20:00 January 2020         NA         NA         NA
#> 100 2020-01-01 09:30:00 January 2020         NA         NA         NA
```
