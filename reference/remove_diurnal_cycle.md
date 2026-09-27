# Remove diurnal cycle

Removes the mean diurnal (or other periodic) cycle by subtracting the
average value for each time period (hour, month, or day). The grouping
is based on a time column.

## Usage

``` r
remove_diurnal_cycle(data, cols = NULL, date_col = NULL,
                     by = "hour", verbose = FALSE)
```

## Arguments

- data:

  A data frame with a time column.

- cols:

  Column indices or names to adjust.

- date_col:

  Time column index or name.

- by:

  Period for cycle removal: `"hour"`, `"month"`, or `"day"`.

- verbose:

  Logical; if `TRUE`, prints progress message.

## Value

A data frame with diurnal cycle removed.

## Examples

``` r
remove_diurnal_cycle(data[1:200, c(1, 4, 17:19)], cols = 3:5, date_col = 1, by = "hour")
#>                    date    monthyear        3.98        4.47        5.01
#> 1   2019-12-31 16:00:00 January 2020          NA          NA          NA
#> 2   2019-12-31 16:10:00 January 2020          NA          NA          NA
#> 3   2019-12-31 16:20:00 January 2020          NA          NA          NA
#> 4   2019-12-31 16:30:00 January 2020  -8.9430250   34.000375 -10.0015000
#> 5   2019-12-31 16:40:00 January 2020          NA  -33.913025   9.8786000
#> 6   2019-12-31 16:50:00 January 2020   9.3448750          NA          NA
#> 7   2019-12-31 17:00:00 January 2020  45.7607250          NA          NA
#> 8   2019-12-31 17:10:00 January 2020          NA          NA          NA
#> 9   2019-12-31 17:20:00 January 2020  -8.8751750          NA          NA
#> 10  2019-12-31 17:30:00 January 2020  -9.1947750  -34.540550 -12.9910500
#> 11  2019-12-31 17:40:00 January 2020          NA          NA          NA
#> 12  2019-12-31 17:50:00 January 2020          NA          NA          NA
#> 13  2019-12-31 18:00:00 January 2020          NA -106.973100   7.5850000
#> 14  2019-12-31 18:10:00 January 2020          NA          NA          NA
#> 15  2019-12-31 18:20:00 January 2020 -21.8625667          NA          NA
#> 16  2019-12-31 18:30:00 January 2020  47.8403333  106.973100  -7.5850000
#> 17  2019-12-31 18:40:00 January 2020          NA          NA          NA
#> 18  2019-12-31 18:50:00 January 2020          NA          NA          NA
#> 19  2019-12-31 19:00:00 January 2020          NA          NA          NA
#> 20  2019-12-31 19:10:00 January 2020  36.1314500    0.000000   0.0000000
#> 21  2019-12-31 19:20:00 January 2020 -36.1314500          NA          NA
#> 22  2019-12-31 19:40:00 January 2020          NA          NA          NA
#> 23  2019-12-31 19:50:00 January 2020          NA          NA          NA
#> 24  2019-12-31 20:00:00 January 2020          NA          NA          NA
#> 25  2019-12-31 20:10:00 January 2020          NA          NA          NA
#> 26  2019-12-31 20:20:00 January 2020 -17.7839333   27.435020  -8.3834400
#> 27  2019-12-31 20:30:00 January 2020 -17.6840333   27.608820  -8.3256400
#> 28  2019-12-31 20:40:00 January 2020          NA          NA          NA
#> 29  2019-12-31 20:50:00 January 2020          NA  -39.949680  13.8660600
#> 30  2019-12-31 21:00:00 January 2020          NA          NA          NA
#> 31  2019-12-31 21:10:00 January 2020  24.0047250   -1.266100 -13.5445000
#> 32  2019-12-31 21:20:00 January 2020 -32.7562750    2.188400  26.9748000
#> 33  2019-12-31 21:30:00 January 2020  24.1533250   -0.922300 -13.4303000
#> 34  2019-12-31 21:40:00 January 2020          NA          NA          NA
#> 35  2019-12-31 21:50:00 January 2020          NA          NA          NA
#> 36  2019-12-31 22:00:00 January 2020          NA          NA          NA
#> 37  2019-12-31 22:10:00 January 2020          NA          NA          NA
#> 38  2019-12-31 22:20:00 January 2020 -17.9609333   -1.071433 -29.0165333
#> 39  2019-12-31 22:30:00 January 2020  -1.7193333          NA          NA
#> 40  2019-12-31 22:40:00 January 2020  19.6802667   71.095167  36.1523667
#> 41  2019-12-31 22:50:00 January 2020          NA          NA          NA
#> 42  2019-12-31 23:00:00 January 2020   1.1599333          NA          NA
#> 43  2019-12-31 23:10:00 January 2020          NA          NA          NA
#> 44  2019-12-31 23:20:00 January 2020  -2.3198667          NA          NA
#> 45  2019-12-31 23:30:00 January 2020          NA          NA          NA
#> 46  2019-12-31 23:40:00 January 2020          NA          NA          NA
#> 47  2019-12-31 23:50:00 January 2020   1.1599333          NA          NA
#> 48  2020-01-01 00:00:00 January 2020          NA          NA          NA
#> 49  2020-01-01 00:10:00 January 2020          NA          NA          NA
#> 50  2020-01-01 00:20:00 January 2020          NA          NA          NA
#> 51  2020-01-01 00:30:00 January 2020          NA          NA          NA
#> 52  2020-01-01 00:40:00 January 2020 -10.4784667    2.205550  44.5768500
#> 53  2020-01-01 00:50:00 January 2020          NA          NA          NA
#> 54  2020-01-01 01:00:00 January 2020          NA          NA          NA
#> 55  2020-01-01 01:10:00 January 2020  -4.9453750          NA          NA
#> 56  2020-01-01 01:20:00 January 2020  28.6629250   34.360450  -9.0389000
#> 57  2020-01-01 01:30:00 January 2020          NA  -35.265050   8.6771000
#> 58  2020-01-01 01:40:00 January 2020          NA  -34.897150  11.0732000
#> 59  2020-01-01 01:50:00 January 2020          NA          NA          NA
#> 60  2020-01-01 02:20:00 January 2020          NA          NA          NA
#> 61  2020-01-01 02:30:00 January 2020          NA          NA          NA
#> 62  2020-01-01 02:40:00 January 2020          NA          NA          NA
#> 63  2020-01-01 02:50:00 January 2020          NA          NA          NA
#> 64  2020-01-01 03:00:00 January 2020          NA          NA          NA
#> 65  2020-01-01 03:10:00 January 2020          NA          NA          NA
#> 66  2020-01-01 03:20:00 January 2020          NA          NA          NA
#> 67  2020-01-01 03:30:00 January 2020   0.0000000          NA          NA
#> 68  2020-01-01 03:40:00 January 2020          NA          NA          NA
#> 69  2020-01-01 03:50:00 January 2020          NA          NA          NA
#> 70  2020-01-01 04:00:00 January 2020          NA          NA          NA
#> 71  2020-01-01 04:10:00 January 2020          NA          NA          NA
#> 72  2020-01-01 04:20:00 January 2020   0.0000000    0.000000   0.0000000
#> 73  2020-01-01 04:30:00 January 2020          NA          NA          NA
#> 74  2020-01-01 04:40:00 January 2020          NA          NA          NA
#> 75  2020-01-01 04:50:00 January 2020          NA          NA          NA
#> 76  2020-01-01 05:00:00 January 2020          NA          NA          NA
#> 77  2020-01-01 05:40:00 January 2020          NA          NA          NA
#> 78  2020-01-01 05:50:00 January 2020          NA          NA          NA
#> 79  2020-01-01 06:00:00 January 2020          NA          NA          NA
#> 80  2020-01-01 06:10:00 January 2020          NA          NA          NA
#> 81  2020-01-01 06:20:00 January 2020          NA   -7.061000 -45.9933667
#> 82  2020-01-01 06:30:00 January 2020          NA          NA          NA
#> 83  2020-01-01 06:40:00 January 2020          NA    7.147100  46.5541333
#> 84  2020-01-01 06:50:00 January 2020          NA   -0.086100  -0.5607667
#> 85  2020-01-01 07:00:00 January 2020          NA          NA          NA
#> 86  2020-01-01 07:10:00 January 2020          NA          NA          NA
#> 87  2020-01-01 07:20:00 January 2020  39.7739000          NA          NA
#> 88  2020-01-01 07:30:00 January 2020          NA  -33.772650  11.1388500
#> 89  2020-01-01 07:40:00 January 2020 -39.7739000   33.772650 -11.1388500
#> 90  2020-01-01 07:50:00 January 2020          NA          NA          NA
#> 91  2020-01-01 08:00:00 January 2020          NA          NA          NA
#> 92  2020-01-01 08:10:00 January 2020          NA          NA          NA
#> 93  2020-01-01 08:20:00 January 2020          NA          NA          NA
#> 94  2020-01-01 08:30:00 January 2020          NA          NA          NA
#> 95  2020-01-01 08:40:00 January 2020          NA          NA          NA
#> 96  2020-01-01 08:50:00 January 2020          NA          NA          NA
#> 97  2020-01-01 09:00:00 January 2020          NA          NA          NA
#> 98  2020-01-01 09:10:00 January 2020          NA          NA          NA
#> 99  2020-01-01 09:20:00 January 2020          NA          NA          NA
#> 100 2020-01-01 09:30:00 January 2020          NA          NA          NA
#> 101 2020-01-01 09:40:00 January 2020          NA          NA          NA
#> 102 2020-01-01 09:50:00 January 2020          NA          NA          NA
#> 103 2020-01-01 10:00:00 January 2020          NA          NA          NA
#> 104 2020-01-01 10:10:00 January 2020          NA          NA          NA
#> 105 2020-01-01 10:20:00 January 2020          NA          NA          NA
#> 106 2020-01-01 10:30:00 January 2020          NA          NA          NA
#> 107 2020-01-01 10:40:00 January 2020          NA          NA          NA
#> 108 2020-01-01 10:50:00 January 2020   0.0000000    0.000000   0.0000000
#> 109 2020-01-01 11:00:00 January 2020          NA          NA          NA
#> 110 2020-01-01 11:10:00 January 2020          NA          NA          NA
#> 111 2020-01-01 11:20:00 January 2020          NA          NA          NA
#> 112 2020-01-01 11:30:00 January 2020          NA          NA          NA
#> 113 2020-01-01 11:40:00 January 2020          NA          NA          NA
#> 114 2020-01-01 11:50:00 January 2020          NA          NA          NA
#> 115 2020-01-01 12:00:00 January 2020          NA          NA          NA
#> 116 2020-01-01 12:10:00 January 2020  -1.3043500   20.787733  -7.3073667
#> 117 2020-01-01 12:20:00 January 2020   1.3043500   25.327233  -5.7995667
#> 118 2020-01-01 12:30:00 January 2020          NA          NA          NA
#> 119 2020-01-01 12:40:00 January 2020          NA  -46.114967  13.1069333
#> 120 2020-01-01 12:50:00 January 2020          NA          NA          NA
#> 121 2020-01-01 13:00:00 January 2020          NA          NA          NA
#> 122 2020-01-01 13:10:00 January 2020          NA          NA          NA
#> 123 2020-01-01 13:20:00 January 2020          NA          NA          NA
#> 124 2020-01-01 13:30:00 January 2020          NA          NA          NA
#> 125 2020-01-01 13:40:00 January 2020          NA          NA          NA
#> 126 2020-01-01 13:50:00 January 2020          NA          NA          NA
#> 127 2020-01-01 14:00:00 January 2020          NA          NA          NA
#> 128 2020-01-01 14:10:00 January 2020          NA          NA          NA
#> 129 2020-01-01 14:20:00 January 2020   0.0000000   -0.136600  -0.8899500
#> 130 2020-01-01 14:30:00 January 2020          NA          NA          NA
#> 131 2020-01-01 14:40:00 January 2020          NA    0.136600   0.8899500
#> 132 2020-01-01 14:50:00 January 2020          NA          NA          NA
#> 133 2020-01-01 15:00:00 January 2020          NA          NA          NA
#> 134 2020-01-01 15:10:00 January 2020          NA          NA          NA
#> 135 2020-01-01 15:20:00 January 2020          NA          NA          NA
#> 136 2020-01-01 15:30:00 January 2020          NA    0.000000   0.0000000
#> 137 2020-01-01 15:40:00 January 2020          NA          NA          NA
#> 138 2020-01-01 15:50:00 January 2020          NA          NA          NA
#> 139 2020-01-01 16:00:00 January 2020          NA          NA          NA
#> 140 2020-01-01 16:10:00 January 2020   5.2296750          NA          NA
#> 141 2020-01-01 16:20:00 January 2020          NA  -31.400525  13.0136000
#> 142 2020-01-01 16:30:00 January 2020          NA          NA          NA
#> 143 2020-01-01 16:40:00 January 2020          NA          NA          NA
#> 144 2020-01-01 16:50:00 January 2020  -5.6315250   31.313175 -12.8907000
#> 145 2020-01-01 17:00:00 January 2020          NA          NA          NA
#> 146 2020-01-01 17:10:00 January 2020          NA          NA          NA
#> 147 2020-01-01 17:20:00 January 2020 -27.6907750   34.540550  12.9910500
#> 148 2020-01-01 17:30:00 January 2020          NA          NA          NA
#> 149 2020-01-01 17:40:00 January 2020          NA          NA          NA
#> 150 2020-01-01 17:50:00 January 2020          NA          NA          NA
#> 151 2020-01-01 18:00:00 January 2020          NA          NA          NA
#> 152 2020-01-01 18:10:00 January 2020          NA          NA          NA
#> 153 2020-01-01 18:20:00 January 2020 -25.9777667          NA          NA
#> 154 2020-01-01 18:30:00 January 2020          NA          NA          NA
#> 155 2020-01-01 18:40:00 January 2020          NA          NA          NA
#> 156 2020-01-01 18:50:00 January 2020          NA          NA          NA
#> 157 2020-01-01 19:00:00 January 2020          NA          NA          NA
#> 158 2020-01-01 19:10:00 January 2020          NA          NA          NA
#> 159 2020-01-01 19:20:00 January 2020          NA          NA          NA
#> 160 2020-01-01 19:30:00 January 2020          NA          NA          NA
#> 161 2020-01-01 19:40:00 January 2020          NA          NA          NA
#> 162 2020-01-01 19:50:00 January 2020          NA          NA          NA
#> 163 2020-01-01 20:00:00 January 2020  35.4679667   22.688420 -11.8947400
#> 164 2020-01-01 20:10:00 January 2020          NA          NA          NA
#> 165 2020-01-01 20:20:00 January 2020          NA          NA          NA
#> 166 2020-01-01 20:30:00 January 2020          NA          NA          NA
#> 167 2020-01-01 20:40:00 January 2020          NA          NA          NA
#> 168 2020-01-01 20:50:00 January 2020          NA  -37.782580  14.7377600
#> 169 2020-01-01 21:00:00 January 2020          NA          NA          NA
#> 170 2020-01-01 21:10:00 January 2020 -15.4017750          NA          NA
#> 171 2020-01-01 21:20:00 January 2020          NA          NA          NA
#> 172 2020-01-01 21:30:00 January 2020          NA          NA          NA
#> 173 2020-01-01 21:40:00 January 2020          NA          NA          NA
#> 174 2020-01-01 21:50:00 January 2020          NA          NA          NA
#> 175 2020-01-01 22:00:00 January 2020          NA  -70.023733  -7.1358333
#> 176 2020-01-01 22:10:00 January 2020          NA          NA          NA
#> 177 2020-01-01 22:20:00 January 2020          NA          NA          NA
#> 178 2020-01-01 22:30:00 January 2020          NA          NA          NA
#> 179 2020-01-01 22:40:00 January 2020          NA          NA          NA
#> 180 2020-01-01 22:50:00 January 2020          NA          NA          NA
#> 181 2020-01-01 23:00:00 January 2020          NA          NA          NA
#> 182 2020-01-01 23:10:00 January 2020          NA          NA          NA
#> 183 2020-01-01 23:20:00 January 2020          NA          NA          NA
#> 184 2020-01-01 23:30:00 January 2020          NA          NA          NA
#> 185 2020-01-01 23:40:00 January 2020          NA          NA          NA
#> 186 2020-01-01 23:50:00 January 2020          NA          NA          NA
#> 187 2020-01-02 00:00:00 January 2020  10.6698333          NA          NA
#> 188 2020-01-02 00:10:00 January 2020          NA          NA          NA
#> 189 2020-01-02 00:20:00 January 2020          NA          NA          NA
#> 190 2020-01-02 00:30:00 January 2020          NA          NA          NA
#> 191 2020-01-02 00:40:00 January 2020          NA          NA          NA
#> 192 2020-01-02 00:50:00 January 2020  -0.1913667   -2.205550 -44.5768500
#> 193 2020-01-02 01:00:00 January 2020 -16.3377750   35.801750 -10.7114000
#> 194 2020-01-02 01:10:00 January 2020          NA          NA          NA
#> 195 2020-01-02 01:20:00 January 2020          NA          NA          NA
#> 196 2020-01-02 01:30:00 January 2020  -7.3797750          NA          NA
#> 197 2020-01-02 01:40:00 January 2020          NA          NA          NA
#> 198 2020-01-02 01:50:00 January 2020          NA          NA          NA
#> 199 2020-01-02 02:00:00 January 2020          NA          NA          NA
#> 200 2020-01-02 02:10:00 January 2020          NA          NA          NA
```
