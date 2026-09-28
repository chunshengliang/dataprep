# Discretize continuous variables into bins

Converts numeric columns into categorical factors using equal-width,
equal-frequency, or custom breakpoints. This is a common preprocessing
step for transforming continuous variables for modeling or
visualization.

## Usage

``` r
bin_data(data, cols = NULL, method = "equal_width", 
         bins = 10, breaks = NULL, include_lowest = TRUE,
         labels = NULL, verbose = FALSE)
```

## Arguments

- data:

  A data frame containing numeric columns to bin.

- cols:

  Column indices or names to bin. If `NULL`, all numeric columns are
  used.

- method:

  Binning method. One of `"equal_width"`, `"equal_freq"`, or `"custom"`.

- bins:

  Number of bins for equal-width and equal-frequency methods.

- breaks:

  Numeric vector of breakpoints for custom binning. Required when
  `method = "custom"`.

- labels:

  Optional character vector of labels for the bins.

- include_lowest:

  Logical; if `TRUE`, the lowest break value is included in the first
  bin.

- verbose:

  Logical; if `TRUE`, prints a timing message.

## Details

For `"equal_width"`, bins are created by dividing the range of the data
into equal-width intervals. For `"equal_freq"`, bins are created by
quantiles so that each bin contains approximately the same number of
observations. The `"custom"` method uses user-supplied breakpoints.

Missing values are preserved as `NA` in the output.

## Value

If `data` is a data frame, a data frame with the selected columns
replaced by factors. If `data` is a numeric vector, a factor vector.

## Examples

