# Example data (particle number concentrations in SMEAR I Varrio forest)

Particle number size distribution (PNSD) and auxiliary variables
measured at the SMEAR I Varrio forest station in Finland. The raw data
were downloaded from <https://smear.avaa.csc.fi/download> and subset to
two months (January and July 2020) for package size reasons.

## Usage

``` r
data
```

## Format

A data frame with 7,640 observations on the following 65 variables.

- `date`:

  a POSIXct vector, sampling time (10-minute resolution).

- `tconc`:

  a numeric vector, total number concentration.

- `TPNC`:

  a numeric vector, total particle number concentration.

- `monthyear`:

  a character vector, e.g. `"January 2020"`.

- `1`:

  a numeric vector, particle diameter in nm (1 nm).

- `1.12`:

  a numeric vector, particle diameter in nm.

- `1.26`:

  a numeric vector, particle diameter in nm.

- `1.41`:

  a numeric vector, particle diameter in nm.

- `1.58`:

  a numeric vector, particle diameter in nm.

- `1.78`:

  a numeric vector, particle diameter in nm.

- `2`:

  a numeric vector, particle diameter in nm.

- `2.24`:

  a numeric vector, particle diameter in nm.

- `2.51`:

  a numeric vector, particle diameter in nm.

- `2.82`:

  a numeric vector, particle diameter in nm.

- `3.16`:

  a numeric vector, particle diameter in nm.

- `3.55`:

  a numeric vector, particle diameter in nm.

- `3.98`:

  a numeric vector, particle diameter in nm.

- `4.47`:

  a numeric vector, particle diameter in nm.

- `5.01`:

  a numeric vector, particle diameter in nm.

- `5.62`:

  a numeric vector, particle diameter in nm.

- `6.31`:

  a numeric vector, particle diameter in nm.

- `7.08`:

  a numeric vector, particle diameter in nm.

- `7.94`:

  a numeric vector, particle diameter in nm.

- `8.91`:

  a numeric vector, particle diameter in nm.

- `10`:

  a numeric vector, particle diameter in nm.

- `11.2`:

  a numeric vector, particle diameter in nm.

- `12.6`:

  a numeric vector, particle diameter in nm.

- `14.1`:

  a numeric vector, particle diameter in nm.

- `15.8`:

  a numeric vector, particle diameter in nm.

- `17.8`:

  a numeric vector, particle diameter in nm.

- `20`:

  a numeric vector, particle diameter in nm.

- `22.4`:

  a numeric vector, particle diameter in nm.

- `25.1`:

  a numeric vector, particle diameter in nm.

- `28.2`:

  a numeric vector, particle diameter in nm.

- `31.6`:

  a numeric vector, particle diameter in nm.

- `35.5`:

  a numeric vector, particle diameter in nm.

- `39.8`:

  a numeric vector, particle diameter in nm.

- `44.7`:

  a numeric vector, particle diameter in nm.

- `50.1`:

  a numeric vector, particle diameter in nm.

- `56.2`:

  a numeric vector, particle diameter in nm.

- `63.1`:

  a numeric vector, particle diameter in nm.

- `70.8`:

  a numeric vector, particle diameter in nm.

- `79.4`:

  a numeric vector, particle diameter in nm.

- `89.1`:

  a numeric vector, particle diameter in nm.

- `100`:

  a numeric vector, particle diameter in nm.

- `112`:

  a numeric vector, particle diameter in nm.

- `126`:

  a numeric vector, particle diameter in nm.

- `141`:

  a numeric vector, particle diameter in nm.

- `158`:

  a numeric vector, particle diameter in nm.

- `178`:

  a numeric vector, particle diameter in nm.

- `200`:

  a numeric vector, particle diameter in nm.

- `224`:

  a numeric vector, particle diameter in nm.

- `251`:

  a numeric vector, particle diameter in nm.

- `282`:

  a numeric vector, particle diameter in nm.

- `316`:

  a numeric vector, particle diameter in nm.

- `355`:

  a numeric vector, particle diameter in nm.

- `398`:

  a numeric vector, particle diameter in nm.

