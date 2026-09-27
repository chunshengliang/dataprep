# Create lagged variables

Generates lagged (or lead) versions of selected numeric columns.
Positive lags shift values backward in time (past values), negative lags
shift forward. Useful for time series modeling.

## Usage

``` r
create_lags(data, cols = NULL, lags = 1, prefix = "lag_",
            group = NULL, date_col = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame, matrix, or numeric vector.

- cols:

  Column indices or names to create lags for.

- lags:

  Vector of lag orders (positive for past, negative for future).

- prefix:

  Prefix for new column names.

- group:

  Optional grouping column for group-specific lags.

- date_col:

  Time column (used to ensure order if provided).

- verbose:

  Logical; if `TRUE`, prints number of lag columns created.

## Value

A data frame or matrix with additional lag columns.

## Examples

``` r
create_lags(data[1:100, c(1, 4, 17:19)], cols = 3:5, lags = c(1, 2))
#>                   date    monthyear     3.98     4.47     5.01 lag_3.98_1
#> 1  2020-01-01 00:00:00 January 2020       NA       NA       NA         NA
#> 2  2020-01-01 00:10:00 January 2020       NA       NA       NA         NA
#> 3  2020-01-01 00:20:00 January 2020       NA       NA       NA         NA
#> 4  2020-01-01 00:30:00 January 2020  42.9722  74.7785  24.8375         NA
#> 5  2020-01-01 00:40:00 January 2020       NA   6.8651  44.7176    42.9722
#> 6  2020-01-01 00:50:00 January 2020  61.2601       NA       NA         NA
#> 7  2020-01-01 01:00:00 January 2020 115.8960       NA       NA    61.2601
#> 8  2020-01-01 01:10:00 January 2020       NA       NA       NA   115.8960
#> 9  2020-01-01 01:20:00 January 2020  61.2601       NA       NA         NA
#> 10 2020-01-01 01:30:00 January 2020  60.9405   6.5912  42.9332    61.2601
#> 11 2020-01-01 01:40:00 January 2020       NA       NA       NA    60.9405
#> 12 2020-01-01 01:50:00 January 2020       NA       NA       NA         NA
#> 13 2020-01-01 02:00:00 January 2020       NA  13.9498  90.8651         NA
#> 14 2020-01-01 02:10:00 January 2020       NA       NA       NA         NA
#> 15 2020-01-01 02:20:00 January 2020  61.2601       NA       NA         NA
#> 16 2020-01-01 02:30:00 January 2020 130.9630 227.8960  75.6951    61.2601
#> 17 2020-01-01 02:40:00 January 2020       NA       NA       NA   130.9630
#> 18 2020-01-01 02:50:00 January 2020       NA       NA       NA         NA
#> 19 2020-01-01 03:00:00 January 2020       NA       NA       NA         NA
#> 20 2020-01-01 03:10:00 January 2020 133.5230 148.2000  49.2241         NA
#> 21 2020-01-01 03:20:00 January 2020  61.2601       NA       NA   133.5230
#> 22 2020-01-01 03:40:00 January 2020       NA       NA       NA    61.2601
#> 23 2020-01-01 03:50:00 January 2020       NA       NA       NA         NA
#> 24 2020-01-01 04:00:00 January 2020       NA       NA       NA         NA
#> 25 2020-01-01 04:10:00 January 2020       NA       NA       NA         NA
#> 26 2020-01-01 04:20:00 January 2020  42.8723  74.6047  24.7797         NA
#> 27 2020-01-01 04:30:00 January 2020  42.9722  74.7785  24.8375    42.8723
#> 28 2020-01-01 04:40:00 January 2020       NA       NA       NA    42.9722
#> 29 2020-01-01 04:50:00 January 2020       NA   7.2200  47.0292         NA
#> 30 2020-01-01 05:00:00 January 2020       NA       NA       NA         NA
#> 31 2020-01-01 05:10:00 January 2020  96.5514  72.1565  23.9666         NA
#> 32 2020-01-01 05:20:00 January 2020  39.7904  75.6110  64.4859    96.5514
#> 33 2020-01-01 05:30:00 January 2020  96.7000  72.5003  24.0808    39.7904
#> 34 2020-01-01 05:40:00 January 2020       NA       NA       NA    96.7000
#> 35 2020-01-01 05:50:00 January 2020       NA       NA       NA         NA
#> 36 2020-01-01 06:00:00 January 2020       NA       NA       NA         NA
#> 37 2020-01-01 06:10:00 January 2020       NA       NA       NA         NA
#> 38 2020-01-01 06:20:00 January 2020  45.0185  78.3394  26.0202         NA
#> 39 2020-01-01 06:30:00 January 2020  61.2601       NA       NA    45.0185
#> 40 2020-01-01 06:40:00 January 2020  82.6597 150.5060  91.1891    61.2601
#> 41 2020-01-01 06:50:00 January 2020       NA       NA       NA    82.6597
#> 42 2020-01-01 07:00:00 January 2020  61.2601       NA       NA         NA
#> 43 2020-01-01 07:10:00 January 2020       NA       NA       NA    61.2601
#> 44 2020-01-01 07:20:00 January 2020  57.7803       NA       NA         NA
#> 45 2020-01-01 07:30:00 January 2020       NA       NA       NA    57.7803
#> 46 2020-01-01 07:40:00 January 2020       NA       NA       NA         NA
#> 47 2020-01-01 07:50:00 January 2020  61.2601       NA       NA         NA
#> 48 2020-01-01 08:00:00 January 2020       NA       NA       NA    61.2601
#> 49 2020-01-01 08:10:00 January 2020       NA       NA       NA         NA
#> 50 2020-01-01 08:20:00 January 2020       NA       NA       NA         NA
#> 51 2020-01-01 08:30:00 January 2020       NA       NA       NA         NA
#> 52 2020-01-01 08:40:00 January 2020  35.9966  76.5024 111.1020         NA
#> 53 2020-01-01 08:50:00 January 2020       NA       NA       NA    35.9966
#> 54 2020-01-01 09:00:00 January 2020       NA       NA       NA         NA
#> 55 2020-01-01 09:10:00 January 2020  61.2601       NA       NA         NA
#> 56 2020-01-01 09:20:00 January 2020  94.8684  76.2325  25.3204    61.2601
#> 57 2020-01-01 09:30:00 January 2020       NA   6.6070  43.0364    94.8684
#> 58 2020-01-01 09:40:00 January 2020       NA   6.9749  45.4325         NA
#> 59 2020-01-01 09:50:00 January 2020       NA       NA       NA         NA
#> 60 2020-01-01 10:20:00 January 2020       NA       NA       NA         NA
#> 61 2020-01-01 10:30:00 January 2020       NA       NA       NA         NA
#> 62 2020-01-01 10:40:00 January 2020       NA       NA       NA         NA
#> 63 2020-01-01 10:50:00 January 2020       NA       NA       NA         NA
#> 64 2020-01-01 11:00:00 January 2020       NA       NA       NA         NA
#> 65 2020-01-01 11:10:00 January 2020       NA       NA       NA         NA
#> 66 2020-01-01 11:20:00 January 2020       NA       NA       NA         NA
#> 67 2020-01-01 11:30:00 January 2020 122.5200       NA       NA         NA
#> 68 2020-01-01 11:40:00 January 2020       NA       NA       NA   122.5200
#> 69 2020-01-01 11:50:00 January 2020       NA       NA       NA         NA
#> 70 2020-01-01 12:00:00 January 2020       NA       NA       NA         NA
#> 71 2020-01-01 12:10:00 January 2020       NA       NA       NA         NA
#> 72 2020-01-01 12:20:00 January 2020 129.7860 148.3430  49.2718         NA
#> 73 2020-01-01 12:30:00 January 2020       NA       NA       NA   129.7860
#> 74 2020-01-01 12:40:00 January 2020       NA       NA       NA         NA
#> 75 2020-01-01 12:50:00 January 2020       NA       NA       NA         NA
#> 76 2020-01-01 13:00:00 January 2020       NA       NA       NA         NA
#> 77 2020-01-01 13:40:00 January 2020       NA       NA       NA         NA
#> 78 2020-01-01 13:50:00 January 2020       NA       NA       NA         NA
#> 79 2020-01-01 14:00:00 January 2020       NA       NA       NA         NA
#> 80 2020-01-01 14:10:00 January 2020       NA       NA       NA         NA
#> 81 2020-01-01 14:20:00 January 2020       NA   6.9749  45.4325         NA
#> 82 2020-01-01 14:30:00 January 2020       NA       NA       NA         NA
#> 83 2020-01-01 14:40:00 January 2020       NA  21.1830 137.9800         NA
#> 84 2020-01-01 14:50:00 January 2020       NA  13.9498  90.8651         NA
#> 85 2020-01-01 15:00:00 January 2020       NA       NA       NA         NA
#> 86 2020-01-01 15:10:00 January 2020       NA       NA       NA         NA
#> 87 2020-01-01 15:20:00 January 2020 122.5200       NA       NA         NA
#> 88 2020-01-01 15:30:00 January 2020       NA   7.2332  47.1152   122.5200
#> 89 2020-01-01 15:40:00 January 2020  42.9722  74.7785  24.8375         NA
#> 90 2020-01-01 15:50:00 January 2020       NA       NA       NA    42.9722
#>    lag_3.98_2 lag_4.47_1 lag_4.47_2 lag_5.01_1 lag_5.01_2
#> 1          NA         NA         NA         NA         NA
#> 2          NA         NA         NA         NA         NA
#> 3          NA         NA         NA         NA         NA
#> 4          NA         NA         NA         NA         NA
#> 5          NA    74.7785         NA    24.8375         NA
#> 6     42.9722     6.8651    74.7785    44.7176    24.8375
#> 7          NA         NA     6.8651         NA    44.7176
#> 8     61.2601         NA         NA         NA         NA
#> 9    115.8960         NA         NA         NA         NA
#> 10         NA         NA         NA         NA         NA
#> 11    61.2601     6.5912         NA    42.9332         NA
#> 12    60.9405         NA     6.5912         NA    42.9332
#> 13         NA         NA         NA         NA         NA
#> 14         NA    13.9498         NA    90.8651         NA
#> 15         NA         NA    13.9498         NA    90.8651
#> 16         NA         NA         NA         NA         NA
#> 17    61.2601   227.8960         NA    75.6951         NA
#> 18   130.9630         NA   227.8960         NA    75.6951
#> 19         NA         NA         NA         NA         NA
#> 20         NA         NA         NA         NA         NA
#> 21         NA   148.2000         NA    49.2241         NA
#> 22   133.5230         NA   148.2000         NA    49.2241
#> 23    61.2601         NA         NA         NA         NA
#> 24         NA         NA         NA         NA         NA
#> 25         NA         NA         NA         NA         NA
#> 26         NA         NA         NA         NA         NA
#> 27         NA    74.6047         NA    24.7797         NA
#> 28    42.8723    74.7785    74.6047    24.8375    24.7797
#> 29    42.9722         NA    74.7785         NA    24.8375
#> 30         NA     7.2200         NA    47.0292         NA
#> 31         NA         NA     7.2200         NA    47.0292
#> 32         NA    72.1565         NA    23.9666         NA
#> 33    96.5514    75.6110    72.1565    64.4859    23.9666
#> 34    39.7904    72.5003    75.6110    24.0808    64.4859
#> 35    96.7000         NA    72.5003         NA    24.0808
#> 36         NA         NA         NA         NA         NA
#> 37         NA         NA         NA         NA         NA
#> 38         NA         NA         NA         NA         NA
#> 39         NA    78.3394         NA    26.0202         NA
#> 40    45.0185         NA    78.3394         NA    26.0202
#> 41    61.2601   150.5060         NA    91.1891         NA
#> 42    82.6597         NA   150.5060         NA    91.1891
#> 43         NA         NA         NA         NA         NA
#> 44    61.2601         NA         NA         NA         NA
#> 45         NA         NA         NA         NA         NA
#> 46    57.7803         NA         NA         NA         NA
#> 47         NA         NA         NA         NA         NA
#> 48         NA         NA         NA         NA         NA
#> 49    61.2601         NA         NA         NA         NA
#> 50         NA         NA         NA         NA         NA
#> 51         NA         NA         NA         NA         NA
#> 52         NA         NA         NA         NA         NA
#> 53         NA    76.5024         NA   111.1020         NA
#> 54    35.9966         NA    76.5024         NA   111.1020
#> 55         NA         NA         NA         NA         NA
#> 56         NA         NA         NA         NA         NA
#> 57    61.2601    76.2325         NA    25.3204         NA
#> 58    94.8684     6.6070    76.2325    43.0364    25.3204
#> 59         NA     6.9749     6.6070    45.4325    43.0364
#> 60         NA         NA     6.9749         NA    45.4325
#> 61         NA         NA         NA         NA         NA
#> 62         NA         NA         NA         NA         NA
#> 63         NA         NA         NA         NA         NA
#> 64         NA         NA         NA         NA         NA
#> 65         NA         NA         NA         NA         NA
#> 66         NA         NA         NA         NA         NA
#> 67         NA         NA         NA         NA         NA
#> 68         NA         NA         NA         NA         NA
#> 69   122.5200         NA         NA         NA         NA
#> 70         NA         NA         NA         NA         NA
#> 71         NA         NA         NA         NA         NA
#> 72         NA         NA         NA         NA         NA
#> 73         NA   148.3430         NA    49.2718         NA
#> 74   129.7860         NA   148.3430         NA    49.2718
#> 75         NA         NA         NA         NA         NA
#> 76         NA         NA         NA         NA         NA
#> 77         NA         NA         NA         NA         NA
#> 78         NA         NA         NA         NA         NA
#> 79         NA         NA         NA         NA         NA
#> 80         NA         NA         NA         NA         NA
#> 81         NA         NA         NA         NA         NA
#> 82         NA     6.9749         NA    45.4325         NA
#> 83         NA         NA     6.9749         NA    45.4325
#> 84         NA    21.1830         NA   137.9800         NA
#> 85         NA    13.9498    21.1830    90.8651   137.9800
#> 86         NA         NA    13.9498         NA    90.8651
#> 87         NA         NA         NA         NA         NA
#> 88         NA         NA         NA         NA         NA
#> 89   122.5200     7.2332         NA    47.1152         NA
#> 90         NA    74.7785     7.2332    24.8375    47.1152
#>  [ reached 'max' / getOption("max.print") -- omitted 10 rows ]
```
