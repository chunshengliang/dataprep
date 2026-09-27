# Validate data against a set of rules

Checks whether data conforms to user-specified rules such as column
type, value range, uniqueness, and presence of missing values. Returns a
report indicating which rules passed and which failed.

## Usage

``` r
validate_data(data, rules, verbose = FALSE)
```

## Arguments

- data:

  A data frame to validate.

- rules:

  A list of rules. Each rule is a list with components: `column` (column
  name), `type` (one of "numeric", "integer", "character", "factor"),
  `min`, `max`, `unique` (logical), and `na_allowed` (logical).

- verbose:

  Logical; if `TRUE`, prints progress message.

## Value

A data frame with columns: `rule`, `column`, `passed` (logical), and
`message`. If a rule passes, `message` is "OK"; otherwise it contains a
description of the failure.

## Examples

``` r
rules <- list(
  list(column = "3.98", type = "numeric", min = 0, na_allowed = TRUE),
  list(column = "monthyear", type = "character", unique = FALSE)
)
validate_data(data[1:50, c(1, 4, 17:19)], rules)
#>   rule    column passed message
#> 1    1      3.98   TRUE      OK
#> 2    2 monthyear   TRUE      OK
```
