# Logarithmic returns for financial time series

Computes the logarithmic return \\\log(x_t / x\_{t-1})\\ for selected
numeric columns. Used extensively in financial time series analysis.
Group-wise computation is supported.

## Usage

``` r
log_returns(data, cols = NULL, group = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame, matrix, or numeric vector.

- cols:

  Column indices or names of numeric variables. If `NULL`, all numeric
  columns are used.

- group:

  Optional grouping column for group-specific returns.

- verbose:

  Logical; if `TRUE`, prints progress message.

## Details

The function only computes a return when both the current and previous
values are positive. Otherwise, the result is `NA`.

## Value

A vector or data frame with logarithmic returns. The first observation
of each group (or the vector) is `NA`.

## Examples

``` r
log_returns(data[1:200, c(1, 4, 17:19)], cols = 3:5)
#>                    date    monthyear         3.98         4.47         5.01
#> 1   2020-01-01 00:00:00 January 2020           NA           NA           NA
#> 2   2020-01-01 00:10:00 January 2020           NA           NA           NA
#> 3   2020-01-01 00:20:00 January 2020           NA           NA           NA
#> 4   2020-01-01 00:30:00 January 2020           NA           NA           NA
#> 5   2020-01-01 00:40:00 January 2020           NA -2.388079805  0.588012552
#> 6   2020-01-01 00:50:00 January 2020           NA           NA           NA
#> 7   2020-01-01 01:00:00 January 2020  0.637564503           NA           NA
#> 8   2020-01-01 01:10:00 January 2020           NA           NA           NA
#> 9   2020-01-01 01:20:00 January 2020           NA           NA           NA
#> 10  2020-01-01 01:30:00 January 2020 -0.005230755           NA           NA
#> 11  2020-01-01 01:40:00 January 2020           NA           NA           NA
#> 12  2020-01-01 01:50:00 January 2020           NA           NA           NA
#> 13  2020-01-01 02:00:00 January 2020           NA           NA           NA
#> 14  2020-01-01 02:10:00 January 2020           NA           NA           NA
#> 15  2020-01-01 02:20:00 January 2020           NA           NA           NA
#> 16  2020-01-01 02:30:00 January 2020  0.759786107           NA           NA
#> 17  2020-01-01 02:40:00 January 2020           NA           NA           NA
#> 18  2020-01-01 02:50:00 January 2020           NA           NA           NA
#> 19  2020-01-01 03:00:00 January 2020           NA           NA           NA
#> 20  2020-01-01 03:10:00 January 2020           NA           NA           NA
#> 21  2020-01-01 03:20:00 January 2020 -0.779145014           NA           NA
#> 22  2020-01-01 03:40:00 January 2020           NA           NA           NA
#> 23  2020-01-01 03:50:00 January 2020           NA           NA           NA
#> 24  2020-01-01 04:00:00 January 2020           NA           NA           NA
#> 25  2020-01-01 04:10:00 January 2020           NA           NA           NA
#> 26  2020-01-01 04:20:00 January 2020           NA           NA           NA
#> 27  2020-01-01 04:30:00 January 2020  0.002327465  0.002326903  0.002329838
#> 28  2020-01-01 04:40:00 January 2020           NA           NA           NA
#> 29  2020-01-01 04:50:00 January 2020           NA           NA           NA
#> 30  2020-01-01 05:00:00 January 2020           NA           NA           NA
#> 31  2020-01-01 05:10:00 January 2020           NA           NA           NA
#> 32  2020-01-01 05:20:00 January 2020 -0.886449832  0.046764404  0.989785401
#> 33  2020-01-01 05:30:00 January 2020  0.887987725 -0.042011075 -0.985031753
#> 34  2020-01-01 05:40:00 January 2020           NA           NA           NA
#> 35  2020-01-01 05:50:00 January 2020           NA           NA           NA
#> 36  2020-01-01 06:00:00 January 2020           NA           NA           NA
#> 37  2020-01-01 06:10:00 January 2020           NA           NA           NA
#> 38  2020-01-01 06:20:00 January 2020           NA           NA           NA
#> 39  2020-01-01 06:30:00 January 2020  0.308055217           NA           NA
#> 40  2020-01-01 06:40:00 January 2020  0.299603446           NA           NA
#> 41  2020-01-01 06:50:00 January 2020           NA           NA           NA
#> 42  2020-01-01 07:00:00 January 2020           NA           NA           NA
#> 43  2020-01-01 07:10:00 January 2020           NA           NA           NA
#> 44  2020-01-01 07:20:00 January 2020           NA           NA           NA
#> 45  2020-01-01 07:30:00 January 2020           NA           NA           NA
#> 46  2020-01-01 07:40:00 January 2020           NA           NA           NA
#> 47  2020-01-01 07:50:00 January 2020           NA           NA           NA
#> 48  2020-01-01 08:00:00 January 2020           NA           NA           NA
#> 49  2020-01-01 08:10:00 January 2020           NA           NA           NA
#> 50  2020-01-01 08:20:00 January 2020           NA           NA           NA
#> 51  2020-01-01 08:30:00 January 2020           NA           NA           NA
#> 52  2020-01-01 08:40:00 January 2020           NA           NA           NA
#> 53  2020-01-01 08:50:00 January 2020           NA           NA           NA
#> 54  2020-01-01 09:00:00 January 2020           NA           NA           NA
#> 55  2020-01-01 09:10:00 January 2020           NA           NA           NA
#> 56  2020-01-01 09:20:00 January 2020  0.437361934           NA           NA
#> 57  2020-01-01 09:30:00 January 2020           NA -2.445658188  0.530435874
#> 58  2020-01-01 09:40:00 January 2020           NA  0.054188298  0.054181439
#> 59  2020-01-01 09:50:00 January 2020           NA           NA           NA
#> 60  2020-01-01 10:20:00 January 2020           NA           NA           NA
#> 61  2020-01-01 10:30:00 January 2020           NA           NA           NA
#> 62  2020-01-01 10:40:00 January 2020           NA           NA           NA
#> 63  2020-01-01 10:50:00 January 2020           NA           NA           NA
#> 64  2020-01-01 11:00:00 January 2020           NA           NA           NA
#> 65  2020-01-01 11:10:00 January 2020           NA           NA           NA
#> 66  2020-01-01 11:20:00 January 2020           NA           NA           NA
#> 67  2020-01-01 11:30:00 January 2020           NA           NA           NA
#> 68  2020-01-01 11:40:00 January 2020           NA           NA           NA
#> 69  2020-01-01 11:50:00 January 2020           NA           NA           NA
#> 70  2020-01-01 12:00:00 January 2020           NA           NA           NA
#> 71  2020-01-01 12:10:00 January 2020           NA           NA           NA
#> 72  2020-01-01 12:20:00 January 2020           NA           NA           NA
#> 73  2020-01-01 12:30:00 January 2020           NA           NA           NA
#> 74  2020-01-01 12:40:00 January 2020           NA           NA           NA
#> 75  2020-01-01 12:50:00 January 2020           NA           NA           NA
#> 76  2020-01-01 13:00:00 January 2020           NA           NA           NA
#> 77  2020-01-01 13:40:00 January 2020           NA           NA           NA
#> 78  2020-01-01 13:50:00 January 2020           NA           NA           NA
#> 79  2020-01-01 14:00:00 January 2020           NA           NA           NA
#> 80  2020-01-01 14:10:00 January 2020           NA           NA           NA
#> 81  2020-01-01 14:20:00 January 2020           NA           NA           NA
#> 82  2020-01-01 14:30:00 January 2020           NA           NA           NA
#> 83  2020-01-01 14:40:00 January 2020           NA           NA           NA
#> 84  2020-01-01 14:50:00 January 2020           NA -0.417733802 -0.417732758
#> 85  2020-01-01 15:00:00 January 2020           NA           NA           NA
#> 86  2020-01-01 15:10:00 January 2020           NA           NA           NA
#> 87  2020-01-01 15:20:00 January 2020           NA           NA           NA
#> 88  2020-01-01 15:30:00 January 2020           NA           NA           NA
#> 89  2020-01-01 15:40:00 January 2020           NA  2.335848872 -0.640241059
#> 90  2020-01-01 15:50:00 January 2020           NA           NA           NA
#> 91  2020-01-01 16:00:00 January 2020           NA           NA           NA
#> 92  2020-01-01 16:10:00 January 2020           NA           NA           NA
#> 93  2020-01-01 16:20:00 January 2020           NA           NA           NA
#> 94  2020-01-01 16:30:00 January 2020           NA           NA           NA
#> 95  2020-01-01 16:40:00 January 2020           NA           NA           NA
#> 96  2020-01-01 16:50:00 January 2020           NA           NA           NA
#> 97  2020-01-01 17:00:00 January 2020           NA           NA           NA
#> 98  2020-01-01 17:10:00 January 2020           NA           NA           NA
#> 99  2020-01-01 17:20:00 January 2020           NA           NA           NA
#> 100 2020-01-01 17:30:00 January 2020           NA           NA           NA
#> 101 2020-01-01 17:40:00 January 2020           NA           NA           NA
#> 102 2020-01-01 17:50:00 January 2020           NA           NA           NA
#> 103 2020-01-01 18:00:00 January 2020           NA           NA           NA
#> 104 2020-01-01 18:10:00 January 2020           NA           NA           NA
#> 105 2020-01-01 18:20:00 January 2020           NA           NA           NA
#> 106 2020-01-01 18:30:00 January 2020           NA           NA           NA
#> 107 2020-01-01 18:40:00 January 2020           NA           NA           NA
#> 108 2020-01-01 18:50:00 January 2020           NA           NA           NA
#> 109 2020-01-01 19:00:00 January 2020           NA           NA           NA
#> 110 2020-01-01 19:10:00 January 2020           NA           NA           NA
#> 111 2020-01-01 19:20:00 January 2020           NA           NA           NA
#> 112 2020-01-01 19:30:00 January 2020           NA           NA           NA
#> 113 2020-01-01 19:40:00 January 2020           NA           NA           NA
#> 114 2020-01-01 19:50:00 January 2020           NA           NA           NA
#> 115 2020-01-01 20:00:00 January 2020           NA           NA           NA
#> 116 2020-01-01 20:10:00 January 2020           NA           NA           NA
#> 117 2020-01-01 20:20:00 January 2020  0.059694049  0.059693293  0.059694047
#> 118 2020-01-01 20:30:00 January 2020           NA           NA           NA
#> 119 2020-01-01 20:40:00 January 2020           NA           NA           NA
#> 120 2020-01-01 20:50:00 January 2020           NA           NA           NA
#> 121 2020-01-01 21:00:00 January 2020           NA           NA           NA
#> 122 2020-01-01 21:10:00 January 2020           NA           NA           NA
#> 123 2020-01-01 21:20:00 January 2020           NA           NA           NA
#> 124 2020-01-01 21:30:00 January 2020           NA           NA           NA
#> 125 2020-01-01 21:40:00 January 2020           NA           NA           NA
#> 126 2020-01-01 21:50:00 January 2020           NA           NA           NA
#> 127 2020-01-01 22:00:00 January 2020           NA           NA           NA
#> 128 2020-01-01 22:10:00 January 2020           NA           NA           NA
#> 129 2020-01-01 22:20:00 January 2020           NA           NA           NA
#> 130 2020-01-01 22:30:00 January 2020           NA           NA           NA
#> 131 2020-01-01 22:40:00 January 2020           NA           NA           NA
#> 132 2020-01-01 22:50:00 January 2020           NA           NA           NA
#> 133 2020-01-01 23:00:00 January 2020           NA           NA           NA
#> 134 2020-01-01 23:10:00 January 2020           NA           NA           NA
#> 135 2020-01-01 23:20:00 January 2020           NA           NA           NA
#> 136 2020-01-01 23:30:00 January 2020           NA           NA           NA
#> 137 2020-01-01 23:40:00 January 2020           NA           NA           NA
#> 138 2020-01-01 23:50:00 January 2020           NA           NA           NA
#> 139 2020-01-02 00:00:00 January 2020           NA           NA           NA
#> 140 2020-01-02 00:10:00 January 2020           NA           NA           NA
#> 141 2020-01-02 00:20:00 January 2020           NA           NA           NA
#> 142 2020-01-02 00:30:00 January 2020           NA           NA           NA
#> 143 2020-01-02 00:40:00 January 2020           NA           NA           NA
#> 144 2020-01-02 00:50:00 January 2020           NA           NA           NA
#> 145 2020-01-02 01:00:00 January 2020           NA           NA           NA
#> 146 2020-01-02 01:10:00 January 2020           NA           NA           NA
#> 147 2020-01-02 01:20:00 January 2020           NA           NA           NA
#> 148 2020-01-02 01:30:00 January 2020           NA           NA           NA
#> 149 2020-01-02 01:40:00 January 2020           NA           NA           NA
#> 150 2020-01-02 01:50:00 January 2020           NA           NA           NA
#> 151 2020-01-02 02:00:00 January 2020           NA           NA           NA
#> 152 2020-01-02 02:10:00 January 2020           NA           NA           NA
#> 153 2020-01-02 02:20:00 January 2020           NA           NA           NA
#> 154 2020-01-02 02:30:00 January 2020           NA           NA           NA
#> 155 2020-01-02 02:40:00 January 2020           NA           NA           NA
#> 156 2020-01-02 02:50:00 January 2020           NA           NA           NA
#> 157 2020-01-02 03:00:00 January 2020           NA           NA           NA
#> 158 2020-01-02 03:10:00 January 2020           NA           NA           NA
#> 159 2020-01-02 03:20:00 January 2020           NA           NA           NA
#> 160 2020-01-02 03:30:00 January 2020           NA           NA           NA
#> 161 2020-01-02 03:40:00 January 2020           NA           NA           NA
#> 162 2020-01-02 03:50:00 January 2020           NA           NA           NA
#> 163 2020-01-02 04:00:00 January 2020           NA           NA           NA
#> 164 2020-01-02 04:10:00 January 2020           NA           NA           NA
#> 165 2020-01-02 04:20:00 January 2020           NA           NA           NA
#> 166 2020-01-02 04:30:00 January 2020           NA           NA           NA
#> 167 2020-01-02 04:40:00 January 2020           NA           NA           NA
#> 168 2020-01-02 04:50:00 January 2020           NA           NA           NA
#> 169 2020-01-02 05:00:00 January 2020           NA           NA           NA
#> 170 2020-01-02 05:10:00 January 2020           NA           NA           NA
#> 171 2020-01-02 05:20:00 January 2020           NA           NA           NA
#> 172 2020-01-02 05:30:00 January 2020           NA           NA           NA
#> 173 2020-01-02 05:40:00 January 2020           NA           NA           NA
#> 174 2020-01-02 05:50:00 January 2020           NA           NA           NA
#> 175 2020-01-02 06:00:00 January 2020           NA           NA           NA
#> 176 2020-01-02 06:10:00 January 2020           NA           NA           NA
#> 177 2020-01-02 06:20:00 January 2020           NA           NA           NA
#> 178 2020-01-02 06:30:00 January 2020           NA           NA           NA
#> 179 2020-01-02 06:40:00 January 2020           NA           NA           NA
#> 180 2020-01-02 06:50:00 January 2020           NA           NA           NA
#> 181 2020-01-02 07:00:00 January 2020           NA           NA           NA
#> 182 2020-01-02 07:10:00 January 2020           NA           NA           NA
#> 183 2020-01-02 07:20:00 January 2020           NA           NA           NA
#> 184 2020-01-02 07:30:00 January 2020           NA           NA           NA
#> 185 2020-01-02 07:40:00 January 2020           NA           NA           NA
#> 186 2020-01-02 07:50:00 January 2020           NA           NA           NA
#> 187 2020-01-02 08:00:00 January 2020           NA           NA           NA
#> 188 2020-01-02 08:10:00 January 2020           NA           NA           NA
#> 189 2020-01-02 08:20:00 January 2020           NA           NA           NA
#> 190 2020-01-02 08:30:00 January 2020           NA           NA           NA
#> 191 2020-01-02 08:40:00 January 2020           NA           NA           NA
#> 192 2020-01-02 08:50:00 January 2020           NA           NA           NA
#> 193 2020-01-02 09:00:00 January 2020  0.074583651  0.074584635  0.074584628
#> 194 2020-01-02 09:10:00 January 2020           NA           NA           NA
#> 195 2020-01-02 09:20:00 January 2020           NA           NA           NA
#> 196 2020-01-02 09:30:00 January 2020           NA           NA           NA
#> 197 2020-01-02 09:40:00 January 2020           NA           NA           NA
#> 198 2020-01-02 09:50:00 January 2020           NA           NA           NA
#> 199 2020-01-02 10:00:00 January 2020           NA           NA           NA
#> 200 2020-01-02 10:10:00 January 2020           NA           NA           NA
```
