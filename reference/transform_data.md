# Transform and standardize numeric variables

Applies a transformation (log, sqrt, inverse, Box-Cox, Yeo-Johnson) or
standardization (z-score, centering, scaling, min-max, robust scaling)
to selected numeric columns. Group-wise transformation is supported.

## Usage

``` r
transform_data(data, cols = NULL, method = "log", group = NULL,
               lambda = 1.0, verbose = FALSE)
```

## Arguments

- data:

  A data frame or matrix containing numeric variables.

- cols:

  The column indices or names of selected variables. If NULL, all
  columns are used.

- method:

  Transformation method. One of `"log"`, `"log1p"`, `"sqrt"`,
  `"inverse"`, `"boxcox"`, `"yeojohnson"`, `"zscore"`, `"center"`,
  `"scale"`, `"minmax"`, `"robust"`.

- group:

  Optional grouping column for group-wise transformation.

- lambda:

  Parameter for Box-Cox and Yeo-Johnson transformations (default 1.0).

- verbose:

  Logical; if `TRUE`, prints progress message.

## Details

For `"boxcox"`, values must be positive. For `"yeojohnson"`, both
positive and negative values are allowed. Standardization methods ignore
NA values.

## Value

A data frame with transformed variables.

## Examples

``` r
# Log transformation
transform_data(data[1:100, c(1, 4, 17:19)], cols = 3:5, method = "log")
#>                    date    monthyear     3.98     4.47     5.01
#> 1   2020-01-01 00:00:00 January 2020       NA       NA       NA
#> 2   2020-01-01 00:10:00 January 2020       NA       NA       NA
#> 3   2020-01-01 00:20:00 January 2020       NA       NA       NA
#> 4   2020-01-01 00:30:00 January 2020 3.760553 4.314530 3.212355
#> 5   2020-01-01 00:40:00 January 2020       NA 1.926451 3.800367
#> 6   2020-01-01 00:50:00 January 2020 4.115129       NA       NA
#> 7   2020-01-01 01:00:00 January 2020 4.752693       NA       NA
#> 8   2020-01-01 01:10:00 January 2020       NA       NA       NA
#> 9   2020-01-01 01:20:00 January 2020 4.115129       NA       NA
#> 10  2020-01-01 01:30:00 January 2020 4.109898 1.885735 3.759645
#> 11  2020-01-01 01:40:00 January 2020       NA       NA       NA
#> 12  2020-01-01 01:50:00 January 2020       NA       NA       NA
#> 13  2020-01-01 02:00:00 January 2020       NA 2.635465 4.509376
#> 14  2020-01-01 02:10:00 January 2020       NA       NA       NA
#> 15  2020-01-01 02:20:00 January 2020 4.115129       NA       NA
#> 16  2020-01-01 02:30:00 January 2020 4.874915 5.428889 4.326713
#> 17  2020-01-01 02:40:00 January 2020       NA       NA       NA
#> 18  2020-01-01 02:50:00 January 2020       NA       NA       NA
#> 19  2020-01-01 03:00:00 January 2020       NA       NA       NA
#> 20  2020-01-01 03:10:00 January 2020 4.894274 4.998563 3.896383
#> 21  2020-01-01 03:20:00 January 2020 4.115129       NA       NA
#> 22  2020-01-01 03:40:00 January 2020       NA       NA       NA
#> 23  2020-01-01 03:50:00 January 2020       NA       NA       NA
#> 24  2020-01-01 04:00:00 January 2020       NA       NA       NA
#> 25  2020-01-01 04:10:00 January 2020       NA       NA       NA
#> 26  2020-01-01 04:20:00 January 2020 3.758226 4.312204 3.210025
#> 27  2020-01-01 04:30:00 January 2020 3.760553 4.314530 3.212355
#> 28  2020-01-01 04:40:00 January 2020       NA       NA       NA
#> 29  2020-01-01 04:50:00 January 2020       NA 1.976855 3.850769
#> 30  2020-01-01 05:00:00 January 2020       NA       NA       NA
#> 31  2020-01-01 05:10:00 January 2020 4.570076 4.278837 3.176661
#> 32  2020-01-01 05:20:00 January 2020 3.683626 4.325602 4.166447
#> 33  2020-01-01 05:30:00 January 2020 4.571613 4.283591 3.181415
#> 34  2020-01-01 05:40:00 January 2020       NA       NA       NA
#> 35  2020-01-01 05:50:00 January 2020       NA       NA       NA
#> 36  2020-01-01 06:00:00 January 2020       NA       NA       NA
#> 37  2020-01-01 06:10:00 January 2020       NA       NA       NA
#> 38  2020-01-01 06:20:00 January 2020 3.807074 4.361051 3.258873
#> 39  2020-01-01 06:30:00 January 2020 4.115129       NA       NA
#> 40  2020-01-01 06:40:00 January 2020 4.414732 5.014003 4.512935
#> 41  2020-01-01 06:50:00 January 2020       NA       NA       NA
#> 42  2020-01-01 07:00:00 January 2020 4.115129       NA       NA
#> 43  2020-01-01 07:10:00 January 2020       NA       NA       NA
#> 44  2020-01-01 07:20:00 January 2020 4.056648       NA       NA
#> 45  2020-01-01 07:30:00 January 2020       NA       NA       NA
#> 46  2020-01-01 07:40:00 January 2020       NA       NA       NA
#> 47  2020-01-01 07:50:00 January 2020 4.115129       NA       NA
#> 48  2020-01-01 08:00:00 January 2020       NA       NA       NA
#> 49  2020-01-01 08:10:00 January 2020       NA       NA       NA
#> 50  2020-01-01 08:20:00 January 2020       NA       NA       NA
#> 51  2020-01-01 08:30:00 January 2020       NA       NA       NA
#> 52  2020-01-01 08:40:00 January 2020 3.583424 4.337322 4.710449
#> 53  2020-01-01 08:50:00 January 2020       NA       NA       NA
#> 54  2020-01-01 09:00:00 January 2020       NA       NA       NA
#> 55  2020-01-01 09:10:00 January 2020 4.115129       NA       NA
#> 56  2020-01-01 09:20:00 January 2020 4.552491 4.333788 3.231610
#> 57  2020-01-01 09:30:00 January 2020       NA 1.888130 3.762046
#> 58  2020-01-01 09:40:00 January 2020       NA 1.942318 3.816228
#> 59  2020-01-01 09:50:00 January 2020       NA       NA       NA
#> 60  2020-01-01 10:20:00 January 2020       NA       NA       NA
#> 61  2020-01-01 10:30:00 January 2020       NA       NA       NA
#> 62  2020-01-01 10:40:00 January 2020       NA       NA       NA
#> 63  2020-01-01 10:50:00 January 2020       NA       NA       NA
#> 64  2020-01-01 11:00:00 January 2020       NA       NA       NA
#> 65  2020-01-01 11:10:00 January 2020       NA       NA       NA
#> 66  2020-01-01 11:20:00 January 2020       NA       NA       NA
#> 67  2020-01-01 11:30:00 January 2020 4.808274       NA       NA
#> 68  2020-01-01 11:40:00 January 2020       NA       NA       NA
#> 69  2020-01-01 11:50:00 January 2020       NA       NA       NA
#> 70  2020-01-01 12:00:00 January 2020       NA       NA       NA
#> 71  2020-01-01 12:10:00 January 2020       NA       NA       NA
#> 72  2020-01-01 12:20:00 January 2020 4.865887 4.999527 3.897352
#> 73  2020-01-01 12:30:00 January 2020       NA       NA       NA
#> 74  2020-01-01 12:40:00 January 2020       NA       NA       NA
#> 75  2020-01-01 12:50:00 January 2020       NA       NA       NA
#> 76  2020-01-01 13:00:00 January 2020       NA       NA       NA
#> 77  2020-01-01 13:40:00 January 2020       NA       NA       NA
#> 78  2020-01-01 13:50:00 January 2020       NA       NA       NA
#> 79  2020-01-01 14:00:00 January 2020       NA       NA       NA
#> 80  2020-01-01 14:10:00 January 2020       NA       NA       NA
#> 81  2020-01-01 14:20:00 January 2020       NA 1.942318 3.816228
#> 82  2020-01-01 14:30:00 January 2020       NA       NA       NA
#> 83  2020-01-01 14:40:00 January 2020       NA 3.053199 4.927109
#> 84  2020-01-01 14:50:00 January 2020       NA 2.635465 4.509376
#> 85  2020-01-01 15:00:00 January 2020       NA       NA       NA
#> 86  2020-01-01 15:10:00 January 2020       NA       NA       NA
#> 87  2020-01-01 15:20:00 January 2020 4.808274       NA       NA
#> 88  2020-01-01 15:30:00 January 2020       NA 1.978682 3.852596
#> 89  2020-01-01 15:40:00 January 2020 3.760553 4.314530 3.212355
#> 90  2020-01-01 15:50:00 January 2020       NA       NA       NA
#> 91  2020-01-01 16:00:00 January 2020       NA       NA       NA
#> 92  2020-01-01 16:10:00 January 2020       NA       NA       NA
#> 93  2020-01-01 16:20:00 January 2020       NA       NA       NA
#> 94  2020-01-01 16:30:00 January 2020       NA       NA       NA
#> 95  2020-01-01 16:40:00 January 2020       NA       NA       NA
#> 96  2020-01-01 16:50:00 January 2020       NA       NA       NA
#> 97  2020-01-01 17:00:00 January 2020       NA       NA       NA
#> 98  2020-01-01 17:10:00 January 2020       NA       NA       NA
#> 99  2020-01-01 17:20:00 January 2020       NA       NA       NA
#> 100 2020-01-01 17:30:00 January 2020       NA       NA       NA
# Z-score standardization by group
transform_data(data[1:100, c(1, 4, 17:19)], cols = 3:5, method = "zscore", group = 2)
#>                    date    monthyear       3.98       4.47       5.01
#> 1   2020-01-01 00:00:00 January 2020         NA         NA         NA
#> 2   2020-01-01 00:10:00 January 2020         NA         NA         NA
#> 3   2020-01-01 00:20:00 January 2020         NA         NA         NA
#> 4   2020-01-01 00:30:00 January 2020 -0.9982888  0.1927828 -0.9064783
#> 5   2020-01-01 00:40:00 January 2020         NA -0.9628325 -0.2694683
#> 6   2020-01-01 00:50:00 January 2020 -0.4298319         NA         NA
#> 7   2020-01-01 01:00:00 January 2020  1.2684578         NA         NA
#> 8   2020-01-01 01:10:00 January 2020         NA         NA         NA
#> 9   2020-01-01 01:20:00 January 2020 -0.4298319         NA         NA
#> 10  2020-01-01 01:30:00 January 2020 -0.4397663 -0.9674932 -0.3266451
#> 11  2020-01-01 01:40:00 January 2020         NA         NA         NA
#> 12  2020-01-01 01:50:00 January 2020         NA         NA         NA
#> 13  2020-01-01 02:00:00 January 2020         NA -0.8422792  1.2092173
#> 14  2020-01-01 02:10:00 January 2020         NA         NA         NA
#> 15  2020-01-01 02:20:00 January 2020 -0.4298319         NA         NA
#> 16  2020-01-01 02:30:00 January 2020  1.7367970  2.7982322  0.7231311
#> 17  2020-01-01 02:40:00 January 2020         NA         NA         NA
#> 18  2020-01-01 02:50:00 January 2020         NA         NA         NA
#> 19  2020-01-01 03:00:00 January 2020         NA         NA         NA
#> 20  2020-01-01 03:10:00 January 2020  1.8163714  1.4421240 -0.1250684
#> 21  2020-01-01 03:20:00 January 2020 -0.4298319         NA         NA
#> 22  2020-01-01 03:40:00 January 2020         NA         NA         NA
#> 23  2020-01-01 03:50:00 January 2020         NA         NA         NA
#> 24  2020-01-01 04:00:00 January 2020         NA         NA         NA
#> 25  2020-01-01 04:10:00 January 2020         NA         NA         NA
#> 26  2020-01-01 04:20:00 January 2020 -1.0013940  0.1898254 -0.9083304
#> 27  2020-01-01 04:30:00 January 2020 -0.9982888  0.1927828 -0.9064783
#> 28  2020-01-01 04:40:00 January 2020         NA         NA         NA
#> 29  2020-01-01 04:50:00 January 2020         NA -0.9567935 -0.1953987
#> 30  2020-01-01 05:00:00 January 2020         NA         NA         NA
#> 31  2020-01-01 05:10:00 January 2020  0.6671547  0.1481668 -0.9343842
#> 32  2020-01-01 05:20:00 January 2020 -1.0971911  0.2069486  0.3639593
#> 33  2020-01-01 05:30:00 January 2020  0.6717738  0.1540169 -0.9307249
#> 34  2020-01-01 05:40:00 January 2020         NA         NA         NA
#> 35  2020-01-01 05:50:00 January 2020         NA         NA         NA
#> 36  2020-01-01 06:00:00 January 2020         NA         NA         NA
#> 37  2020-01-01 06:10:00 January 2020         NA         NA         NA
#> 38  2020-01-01 06:20:00 January 2020 -0.9346820  0.2533751 -0.8685815
#> 39  2020-01-01 06:30:00 January 2020 -0.4298319         NA         NA
#> 40  2020-01-01 06:40:00 January 2020  0.2353483  1.4813629  1.2195991
#> 41  2020-01-01 06:50:00 January 2020         NA         NA         NA
#> 42  2020-01-01 07:00:00 January 2020 -0.4298319         NA         NA
#> 43  2020-01-01 07:10:00 January 2020         NA         NA         NA
#> 44  2020-01-01 07:20:00 January 2020 -0.5379972         NA         NA
#> 45  2020-01-01 07:30:00 January 2020         NA         NA         NA
#> 46  2020-01-01 07:40:00 January 2020         NA         NA         NA
#> 47  2020-01-01 07:50:00 January 2020 -0.4298319         NA         NA
#> 48  2020-01-01 08:00:00 January 2020         NA         NA         NA
#> 49  2020-01-01 08:10:00 January 2020         NA         NA         NA
#> 50  2020-01-01 08:20:00 January 2020         NA         NA         NA
#> 51  2020-01-01 08:30:00 January 2020         NA         NA         NA
#> 52  2020-01-01 08:40:00 January 2020 -1.2151167  0.2221167  1.8576600
#> 53  2020-01-01 08:50:00 January 2020         NA         NA         NA
#> 54  2020-01-01 09:00:00 January 2020         NA         NA         NA
#> 55  2020-01-01 09:10:00 January 2020 -0.4298319         NA         NA
#> 56  2020-01-01 09:20:00 January 2020  0.6148408  0.2175241 -0.8910049
#> 57  2020-01-01 09:30:00 January 2020         NA -0.9672244 -0.3233383
#> 58  2020-01-01 09:40:00 January 2020         NA -0.9609642 -0.2465611
#> 59  2020-01-01 09:50:00 January 2020         NA         NA         NA
#> 60  2020-01-01 10:20:00 January 2020         NA         NA         NA
#> 61  2020-01-01 10:30:00 January 2020         NA         NA         NA
#> 62  2020-01-01 10:40:00 January 2020         NA         NA         NA
#> 63  2020-01-01 10:50:00 January 2020         NA         NA         NA
#> 64  2020-01-01 11:00:00 January 2020         NA         NA         NA
#> 65  2020-01-01 11:10:00 January 2020         NA         NA         NA
#> 66  2020-01-01 11:20:00 January 2020         NA         NA         NA
#> 67  2020-01-01 11:30:00 January 2020  1.4743567         NA         NA
#> 68  2020-01-01 11:40:00 January 2020         NA         NA         NA
#> 69  2020-01-01 11:50:00 January 2020         NA         NA         NA
#> 70  2020-01-01 12:00:00 January 2020         NA         NA         NA
#> 71  2020-01-01 12:10:00 January 2020         NA         NA         NA
#> 72  2020-01-01 12:20:00 January 2020  1.7002114  1.4445573 -0.1235400
#> 73  2020-01-01 12:30:00 January 2020         NA         NA         NA
#> 74  2020-01-01 12:40:00 January 2020         NA         NA         NA
#> 75  2020-01-01 12:50:00 January 2020         NA         NA         NA
#> 76  2020-01-01 13:00:00 January 2020         NA         NA         NA
#> 77  2020-01-01 13:40:00 January 2020         NA         NA         NA
#> 78  2020-01-01 13:50:00 January 2020         NA         NA         NA
#> 79  2020-01-01 14:00:00 January 2020         NA         NA         NA
#> 80  2020-01-01 14:10:00 January 2020         NA         NA         NA
#> 81  2020-01-01 14:20:00 January 2020         NA -0.9609642 -0.2465611
#> 82  2020-01-01 14:30:00 January 2020         NA         NA         NA
#> 83  2020-01-01 14:40:00 January 2020         NA -0.7191990  2.7189008
#> 84  2020-01-01 14:50:00 January 2020         NA -0.8422792  1.2092173
#> 85  2020-01-01 15:00:00 January 2020         NA         NA         NA
#> 86  2020-01-01 15:10:00 January 2020         NA         NA         NA
#> 87  2020-01-01 15:20:00 January 2020  1.4743567         NA         NA
#> 88  2020-01-01 15:30:00 January 2020         NA -0.9565689 -0.1926430
#> 89  2020-01-01 15:40:00 January 2020 -0.9982888  0.1927828 -0.9064783
#> 90  2020-01-01 15:50:00 January 2020         NA         NA         NA
#> 91  2020-01-01 16:00:00 January 2020         NA         NA         NA
#> 92  2020-01-01 16:10:00 January 2020         NA         NA         NA
#> 93  2020-01-01 16:20:00 January 2020         NA         NA         NA
#> 94  2020-01-01 16:30:00 January 2020         NA         NA         NA
#> 95  2020-01-01 16:40:00 January 2020         NA         NA         NA
#> 96  2020-01-01 16:50:00 January 2020         NA         NA         NA
#> 97  2020-01-01 17:00:00 January 2020         NA         NA         NA
#> 98  2020-01-01 17:10:00 January 2020         NA         NA         NA
#> 99  2020-01-01 17:20:00 January 2020         NA         NA         NA
#> 100 2020-01-01 17:30:00 January 2020         NA         NA         NA
```