- `447`:

  a numeric vector, particle diameter in nm.

- `501`:

  a numeric vector, particle diameter in nm.

- `562`:

  a numeric vector, particle diameter in nm.

- `631`:

  a numeric vector, particle diameter in nm.

- `708`:

  a numeric vector, particle diameter in nm.

- `794`:

  a numeric vector, particle diameter in nm.

- `891`:

  a numeric vector, particle diameter in nm.

- `1000`:

  a numeric vector, particle diameter in nm.

## Details

The 60 numeric channels between `1` and `1000` are size bins of the
differential particle number size distribution, logarithmically spaced
at 64 channels per decade. Column names are the geometric mean diameter
in nanometres, written in the shortest form that R accepts as a name (so
`1` rather than `1.00`, and `10` rather than `10.0`). Values are number
concentrations in cm\\^{-3}\\. `tconc` and `TPNC` are integrals of the
size distribution; `monthyear` is the month label used for group-wise
cleaning.

The data are the raw input for the cleaning pipeline described in
[`vignette("dataprep-cleaning")`](https://chunshengliang.github.io/dataprep/articles/dataprep-cleaning.md).
They contain:

- size bins with substantial missing fractions (especially the smallest
  and largest bins),

- observations with long consecutive missing runs,

- extreme values that are not always genuine outliers.

After running
[`dataprep`](https://chunshengliang.github.io/dataprep/reference/dataprep.md)
on this table, a smaller, cleaner table is returned. The
[`data1`](https://chunshengliang.github.io/dataprep/reference/data1.md)
dataset is the already-aggregated seven-column version; it has no long
gaps and no obvious outliers, and is not a useful input for the cleaning
pipeline.

## Source

<https://smear.avaa.csc.fi/download>

## References

Liang, C.-S., Wu, H., Li, H.-Y., Zhang, Q., Li, Z. & He, K.-B. (2020).
Efficient data preprocessing, episode classification, and source
apportionment of particle number concentrations. *Science of the Total
Environment*, 741, 140923.
[doi:10.1016/j.scitotenv.2020.140923](https://doi.org/10.1016/j.scitotenv.2020.140923)

## Examples

``` r
data
#>                   date    tconc      TPNC    monthyear  1 1.12 1.26 1.41 1.58
#> 1  2020-01-01 00:00:00  53.1926  53.06576 January 2020 NA   NA   NA   NA   NA
#> 2  2020-01-01 00:10:00  56.9493  56.90744 January 2020 NA   NA   NA   NA   NA
#> 3  2020-01-01 00:20:00  71.5224  71.13674 January 2020 NA   NA   NA   NA   NA
#> 4  2020-01-01 00:30:00  64.7958  65.25465 January 2020 NA   NA   NA   NA   NA
#> 5  2020-01-01 00:40:00  53.8040  53.58410 January 2020 NA   NA   NA   NA   NA
#> 6  2020-01-01 00:50:00  60.5816  60.31026 January 2020 NA   NA   NA   NA   NA
#> 7  2020-01-01 01:00:00 105.3540 109.87059 January 2020 NA   NA   NA   NA   NA
#> 8  2020-01-01 01:10:00  50.2337  49.76028 January 2020 NA   NA   NA   NA   NA
#> 9  2020-01-01 01:20:00  69.1047  69.14077 January 2020 NA   NA   NA   NA   NA
#> 10 2020-01-01 01:30:00  58.5322  58.57991 January 2020 NA   NA   NA   NA   NA
#> 11 2020-01-01 01:40:00  70.8015  75.10122 January 2020 NA   NA   NA   NA   NA
#> 12 2020-01-01 01:50:00  47.4897  47.29888 January 2020 NA   NA   NA   NA   NA
#> 13 2020-01-01 02:00:00  52.4953  52.27945 January 2020 NA   NA   NA   NA   NA
#> 14 2020-01-01 02:10:00  50.4983  50.56744 January 2020 NA   NA   NA   NA   NA
#> 15 2020-01-01 02:20:00  59.5743  59.66142 January 2020 NA   NA   NA   NA   NA
#>    1.78  2 2.24 2.51    2.82     3.16     3.55     3.98    4.47    5.01    5.62
#> 1    NA NA   NA   NA      NA       NA       NA       NA      NA      NA      NA
#> 2    NA NA   NA   NA      NA       NA       NA       NA      NA      NA      NA
#> 3    NA NA   NA   NA      NA       NA       NA       NA      NA      NA      NA
#> 4    NA NA   NA   NA      NA       NA       NA  42.9722 74.7785 24.8375      NA
#> 5    NA NA   NA   NA      NA       NA       NA       NA  6.8651 44.7176 58.5297
#> 6    NA NA   NA   NA      NA  40.5477 115.2830  61.2601      NA      NA      NA
#> 7    NA NA   NA   NA 470.911 361.9220 252.9340 115.8960      NA      NA 14.1948
#> 8    NA NA   NA   NA      NA       NA       NA       NA      NA      NA 14.6608
#> 9    NA NA   NA   NA      NA  40.5477 115.2830  61.2601      NA      NA      NA
#> 10   NA NA   NA   NA      NA  40.3362 114.6820  60.9405  6.5912 42.9332 56.7709
#> 11   NA NA   NA   NA 315.819 168.1800  20.5409       NA      NA      NA      NA
#> 12   NA NA   NA   NA      NA       NA       NA       NA      NA      NA      NA
#> 13   NA NA   NA   NA      NA       NA       NA       NA 13.9498 90.8651 90.4969
#> 14   NA NA   NA   NA      NA       NA       NA       NA      NA      NA      NA
#> 15   NA NA   NA   NA      NA  40.5477 115.2830  61.2601      NA      NA 14.0614
#>       6.31    7.08    7.94    8.91      10     11.2     12.6    14.1     15.8
#> 1       NA      NA  3.2424 26.0804 27.8384   4.9901  12.7131 27.2145  26.1879
#> 2       NA      NA      NA      NA  9.1750  29.0599  43.1451 55.8811  71.0050
#> 3       NA 37.6727 73.8825 51.9995 29.1677   5.2284  12.6326 33.5072  95.8604
#> 4       NA      NA      NA      NA 44.9527 142.3780 100.0780 26.9447  26.0886
#> 5  48.5561 23.8906  3.2418 26.0756 27.8333   4.9892  12.9733 26.1856   9.5904
#> 6       NA      NA  6.4869 52.1779 55.6953   9.9835       NA  3.2166  34.7474
#> 7  42.4341 24.2348  3.2418 26.0756 27.8333   4.9892  12.6882 27.3339  28.0025
#> 8  43.8271 25.0304      NA      NA      NA       NA       NA  8.0684  87.1581
#> 9       NA      NA      NA      NA 17.8602  56.5686  46.9412 35.8696 130.9750
#> 10 48.3428 23.9220      NA      NA      NA       NA  12.6909 27.3317  27.9201
#> 11      NA      NA  3.2424 26.0804 27.8385   4.9901  12.6731 27.1006  25.8006
#> 12      NA      NA  3.4270 27.5657 29.4239   5.2743       NA  1.5062  16.2706
#> 13 13.6645      NA      NA      NA 17.9049  56.7099  47.7075 25.9498   9.5041
#> 14      NA      NA  3.2423 26.0798 27.8378   4.9900  13.5956 28.9383  26.2195
#> 15 42.0351 42.8516 38.4884 38.3243 27.7333   4.9713       NA      NA       NA
#>        17.8      20    22.4    25.1    28.2    31.6    35.5    39.8    44.7
#> 1   31.7928 46.4774 36.0224 18.6386 13.0679 35.9371 51.9935 54.4311 53.0410
#> 2   59.7055 12.2289 16.9542 36.4415 33.8670 47.0699 51.0359 41.4634 26.9486
#> 3  119.7010 90.8127 61.0917 34.4429 29.9356 40.6233 55.6097 63.4554 37.9612
#> 4   31.7849 46.4523 36.0501 19.6984 21.4516 19.3537 16.5903 25.0324 79.0379
#> 5    6.6897 22.5384 35.4089 45.3871 43.1186 43.7014 40.1062 31.5486 21.7715
#> 6   45.7484 28.6416 18.9482 19.8402 73.5207 68.2062 45.2565 22.3662 18.4290
#> 7   33.8673 46.8464 30.9061  9.8942 21.5677 31.7276 35.9930 34.5536 31.8643
#> 8  106.2420 43.1707 27.9162 30.7409 43.3770 43.2534 39.6858 33.8770 21.3698
#> 9  145.5270 49.7911 56.6048 85.4519 25.0655 19.2632 23.5568 27.0408 34.8329
#> 10  33.5976 46.2428 39.8575 30.4559 44.6790 35.2641 27.3170 24.3606 15.2732
#> 11  39.8077 74.7724 53.3682 21.3346 48.1121 42.2620 34.4152 32.5040 34.3353
#> 12  39.3416 73.7854 72.6627 55.8086 16.6107 24.1808 38.2818 44.5786 33.6961
#> 13  19.0300 64.1147 44.9746 11.2301 28.0656 19.0042 13.7922 20.6308 38.9528
#> 14  38.9998 73.0007 81.8211 76.2519 33.4393 24.3980 23.0905 28.2888 59.1289
#> 15   5.8215 19.6134 33.5911 47.3969 59.6466 46.1045 40.8987 42.0651 13.3468
#>       50.1    56.2    63.1    70.8    79.4    89.1     100     112     126
#> 1  40.0468 43.5029 52.6924 39.3050 48.8967 43.3303 38.3508 54.6351 46.7352
#> 2  37.1779 44.0252 49.4049 65.0651 46.7264 45.2082 48.8480 40.4787 46.4758
#> 3  50.4268 46.4923 36.2315 54.3585 49.4888 45.1408 38.0173 22.9942 36.6337
#> 4  51.1823 38.7372 44.1332 43.7420 34.3306 31.7308 37.2351 50.7097 42.5168
#> 5  44.5809 51.6841 43.7916 36.2606 27.9514 28.2883 34.6462 43.0303 44.9787
#> 6  36.3427 41.5861 38.8749 47.1001 29.0442 30.3578 36.6450 27.9695 35.5918
#> 7  36.6182 38.0569 39.8943 52.2343 50.1886 44.1894 38.2258 36.0945 25.5007
#> 8  42.5351 38.2826 19.3220 22.9579 29.4331 32.9848 33.0128 29.4228 31.9359
#> 9  29.0427 25.1621 27.4583 39.4630 32.2414 27.8820 29.2833 37.8217 25.3488
#> 10 19.8549 26.5427 27.7756 12.6823 37.2010 43.0968 40.1104 43.7962 25.8444
#> 11 33.9550 39.9368 49.0333 52.6407 44.0904 38.7997 35.6740 32.2359 24.9684
#> 12 34.2008 37.1471 39.8737 39.9601 35.8286 31.0635 28.8427 32.4794 35.3190
#> 13 27.5695 23.1070 28.1246 36.8126 28.9544 33.2718 38.2688 28.6821 29.7079
#> 14 42.0857 36.8696 39.6444 25.8842 32.8497 36.1407 37.7024 41.4387 28.5689
#> 15 43.0356 49.3431 35.0365 29.0553 32.2577 31.2226 30.9472 38.0439 34.9797
#>        141     158     178     200     224     251     282      316    355
#> 1  39.7747 31.9676 22.7002 20.5556 14.6333 10.3792  9.0294  3.71940 5.6306
#> 2  35.2515 27.4407 35.5825 23.6979 16.2016 10.9707  6.6221  0.99185 1.5816
#> 3  34.4225 27.8007 24.8582 17.6864 11.7393  8.1462  7.0668  6.55050 4.1073
#> 4  31.1217 20.4057 14.2551 24.0617 11.4046  2.0622  5.2825  6.79570 9.6893
#> 5  30.2593 16.4082 15.2468  9.5669 10.3725  9.5673  5.7082 11.11240 6.9275
#> 6  34.9339 28.6995 19.8471 17.8737 16.4837 11.6474  4.2196  8.77360 3.5631
#> 7  26.0659 24.7196 13.8197 19.7770 13.2250  6.9745  5.9847  5.11850 8.6477
#> 8  32.6255 30.2158 23.9634 15.7911  5.8193  3.2482  8.8735  4.15180 5.4305
#> 9  22.9861 22.2366 17.4725 18.7025 16.1652 12.4852  8.6583  3.52880 4.4293
#> 10 24.3560 22.4200 10.0417 20.6008 13.1950  7.3466  8.9603  1.34210 5.4089
#> 11 23.3258 21.5847 16.3410 18.6464 10.9306  7.9601 12.6641  4.16360 3.8995
#> 12 33.5880 28.6086 21.3408 10.5241  5.3978  4.4892  6.5719  8.70240 8.5122
#> 13 27.3134 23.7373 20.6181 16.9903 14.4182 10.7492  6.2512  7.69300 5.5785
#> 14 22.2631 18.0952 13.3598 16.2618  9.9228  5.8700  7.6590  5.79900 7.4388
#> 15 33.7992 26.8429 10.8079 14.0431  9.9000  5.6324  4.1744  3.29340 7.1790
#>        398     447     501     562     631     708 794 891 1000
#> 1  5.70080 3.78080 6.82460 4.09490 2.65520 2.70510  NA  NA   NA
#> 2  3.58300 5.13600 4.58130 5.64560 3.47130      NA  NA  NA   NA
#> 3  3.34040 4.53440 5.60340 3.32020 3.73590 6.45430  NA  NA   NA
#> 4  7.78400 2.93850 5.77990 3.12780 2.82310 4.76170  NA  NA   NA
#> 5  2.36860 1.44810 8.69790 4.16480 0.90277      NA  NA  NA   NA
#> 6  0.19951 1.37060 5.67150 3.84130 2.66650 2.54660  NA  NA   NA
#> 7  7.53680 2.44680 2.74010 3.89650 3.54460 2.00030  NA  NA   NA
#> 8  5.04130 2.25910 1.16640 0.51551 2.21990 5.61970  NA  NA   NA
#> 9  3.43270 0.25187 0.49792 2.87020 2.64610 0.30938  NA  NA   NA
#> 10 6.04940 2.36330 3.98580 1.92850 2.32170 4.86460  NA  NA   NA
#> 11 4.62340 4.82690 5.90090 2.32940 0.31675      NA  NA  NA   NA
#> 12 6.63330 4.45640 5.64270 3.24180 1.13960      NA  NA  NA   NA
#> 13 2.89960 1.34290 3.53350 1.22250 0.58736 1.60780  NA  NA   NA
#> 14 5.94830 2.23880 4.46180 1.52390      NA      NA  NA  NA   NA
#> 15 7.64670 4.34770 2.91450 2.96430 1.72050      NA  NA  NA   NA
#>  [ reached 'max' / getOption("max.print") -- omitted 7625 rows ]

dim(data)
#> [1] 7640   65
names(data)[1:6]
#> [1] "date"      "tconc"     "TPNC"      "monthyear" "1"         "1.12"     

# Per-column missing fraction, ordered by severity.
# The smallest and largest size bins have the most missing data.
na_frac <- sort(colMeans(is.na(data)), decreasing = TRUE)
head(na_frac, 10)
#>    1 1.12 1.26 1.41 1.58 1.78    2 2.24 2.51  794 
#>    1    1    1    1    1    1    1    1    1    1 

# The 60 size bins run from column 5 to column 65.
size_bins <- 5:65
summary(data[, size_bins[1:3]])
#>        1             1.12           1.26     
#>  Min.   : NA    Min.   : NA    Min.   : NA   
#>  1st Qu.: NA    1st Qu.: NA    1st Qu.: NA   
#>  Median : NA    Median : NA    Median : NA   
#>  Mean   :NaN    Mean   :NaN    Mean   :NaN   
#>  3rd Qu.: NA    3rd Qu.: NA    3rd Qu.: NA   
#>  Max.   : NA    Max.   : NA    Max.   : NA   
#>  NA's   :7640   NA's   :7640   NA's   :7640  
```
