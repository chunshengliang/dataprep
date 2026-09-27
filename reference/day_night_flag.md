# Day/night flag

Creates a day/night indicator for each time point. If latitude and
longitude are not provided, local time is used (daylight defined by an
hour threshold). Otherwise, an approximate solar zenith angle is
computed to determine whether the sun is above the horizon.

## Usage

``` r
day_night_flag(data, date_col = NULL, lat = NULL, lon = NULL,
               threshold = 6, type = c("binary", "sun", "shade"),
               local_tz = "UTC", verbose = FALSE)
```

## Arguments

- data:

  A data frame with a time column.

- date_col:

  Time column index or name. If `NULL`, the first column matching `date`
  is used.

- lat:

  Latitude of the location(s). Can be a scalar or a vector of length
  equal to the number of rows.

- lon:

  Longitude of the location(s). Same behavior as `lat`.

- threshold:

  Hour threshold used for local-time based classification (default 6).
  Only used if latitude/longitude are missing.

- type:

  Output type: `"binary"` returns 1/0, `"sun"` returns "day"/"night",
  `"shade"` returns "day"/"shade".

- local_tz:

  Time zone for local time calculation (default UTC).

- verbose:

  Logical; if `TRUE`, prints progress message.

## Details

When latitude/longitude are supplied, the function computes the solar
zenith angle using a simplified astronomical formula. Values with a
solar zenith angle below 90 degrees are considered day.

## Value

A vector of day/night flags.

## References

1\. Example data is from https://smear.avaa.csc.fi/download. It includes
particle number concentrations in SMEAR I Varrio forest.

## Author

Chun-Sheng Liang \<chun-shengliang@qq.com\>

## Examples

``` r
day_night_flag(data[1:100, c(1, 4, 17:19)], date_col = 1, lat = 40, lon = -105)
#>   [1] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1
#>  [38] 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
#>  [75] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
```