``` r
data <- data.frame(x = rnorm(100), y = runif(100))
# Equal-width binning into 5 bins
bin_data(data, cols = 1:2, bins = 5)
#>        x    y
#> 1      2    2
#> 2      3    5
#> 3      1    2
#> 4      3    1
#> 5      4    1
#> 6      4    5
#> 7      1    2
#> 8      3    1
#> 9      3    1
#> 10     3    5
#> 11     2    2
#> 12     4    4
#> 13     5    2
#> 14     1    5
#> 15     3    2
#> 16     1 <NA>
#> 17     2    3
#> 18     3    4
#> 19     3    5
#> 20     2    4
#> 21     3    3
#> 22     3    1
#> 23     2    1
#> 24     4    4
#> 25     5    4
#> 26     3    4
#> 27     2    2
#> 28     3    5
#> 29     2    4
#> 30     2    5
#> 31     4    5
#> 32     3    2
#> 33     3    4
#> 34     4    2
#> 35     3    1
#> 36     3    4
#> 37     1    4
#> 38     3    3
#> 39     3    5
#> 40     4    3
#> 41     3    5
#> 42     2    1
#> 43     3    4
#> 44     3    2
#> 45     3    5
#> 46  <NA>    4
#> 47     3    4
#> 48     3    4
#> 49     3    3
#> 50     1    2
#> 51     4    3
#> 52     3    3
#> 53     3    4
#> 54     3    4
#> 55     3    2
#> 56     3    5
#> 57     1    2
#> 58     5    1
#> 59     3    3
#> 60     3    2
#> 61     4    1
#> 62     2    5
#> 63     4    5
#> 64     3    4
#> 65     2    3
#> 66     2    3
#> 67     4    1
#> 68     3    2
#> 69     3    1
#> 70     1    4
#> 71     2    4
#> 72     3    5
#> 73     4    1
#> 74     3    5
#> 75     4    2
#> 76     3    4
#> 77     3    5
#> 78     3    1
#> 79     3    3
#> 80     3    4
#> 81     5    3
#> 82     4    3
#> 83     4    1
#> 84     3    1
#> 85     2    5
#> 86     4    1
#> 87     1    4
#> 88     3    5
#> 89     3    5
#> 90     3    4
#> 91     3    5
#> 92     4    1
#> 93     4    1
#> 94     3    1
#> 95     3    4
#> 96     4    3
#> 97     3    1
#> 98     4    1
#> 99     4    5
#> 100    3    2

# Equal-frequency binning
bin_data(data, cols = "x", method = "equal_freq", bins = 4)
#>        x          y
#> 1      1 0.34929905
#> 2      3 0.94731827
#> 3      1 0.21609998
#> 4      2 0.03209271
#> 5      3 0.14531584
#> 6      4 0.85438389
#> 7      1 0.21314931
#> 8      2 0.21031074
#> 9      2 0.03952069
#> 10     2 0.94477480
#> 11     1 0.24492799
#> 12     4 0.78112257
#> 13     4 0.28823717
#> 14     1 0.87535791
#> 15     3 0.29575009
#> 16     1 0.98352541
#> 17     1 0.58983756
#> 18     2 0.75915838
#> 19     3 0.83607531
#> 20     1 0.76281947
#> 21     3 0.41726993
#> 22     3 0.13807484
#> 23     1 0.08084496
#> 24     4 0.65598263
#> 25     4 0.60200386
#> 26     2 0.65699583
#> 27     1 0.32931716
#> 28     2 0.97947422
#> 29     1 0.71518613
#> 30     1 0.87263030
#> 31     4 0.98328375
#> 32     3 0.21856299
#> 33     3 0.66453006
#> 34     4 0.38956404
#> 35     3 0.04606364
#> 36     2 0.61691456
#> 37     1 0.59847499
#> 38     2 0.40685363
#> 39     2 0.85832815
#> 40     4 0.51768118
#> 41     2 0.97929341
#> 42     1 0.01701569
#> 43     2 0.67344783
#> 44     2 0.37126988
#> 45     3 0.91801064
#> 46  <NA> 0.67797809
#> 47     2 0.66515246
#> 48     3 0.75604109
#> 49     3 0.54283715
#> 50     1 0.23928810
#> 51     4 0.50889357
#> 52     2 0.41726437
#> 53     2 0.72694885
#> 54     2 0.63768555
#> 55     2 0.39640996
#> 56     3 0.95948261
#> 57     1 0.29865803
#> 58     4 0.05020117
#> 59     1 0.57618742
#> 60     3 0.21790581
#> 61     4 0.12585627
#> 62     1 0.93815269
#> 63     4 0.80127513
#> 64     2 0.75805362
#> 65     1 0.53256516
#> 66     1 0.54680477
#> 67     4 0.09592650
#> 68     3 0.38834975
#> 69     3 0.17235189
#> 70     1 0.69072585
#> 71     1 0.67520850
#> 72     3 0.94629485
#> 73     4 0.19621952
#> 74     3 0.96863750
#> 75     4 0.38709628
#> 76     3 0.65034390
#> 77     3 0.81459620
#> 78     2 0.07096477
#> 79     3 0.52683032
#> 80     2 0.76347483
#> 81     4 0.43538664
#> 82     4 0.55247234
#> 83     4 0.20403065
#> 84     3 0.03102602
#> 85     1 0.96970706
#> 86     4 0.17861309
#> 87     1 0.77829279
#> 88     2 0.88571080
#> 89     3 0.83644625
#> 90     1 0.60536844
#> 91     3 0.90687946
#> 92     4 0.03590981
#> 93     4 0.13141851
#> 94     2 0.09403037
#> 95     3 0.69658366
#> 96     4 0.40572872
#> 97     2 0.06563664
#> 98     4 0.12649262
#> 99     4 0.93733022
#> 100    2 0.21638023

# Custom breaks
bin_data(data, cols = "x", method = "custom", breaks = c(-Inf, 0, Inf), labels = c("neg", "pos"))
#>       x          y
#> 1   neg 0.34929905
#> 2   pos 0.94731827
#> 3   neg 0.21609998
#> 4   neg 0.03209271
#> 5   pos 0.14531584
#> 6   pos 0.85438389
#> 7   neg 0.21314931
#> 8   neg 0.21031074
#> 9   neg 0.03952069
#> 10  neg 0.94477480
#> 11  neg 0.24492799
#> 12  pos 0.78112257
#> 13  pos 0.28823717
#> 14  neg 0.87535791
#> 15  pos 0.29575009
#> 16  neg 0.98352541
#> 17  neg 0.58983756
#> 18  neg 0.75915838
#> 19  pos 0.83607531
#> 20  neg 0.76281947
#> 21  pos 0.41726993
#> 22  pos 0.13807484
#> 23  neg 0.08084496
#> 24  pos 0.65598263
#> 25  pos 0.60200386
#> 26  neg 0.65699583
#> 27  neg 0.32931716
#> 28  neg 0.97947422
#> 29  neg 0.71518613
#> 30  neg 0.87263030
#> 31  pos 0.98328375
#> 32  pos 0.21856299
#> 33  pos 0.66453006
#> 34  pos 0.38956404
#> 35  pos 0.04606364
#> 36  neg 0.61691456
#> 37  neg 0.59847499
#> 38  neg 0.40685363
#> 39  neg 0.85832815
#> 40  pos 0.51768118
#> 41  pos 0.97929341
#> 42  neg 0.01701569
#> 43  neg 0.67344783
#> 44  neg 0.37126988
#> 45  pos 0.91801064
#> 46  pos 0.67797809
#> 47  pos 0.66515246
#> 48  pos 0.75604109
#> 49  pos 0.54283715
#> 50  neg 0.23928810
#> 51  pos 0.50889357
#> 52  neg 0.41726437
#> 53  neg 0.72694885
#> 54  pos 0.63768555
#> 55  pos 0.39640996
#> 56  pos 0.95948261
#> 57  neg 0.29865803
#> 58  pos 0.05020117
#> 59  neg 0.57618742
#> 60  pos 0.21790581
#> 61  pos 0.12585627
#> 62  neg 0.93815269
#> 63  pos 0.80127513
#> 64  neg 0.75805362
#> 65  neg 0.53256516
#> 66  neg 0.54680477
#> 67  pos 0.09592650
#> 68  pos 0.38834975
#> 69  pos 0.17235189
#> 70  neg 0.69072585
#> 71  neg 0.67520850
#> 72  pos 0.94629485
#> 73  pos 0.19621952
#> 74  pos 0.96863750
#> 75  pos 0.38709628
#> 76  pos 0.65034390
#> 77  pos 0.81459620
#> 78  neg 0.07096477
#> 79  pos 0.52683032
#> 80  neg 0.76347483
#> 81  pos 0.43538664
#> 82  pos 0.55247234
#> 83  pos 0.20403065
#> 84  pos 0.03102602
#> 85  neg 0.96970706
#> 86  pos 0.17861309
#> 87  neg 0.77829279
#> 88  neg 0.88571080
#> 89  pos 0.83644625
#> 90  neg 0.60536844
#> 91  pos 0.90687946
#> 92  pos 0.03590981
#> 93  pos 0.13141851
#> 94  neg 0.09403037
#> 95  pos 0.69658366
#> 96  pos 0.40572872
#> 97  neg 0.06563664
#> 98  pos 0.12649262
#> 99  pos 0.93733022
#> 100 neg 0.21638023
```
