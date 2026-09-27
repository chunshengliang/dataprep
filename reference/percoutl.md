# Traditional percentile-based outlier removal

Removes values above a top percentile and below a bottom percentile.
Thresholds are computed from quantiles. Optionally, observation deletion
based on consecutive missing values can be performed after outlier
removal.

## Usage

``` r
percoutl(data, cols = NULL, group = NULL, top = 0.995,
         bottom = 0.0025, by = "min", half = 30,
         date_col = NULL, cores = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame or matrix. If a numeric vector is supplied, only outlier
  marking (no observation deletion) is performed because there is no
  time axis.

- cols:

  Column indices or names of numeric variables. If `NULL`, all numeric
  columns are used.

- group:

  Optional grouping column for group-wise outlier removal.

- top:

  Top percentile threshold. Values above this quantile are removed.

- bottom:

  Bottom percentile threshold. Values below this quantile are removed.

- by:

  Time unit for observation deletion (see `obsedele`).

- half:

  Half window size, in minutes, for consecutive missing deletion.

- date_col:

  Time column index or name. If `NULL`, automatically detected.

- cores:

  Number of OpenMP threads. Passed to `obsedele`.

- verbose:

  Logical; if `TRUE`, prints timing and deletion counts.

## Details

This method is a one-size-fits-all approach and may remove non-outliers
or fail to remove some outliers. It is provided for comparison with
[`condextr`](https://chunshengliang.github.io/dataprep/reference/condextr.md),
which uses a point-by-point weighted conditional extremum criterion.

## Value

A data frame with outliers removed.

## References

1\. Example data is from <https://smear.avaa.csc.fi/download>. It
includes particle number concentrations in SMEAR I Varrio forest.

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
percoutl(obsedele(data[1:500, c(1, 4, 17:19)], cols = 3:5, group = 2),
         cols = 3:5, group = 2)
#>                    date    monthyear     3.98     4.47     5.01
#> 1   2020-01-01 00:00:00 January 2020       NA       NA       NA
#> 2   2020-01-01 00:10:00 January 2020       NA       NA       NA
#> 3   2020-01-01 00:20:00 January 2020       NA       NA       NA
#> 4   2020-01-01 00:30:00 January 2020  42.9722  74.7785  24.8375
#> 5   2020-01-01 00:40:00 January 2020       NA   6.8651  44.7176
#> 6   2020-01-01 00:50:00 January 2020  61.2601       NA       NA
#> 7   2020-01-01 01:00:00 January 2020 115.8960       NA       NA
#> 8   2020-01-01 01:10:00 January 2020       NA       NA       NA
#> 9   2020-01-01 01:20:00 January 2020  61.2601       NA       NA
#> 10  2020-01-01 01:30:00 January 2020  60.9405   6.5912  42.9332
#> 11  2020-01-01 01:40:00 January 2020       NA       NA       NA
#> 12  2020-01-01 01:50:00 January 2020       NA       NA       NA
#> 13  2020-01-01 02:00:00 January 2020       NA  13.9498  90.8651
#> 14  2020-01-01 02:10:00 January 2020       NA       NA       NA
#> 15  2020-01-01 02:20:00 January 2020  61.2601       NA       NA
#> 16  2020-01-01 02:30:00 January 2020 130.9630 227.8960  75.6951
#> 17  2020-01-01 02:40:00 January 2020       NA       NA       NA
#> 18  2020-01-01 02:50:00 January 2020       NA       NA       NA
#> 19  2020-01-01 03:00:00 January 2020       NA       NA       NA
#> 20  2020-01-01 03:10:00 January 2020 133.5230 148.2000  49.2241
#> 21  2020-01-01 03:20:00 January 2020  61.2601       NA       NA
#> 22  2020-01-01 03:40:00 January 2020       NA       NA       NA
#> 23  2020-01-01 03:50:00 January 2020       NA       NA       NA
#> 24  2020-01-01 04:00:00 January 2020       NA       NA       NA
#> 25  2020-01-01 04:10:00 January 2020       NA       NA       NA
#> 26  2020-01-01 04:20:00 January 2020  42.8723  74.6047  24.7797
#> 27  2020-01-01 04:30:00 January 2020  42.9722  74.7785  24.8375
#> 28  2020-01-01 04:40:00 January 2020       NA       NA       NA
#> 29  2020-01-01 04:50:00 January 2020       NA   7.2200  47.0292
#> 30  2020-01-01 05:00:00 January 2020       NA       NA       NA
#> 31  2020-01-01 05:10:00 January 2020  96.5514  72.1565  23.9666
#> 32  2020-01-01 05:20:00 January 2020  39.7904  75.6110  64.4859
#> 33  2020-01-01 05:30:00 January 2020  96.7000  72.5003  24.0808
#> 34  2020-01-01 05:40:00 January 2020       NA       NA       NA
#> 35  2020-01-01 05:50:00 January 2020       NA       NA       NA
#> 36  2020-01-01 06:00:00 January 2020       NA       NA       NA
#> 37  2020-01-01 06:10:00 January 2020       NA       NA       NA
#> 38  2020-01-01 06:20:00 January 2020  45.0185  78.3394  26.0202
#> 39  2020-01-01 06:30:00 January 2020  61.2601       NA       NA
#> 40  2020-01-01 06:40:00 January 2020  82.6597 150.5060  91.1891
#> 41  2020-01-01 06:50:00 January 2020       NA       NA       NA
#> 42  2020-01-01 07:00:00 January 2020  61.2601       NA       NA
#> 43  2020-01-01 07:10:00 January 2020       NA       NA       NA
#> 44  2020-01-01 08:10:00 January 2020       NA       NA       NA
#> 45  2020-01-01 08:20:00 January 2020       NA       NA       NA
#> 46  2020-01-01 08:30:00 January 2020       NA       NA       NA
#> 47  2020-01-01 08:40:00 January 2020  35.9966  76.5024 111.1020
#> 48  2020-01-01 08:50:00 January 2020       NA       NA       NA
#> 49  2020-01-01 09:00:00 January 2020       NA       NA       NA
#> 50  2020-01-01 09:10:00 January 2020  61.2601       NA       NA
#> 51  2020-01-01 09:20:00 January 2020  94.8684  76.2325  25.3204
#> 52  2020-01-01 09:30:00 January 2020       NA   6.6070  43.0364
#> 53  2020-01-01 09:40:00 January 2020       NA   6.9749  45.4325
#> 54  2020-01-01 09:50:00 January 2020       NA       NA       NA
#> 55  2020-01-01 11:50:00 January 2020       NA       NA       NA
#> 56  2020-01-01 12:00:00 January 2020       NA       NA       NA
#> 57  2020-01-01 12:10:00 January 2020       NA       NA       NA
#> 58  2020-01-01 12:20:00 January 2020 129.7860 148.3430  49.2718
#> 59  2020-01-01 12:30:00 January 2020       NA       NA       NA
#> 60  2020-01-01 12:40:00 January 2020       NA       NA       NA
#> 61  2020-01-01 12:50:00 January 2020       NA       NA       NA
#> 62  2020-01-01 14:50:00 January 2020       NA  13.9498  90.8651
#> 63  2020-01-01 15:00:00 January 2020       NA       NA       NA
#> 64  2020-01-01 15:10:00 January 2020       NA       NA       NA
#> 65  2020-01-01 15:20:00 January 2020 122.5200       NA       NA
#> 66  2020-01-01 15:30:00 January 2020       NA   7.2332  47.1152
#> 67  2020-01-01 15:40:00 January 2020  42.9722  74.7785  24.8375
#> 68  2020-01-01 15:50:00 January 2020       NA       NA       NA
#> 69  2020-01-01 16:00:00 January 2020       NA       NA       NA
#> 70  2020-01-01 16:10:00 January 2020       NA       NA       NA
#> 71  2020-01-01 18:20:00 January 2020       NA       NA       NA
#> 72  2020-01-01 18:30:00 January 2020       NA       NA       NA
#> 73  2020-01-01 18:40:00 January 2020       NA       NA       NA
#> 74  2020-01-01 18:50:00 January 2020  45.0185  78.3394  26.0202
#> 75  2020-01-01 19:00:00 January 2020       NA       NA       NA
#> 76  2020-01-01 19:10:00 January 2020       NA       NA       NA
#> 77  2020-01-01 19:20:00 January 2020       NA       NA       NA
#> 78  2020-01-01 19:40:00 January 2020       NA       NA       NA
#> 79  2020-01-01 19:50:00 January 2020       NA       NA       NA
#> 80  2020-01-01 20:00:00 January 2020       NA       NA       NA
#> 81  2020-01-01 20:10:00 January 2020  42.4098  73.7999  24.5124
#> 82  2020-01-01 20:20:00 January 2020  45.0185  78.3394  26.0202
#> 83  2020-01-01 20:30:00 January 2020       NA       NA       NA
#> 84  2020-01-01 20:40:00 January 2020       NA   6.8972  44.9267
#> 85  2020-01-01 20:50:00 January 2020       NA       NA       NA
#> 86  2020-01-01 21:50:00 January 2020       NA       NA       NA
#> 87  2020-01-01 22:00:00 January 2020       NA       NA       NA
#> 88  2020-01-01 22:10:00 January 2020       NA       NA       NA
#> 89  2020-01-01 22:20:00 January 2020  60.9227   6.9600  45.3353
#> 90  2020-01-01 22:30:00 January 2020       NA       NA       NA
#> 91  2020-01-01 22:40:00 January 2020       NA   7.2332  47.1152
#> 92  2020-01-01 22:50:00 January 2020       NA       NA       NA
#> 93  2020-01-01 23:50:00 January 2020       NA       NA       NA
#> 94  2020-01-02 00:00:00 January 2020       NA       NA       NA
#> 95  2020-01-02 00:10:00 January 2020  57.1449       NA       NA
#> 96  2020-01-02 00:20:00 January 2020       NA   9.3776  47.8526
#> 97  2020-01-02 00:30:00 January 2020       NA       NA       NA
#> 98  2020-01-02 00:40:00 January 2020       NA       NA       NA
#> 99  2020-01-02 00:50:00 January 2020  46.2837  72.0913  21.9483
#> 100 2020-01-02 01:00:00 January 2020       NA       NA       NA
#> 101 2020-01-02 01:10:00 January 2020       NA       NA       NA
#> 102 2020-01-02 01:20:00 January 2020  42.4445  75.6723  68.9153
#> 103 2020-01-02 01:30:00 January 2020       NA       NA       NA
#> 104 2020-01-02 01:40:00 January 2020       NA       NA       NA
#> 105 2020-01-02 01:50:00 January 2020       NA       NA       NA
#> 106 2020-01-02 04:20:00 January 2020       NA       NA       NA
#> 107 2020-01-02 04:30:00 January 2020       NA       NA       NA
#> 108 2020-01-02 04:40:00 January 2020       NA       NA       NA
#> 109 2020-01-02 04:50:00 January 2020       NA   9.3871  47.9009
#> 110 2020-01-02 05:00:00 January 2020       NA       NA       NA
#> 111 2020-01-02 05:10:00 January 2020  57.1449       NA       NA
#> 112 2020-01-02 05:20:00 January 2020       NA       NA       NA
#> 113 2020-01-02 08:20:00 January 2020       NA       NA       NA
#> 114 2020-01-02 08:30:00 January 2020       NA       NA       NA
#> 115 2020-01-02 08:40:00 January 2020       NA       NA       NA
#> 116 2020-01-02 08:50:00 January 2020  46.2837  72.0913  21.9483
#> 117 2020-01-02 09:00:00 January 2020  49.8677  77.6738  23.6479
#> 118 2020-01-02 09:10:00 January 2020       NA       NA       NA
#> 119 2020-01-02 09:20:00 January 2020       NA       NA       NA
#> 120 2020-01-02 09:30:00 January 2020  58.8257       NA       NA
#> 121 2020-01-02 09:50:00 January 2020       NA       NA       NA
#> 122 2020-01-02 10:00:00 January 2020       NA       NA       NA
#> 123 2020-01-02 10:10:00 January 2020       NA       NA       NA
#> 124 2020-01-02 10:20:00 January 2020  47.4833  73.9599  22.5172
#> 125 2020-01-02 10:30:00 January 2020  94.9316 147.8650  45.0177
#> 126 2020-01-02 10:40:00 January 2020  47.4722  73.9426  22.5119
#> 127 2020-01-02 10:50:00 January 2020       NA   9.4445  48.1940
#> 128 2020-01-02 11:00:00 January 2020       NA   9.1519  46.7010
#> 129 2020-01-02 11:10:00 January 2020       NA   9.4968  48.4608
#> 130 2020-01-02 11:20:00 January 2020  40.0351  81.8495 118.4440
#> 131 2020-01-02 11:30:00 January 2020       NA   9.2470  47.1860
#> 132 2020-01-02 11:40:00 January 2020       NA   9.1802  46.8450
#> 133 2020-01-02 11:50:00 January 2020  44.1765  77.8195  66.9276
#> 134 2020-01-02 12:00:00 January 2020  47.6785  74.2639  22.6097
#> 135 2020-01-02 12:10:00 January 2020       NA       NA       NA
#> 136 2020-01-02 12:20:00 January 2020  88.2793 154.9610 130.9460
#> 137 2020-01-02 12:30:00 January 2020  88.4717 152.8910 118.9460
#> 138 2020-01-02 12:40:00 January 2020 140.8420  86.1568 248.7880
#> 139 2020-01-02 12:50:00 January 2020  58.2736  24.1681 123.3260
#> 140 2020-01-02 13:00:00 January 2020  55.6383 103.1850       NA
#> 141 2020-01-02 13:10:00 January 2020       NA  14.9306  76.1888
#> 142 2020-01-02 13:20:00 January 2020 176.4080 236.2510 197.4570
#> 143 2020-01-02 13:30:00 January 2020  93.7203  78.1097 102.0320
#> 144 2020-01-02 13:40:00 January 2020       NA  25.0583 127.8690
#> 145 2020-01-02 13:50:00 January 2020  41.3755  80.5413 101.7510
#> 146 2020-01-02 14:00:00 January 2020       NA  35.4367 180.8280
#> 147 2020-01-02 14:10:00 January 2020       NA  14.2710  72.8230
#> 148 2020-01-02 14:20:00 January 2020 116.7100  28.6043 145.9640
#> 149 2020-01-02 14:30:00 January 2020       NA  43.7553 223.2770
#> 150 2020-01-02 14:40:00 January 2020 182.1380 227.2330  97.7361
#> 151 2020-01-02 14:50:00 January 2020 125.5730 163.8870 264.9040
#> 152 2020-01-02 15:00:00 January 2020  79.2616  93.9808 308.6480
#> 153 2020-01-02 15:10:00 January 2020  40.7583  81.0403 108.9100
#> 154 2020-01-02 15:20:00 January 2020  33.2681  88.3891 202.3910
#> 155 2020-01-02 15:30:00 January 2020       NA       NA       NA
#> 156 2020-01-02 15:40:00 January 2020       NA       NA       NA
#> 157 2020-01-02 15:50:00 January 2020       NA   9.1804  46.8460
#> 158 2020-01-02 16:00:00 January 2020       NA       NA       NA
#> 159 2020-01-02 16:10:00 January 2020       NA       NA       NA
#> 160 2020-01-02 16:20:00 January 2020  43.9970  77.3754  66.0025
#> 161 2020-01-02 16:30:00 January 2020       NA   9.0166  46.0105
#> 162 2020-01-02 16:40:00 January 2020       NA       NA       NA
#> 163 2020-01-02 16:50:00 January 2020       NA       NA       NA
#> 164 2020-01-02 17:00:00 January 2020       NA       NA       NA
#> 165 2020-01-02 17:10:00 January 2020       NA  19.2452  98.2054
#> 166 2020-01-02 17:20:00 January 2020       NA       NA       NA
#> 167 2020-01-02 17:30:00 January 2020  55.5838       NA       NA
#> 168 2020-01-02 17:40:00 January 2020       NA       NA       NA
#> 169 2020-01-02 17:50:00 January 2020  59.1985       NA       NA
#> 170 2020-01-02 18:00:00 January 2020  47.3995  73.8293  22.4774
#> 171 2020-01-02 18:10:00 January 2020       NA       NA       NA
#> 172 2020-01-02 18:20:00 January 2020       NA       NA       NA
#> 173 2020-01-02 18:30:00 January 2020       NA       NA       NA
#> 174 2020-01-02 21:20:00 January 2020       NA       NA       NA
#> 175 2020-01-02 21:30:00 January 2020       NA       NA       NA
#> 176 2020-01-02 21:40:00 January 2020       NA  20.0681 102.4050
#> 177 2020-01-02 21:50:00 January 2020  59.5532       NA       NA
#> 178 2020-01-02 22:00:00 January 2020       NA       NA       NA
#> 179 2020-01-02 22:10:00 January 2020       NA  10.0338  51.2011
#> 180 2020-01-02 22:20:00 January 2020       NA       NA       NA
#> 181 2020-01-02 23:10:00 January 2020       NA       NA       NA
#> 182 2020-01-02 23:20:00 January 2020       NA       NA       NA
#> 183 2020-01-02 23:30:00 January 2020       NA  10.0328  51.1961
#> 184 2020-01-02 23:40:00 January 2020  47.5941  74.1324  22.5697
#> 185 2020-01-02 23:50:00 January 2020       NA       NA       NA
#> 186 2020-01-03 00:00:00 January 2020       NA       NA       NA
#> 187 2020-01-03 00:10:00 January 2020  97.9560 157.3760  49.1198
#> 188 2020-01-03 00:20:00 January 2020       NA  18.4201 100.1020
#> 189 2020-01-03 00:30:00 January 2020       NA       NA       NA
#> 190 2020-01-03 00:40:00 January 2020       NA       NA       NA
#> 191 2020-01-03 01:30:00 January 2020       NA   9.0296  49.0703
#> 192 2020-01-03 01:40:00 January 2020       NA       NA       NA
#> 193 2020-01-03 01:50:00 January 2020       NA       NA       NA
#> 194 2020-01-03 02:00:00 January 2020  53.3735   9.0840  49.3660
#> 195 2020-01-03 02:10:00 January 2020  47.8390  76.8578  23.9887
#> 196 2020-01-03 02:20:00 January 2020       NA       NA       NA
#> 197 2020-01-03 02:30:00 January 2020       NA       NA       NA
#> 198 2020-01-03 02:40:00 January 2020       NA       NA       NA
#> 199 2020-01-03 02:50:00 January 2020       NA  18.8752 102.5750
#> 200 2020-01-03 03:00:00 January 2020       NA       NA       NA
#>  [ reached 'max' / getOption("max.print") -- omitted 154 rows ]
```
