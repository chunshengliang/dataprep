# Simple time series decomposition

Decomposes a time series into trend, seasonal, and residual components
using simple moving averages and group means. The function returns the
residual component (or the original series with trend removed) to
simplify analysis.

## Usage

``` r
decompose_ts(data, cols = NULL, date_col = NULL,
             period = "month", method = "additive", verbose = FALSE)
```

## Arguments

- data:

  A data frame with a time column.

- cols:

  Column indices or names to decompose.

- date_col:

  Time column index or name.

- period:

  Seasonal period: `"month"`, `"day"`, or `"hour"`.

- method:

  Decomposition method: `"additive"` or `"multiplicative"`.

- verbose:

  Logical; if `TRUE`, prints progress message.

## Value

A data frame with the residual component.

## Examples

``` r
decompose_ts(data[1:500, c(1, 4, 17:19)], cols = 3:5, date_col = 1, period = "month")
#>                    date    monthyear       3.98        4.47       5.01
#> 1   2020-01-01 00:00:00 January 2020         NA          NA         NA
#> 2   2020-01-01 00:10:00 January 2020         NA          NA         NA
#> 3   2020-01-01 00:20:00 January 2020         NA          NA         NA
#> 4   2020-01-01 00:30:00 January 2020  -77.04993  -68.197898  -89.01769
#> 5   2020-01-01 00:40:00 January 2020         NA -102.154598  -79.07764
#> 6   2020-01-01 00:50:00 January 2020  -67.90598          NA         NA
#> 7   2020-01-01 01:00:00 January 2020  -34.53003          NA         NA
#> 8   2020-01-01 01:10:00 January 2020         NA          NA         NA
#> 9   2020-01-01 01:20:00 January 2020  -86.13693          NA         NA
#> 10  2020-01-01 01:30:00 January 2020  -84.57521  -91.018298  -83.58059
#> 11  2020-01-01 01:40:00 January 2020         NA          NA         NA
#> 12  2020-01-01 01:50:00 January 2020         NA          NA         NA
#> 13  2020-01-01 02:00:00 January 2020         NA  -79.794248  -48.99094
#> 14  2020-01-01 02:10:00 January 2020         NA          NA         NA
#> 15  2020-01-01 02:20:00 January 2020  -83.05467          NA         NA
#> 16  2020-01-01 02:30:00 January 2020  -28.01690   95.872577  -76.87534
#> 17  2020-01-01 02:40:00 January 2020         NA          NA         NA
#> 18  2020-01-01 02:50:00 January 2020         NA          NA         NA
#> 19  2020-01-01 03:00:00 January 2020         NA          NA         NA
#> 20  2020-01-01 03:10:00 January 2020  -33.11627  -19.157148 -104.47297
#> 21  2020-01-01 03:20:00 January 2020 -105.37917          NA         NA
#> 22  2020-01-01 03:40:00 January 2020         NA          NA         NA
#> 23  2020-01-01 03:50:00 January 2020         NA          NA         NA
#> 24  2020-01-01 04:00:00 January 2020         NA          NA         NA
#> 25  2020-01-01 04:10:00 January 2020         NA          NA         NA
#> 26  2020-01-01 04:20:00 January 2020 -120.15333 -143.826765 -114.13763
#> 27  2020-01-01 04:30:00 January 2020 -116.39585 -124.789198 -107.81429
#> 28  2020-01-01 04:40:00 January 2020         NA          NA         NA
#> 29  2020-01-01 04:50:00 January 2020         NA -137.178698  -78.45612
#> 30  2020-01-01 05:00:00 January 2020         NA          NA         NA
#> 31  2020-01-01 05:10:00 January 2020  -55.93433  -71.433338  -99.01851
#> 32  2020-01-01 05:20:00 January 2020  -93.94881  -53.461038  -61.55157
#> 33  2020-01-01 05:30:00 January 2020  -44.12719  -58.509432  -99.80018
#> 34  2020-01-01 05:40:00 January 2020         NA          NA         NA
#> 35  2020-01-01 05:50:00 January 2020         NA          NA         NA
#> 36  2020-01-01 06:00:00 January 2020         NA          NA         NA
#> 37  2020-01-01 06:10:00 January 2020         NA          NA         NA
#> 38  2020-01-01 06:20:00 January 2020  -96.23793  -53.292782  -98.06753
#> 39  2020-01-01 06:30:00 January 2020  -83.65391          NA         NA
#> 40  2020-01-01 06:40:00 January 2020  -64.72025    6.252568  -43.95723
#> 41  2020-01-01 06:50:00 January 2020         NA          NA         NA
#> 42  2020-01-01 07:00:00 January 2020  -84.82415          NA         NA
#> 43  2020-01-01 07:10:00 January 2020         NA          NA         NA
#> 44  2020-01-01 07:20:00 January 2020  -86.71608          NA         NA
#> 45  2020-01-01 07:30:00 January 2020         NA          NA         NA
#> 46  2020-01-01 07:40:00 January 2020         NA          NA         NA
#> 47  2020-01-01 07:50:00 January 2020  -77.32963          NA         NA
#> 48  2020-01-01 08:00:00 January 2020         NA          NA         NA
#> 49  2020-01-01 08:10:00 January 2020         NA          NA         NA
#> 50  2020-01-01 08:20:00 January 2020         NA          NA         NA
#> 51  2020-01-01 08:30:00 January 2020         NA          NA         NA
#> 52  2020-01-01 08:40:00 January 2020  -95.12761  -68.197898  -89.01769
#> 53  2020-01-01 08:50:00 January 2020         NA          NA         NA
#> 54  2020-01-01 09:00:00 January 2020         NA          NA         NA
#> 55  2020-01-01 09:10:00 January 2020  -69.86411          NA         NA
#> 56  2020-01-01 09:20:00 January 2020  -45.52783  -68.332848 -131.90849
#> 57  2020-01-01 09:30:00 January 2020         NA -114.704865 -105.80089
#> 58  2020-01-01 09:40:00 January 2020         NA -102.802198  -99.80802
#> 59  2020-01-01 09:50:00 January 2020         NA          NA         NA
#> 60  2020-01-01 10:20:00 January 2020         NA          NA         NA
#> 61  2020-01-01 10:30:00 January 2020         NA          NA         NA
#> 62  2020-01-01 10:40:00 January 2020         NA          NA         NA
#> 63  2020-01-01 10:50:00 January 2020         NA          NA         NA
#> 64  2020-01-01 11:00:00 January 2020         NA          NA         NA
#> 65  2020-01-01 11:10:00 January 2020         NA          NA         NA
#> 66  2020-01-01 11:20:00 January 2020         NA          NA         NA
#> 67  2020-01-01 11:30:00 January 2020  -63.22413          NA         NA
#> 68  2020-01-01 11:40:00 January 2020         NA          NA         NA
#> 69  2020-01-01 11:50:00 January 2020         NA          NA         NA
#> 70  2020-01-01 12:00:00 January 2020         NA          NA         NA
#> 71  2020-01-01 12:10:00 January 2020         NA          NA         NA
#> 72  2020-01-01 12:20:00 January 2020  -73.41693  -68.197898  -89.01769
#> 73  2020-01-01 12:30:00 January 2020         NA          NA         NA
#> 74  2020-01-01 12:40:00 January 2020         NA          NA         NA
#> 75  2020-01-01 12:50:00 January 2020         NA          NA         NA
#> 76  2020-01-01 13:00:00 January 2020         NA          NA         NA
#> 77  2020-01-01 13:40:00 January 2020         NA          NA         NA
#> 78  2020-01-01 13:50:00 January 2020         NA          NA         NA
#> 79  2020-01-01 14:00:00 January 2020         NA          NA         NA
#> 80  2020-01-01 14:10:00 January 2020         NA          NA         NA
#> 81  2020-01-01 14:20:00 January 2020         NA -138.881948  -90.93734
#> 82  2020-01-01 14:30:00 January 2020         NA          NA         NA
#> 83  2020-01-01 14:40:00 January 2020         NA -105.848532  -28.59913
#> 84  2020-01-01 14:50:00 January 2020         NA  -68.283998  -89.57846
#> 85  2020-01-01 15:00:00 January 2020         NA          NA         NA
#> 86  2020-01-01 15:10:00 January 2020         NA          NA         NA
#> 87  2020-01-01 15:20:00 January 2020  -77.04993          NA         NA
#> 88  2020-01-01 15:30:00 January 2020         NA  -73.299923 -122.25069
#> 89  2020-01-01 15:40:00 January 2020 -116.82383  -18.243278 -133.42625
#> 90  2020-01-01 15:50:00 January 2020         NA          NA         NA
#> 91  2020-01-01 16:00:00 January 2020         NA          NA         NA
#> 92  2020-01-01 16:10:00 January 2020         NA          NA         NA
#> 93  2020-01-01 16:20:00 January 2020         NA          NA         NA
#> 94  2020-01-01 16:30:00 January 2020         NA          NA         NA
#> 95  2020-01-01 16:40:00 January 2020         NA          NA         NA
#> 96  2020-01-01 16:50:00 January 2020         NA          NA         NA
#> 97  2020-01-01 17:00:00 January 2020         NA          NA         NA
#> 98  2020-01-01 17:10:00 January 2020         NA          NA         NA
#> 99  2020-01-01 17:20:00 January 2020         NA          NA         NA
#> 100 2020-01-01 17:30:00 January 2020         NA          NA         NA
#> 101 2020-01-01 17:40:00 January 2020         NA          NA         NA
#> 102 2020-01-01 17:50:00 January 2020         NA          NA         NA
#> 103 2020-01-01 18:00:00 January 2020         NA          NA         NA
#> 104 2020-01-01 18:10:00 January 2020         NA          NA         NA
#> 105 2020-01-01 18:20:00 January 2020         NA          NA         NA
#> 106 2020-01-01 18:30:00 January 2020         NA          NA         NA
#> 107 2020-01-01 18:40:00 January 2020         NA          NA         NA
#> 108 2020-01-01 18:50:00 January 2020  -77.04993  -68.197898  -89.01769
#> 109 2020-01-01 19:00:00 January 2020         NA          NA         NA
#> 110 2020-01-01 19:10:00 January 2020         NA          NA         NA
#> 111 2020-01-01 19:20:00 January 2020         NA          NA         NA
#> 112 2020-01-01 19:30:00 January 2020         NA          NA         NA
#> 113 2020-01-01 19:40:00 January 2020         NA          NA         NA
#> 114 2020-01-01 19:50:00 January 2020         NA          NA         NA
#> 115 2020-01-01 20:00:00 January 2020         NA          NA         NA
#> 116 2020-01-01 20:10:00 January 2020  -78.35428  -70.467648  -89.77159
#> 117 2020-01-01 20:20:00 January 2020  -76.18037  -66.684732  -88.51509
#> 118 2020-01-01 20:30:00 January 2020         NA          NA         NA
#> 119 2020-01-01 20:40:00 January 2020         NA -120.644673  -74.46087
#> 120 2020-01-01 20:50:00 January 2020         NA          NA         NA
#> 121 2020-01-01 21:00:00 January 2020         NA          NA         NA
#> 122 2020-01-01 21:10:00 January 2020         NA          NA         NA
#> 123 2020-01-01 21:20:00 January 2020         NA          NA         NA
#> 124 2020-01-01 21:30:00 January 2020         NA          NA         NA
#> 125 2020-01-01 21:40:00 January 2020         NA          NA         NA
#> 126 2020-01-01 21:50:00 January 2020         NA          NA         NA
#> 127 2020-01-01 22:00:00 January 2020         NA          NA         NA
#> 128 2020-01-01 22:10:00 January 2020         NA          NA         NA
#> 129 2020-01-01 22:20:00 January 2020  -77.04993  -68.166498  -88.81339
#> 130 2020-01-01 22:30:00 January 2020         NA          NA         NA
#> 131 2020-01-01 22:40:00 January 2020         NA  -68.061298  -88.12774
#> 132 2020-01-01 22:50:00 January 2020         NA          NA         NA
#> 133 2020-01-01 23:00:00 January 2020         NA          NA         NA
#> 134 2020-01-01 23:10:00 January 2020         NA          NA         NA
#> 135 2020-01-01 23:20:00 January 2020         NA          NA         NA
#> 136 2020-01-01 23:30:00 January 2020         NA  -68.300832  -89.68799
#> 137 2020-01-01 23:40:00 January 2020         NA          NA         NA
#> 138 2020-01-01 23:50:00 January 2020         NA          NA         NA
#> 139 2020-01-02 00:00:00 January 2020         NA          NA         NA
#> 140 2020-01-02 00:10:00 January 2020  -78.93883          NA         NA
#> 141 2020-01-02 00:20:00 January 2020         NA  -66.671298  -87.89429
#> 142 2020-01-02 00:30:00 January 2020         NA          NA         NA
#> 143 2020-01-02 00:40:00 January 2020         NA          NA         NA
#> 144 2020-01-02 00:50:00 January 2020  -82.48053  -25.576965 -105.40963
#> 145 2020-01-02 01:00:00 January 2020         NA          NA         NA
#> 146 2020-01-02 01:10:00 January 2020         NA          NA         NA
#> 147 2020-01-02 01:20:00 January 2020  -83.22980  -33.546448  -66.08639
#> 148 2020-01-02 01:30:00 January 2020         NA          NA         NA
#> 149 2020-01-02 01:40:00 January 2020         NA          NA         NA
#> 150 2020-01-02 01:50:00 January 2020         NA          NA         NA
#> 151 2020-01-02 02:00:00 January 2020         NA          NA         NA
#> 152 2020-01-02 02:10:00 January 2020         NA          NA         NA
#> 153 2020-01-02 02:20:00 January 2020  -68.52940          NA         NA
#> 154 2020-01-02 02:30:00 January 2020         NA          NA         NA
#> 155 2020-01-02 02:40:00 January 2020         NA          NA         NA
#> 156 2020-01-02 02:50:00 January 2020         NA          NA         NA
#> 157 2020-01-02 03:00:00 January 2020         NA          NA         NA
#> 158 2020-01-02 03:10:00 January 2020         NA          NA         NA
#> 159 2020-01-02 03:20:00 January 2020         NA          NA         NA
#> 160 2020-01-02 03:30:00 January 2020         NA          NA         NA
#> 161 2020-01-02 03:40:00 January 2020         NA          NA         NA
#> 162 2020-01-02 03:50:00 January 2020         NA          NA         NA
#> 163 2020-01-02 04:00:00 January 2020  -57.56028  -68.197898  -89.01769
#> 164 2020-01-02 04:10:00 January 2020         NA          NA         NA
#> 165 2020-01-02 04:20:00 January 2020         NA          NA         NA
#> 166 2020-01-02 04:30:00 January 2020         NA          NA         NA
#> 167 2020-01-02 04:40:00 January 2020         NA          NA         NA
#> 168 2020-01-02 04:50:00 January 2020         NA  -98.433398  -75.70144
#> 169 2020-01-02 05:00:00 January 2020         NA          NA         NA
#> 170 2020-01-02 05:10:00 January 2020  -96.53958          NA         NA
#> 171 2020-01-02 05:20:00 January 2020         NA          NA         NA
#> 172 2020-01-02 05:30:00 January 2020         NA          NA         NA
#> 173 2020-01-02 05:40:00 January 2020         NA          NA         NA
#> 174 2020-01-02 05:50:00 January 2020         NA          NA         NA
#> 175 2020-01-02 06:00:00 January 2020         NA  -68.197898  -89.01769
#> 176 2020-01-02 06:10:00 January 2020         NA          NA         NA
#> 177 2020-01-02 06:20:00 January 2020         NA          NA         NA
#> 178 2020-01-02 06:30:00 January 2020         NA          NA         NA
#> 179 2020-01-02 06:40:00 January 2020         NA          NA         NA
#> 180 2020-01-02 06:50:00 January 2020         NA          NA         NA
#> 181 2020-01-02 07:00:00 January 2020         NA          NA         NA
#> 182 2020-01-02 07:10:00 January 2020         NA          NA         NA
#> 183 2020-01-02 07:20:00 January 2020         NA          NA         NA
#> 184 2020-01-02 07:30:00 January 2020         NA          NA         NA
#> 185 2020-01-02 07:40:00 January 2020         NA          NA         NA
#> 186 2020-01-02 07:50:00 January 2020         NA          NA         NA
#> 187 2020-01-02 08:00:00 January 2020  -77.04993          NA         NA
#> 188 2020-01-02 08:10:00 January 2020         NA          NA         NA
#> 189 2020-01-02 08:20:00 January 2020         NA          NA         NA
#> 190 2020-01-02 08:30:00 January 2020         NA          NA         NA
#> 191 2020-01-02 08:40:00 January 2020         NA          NA         NA
#> 192 2020-01-02 08:50:00 January 2020  -82.48053  -68.197898  -89.01769
#> 193 2020-01-02 09:00:00 January 2020  -78.28100  -65.406648  -88.16789
#> 194 2020-01-02 09:10:00 January 2020         NA          NA         NA
#> 195 2020-01-02 09:20:00 January 2020         NA          NA         NA
#> 196 2020-01-02 09:30:00 January 2020  -71.25473          NA         NA
#> 197 2020-01-02 09:40:00 January 2020         NA          NA         NA
#> 198 2020-01-02 09:50:00 January 2020         NA          NA         NA
#> 199 2020-01-02 10:00:00 January 2020         NA          NA         NA
#> 200 2020-01-02 10:10:00 January 2020         NA          NA         NA
#>  [ reached 'max' / getOption("max.print") -- omitted 300 rows ]
```
