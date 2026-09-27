# Clean and standardize character columns

Applies common string cleaning operations to character or factor
columns: trimming whitespace, changing case, and applying regular
expression substitutions. Numeric and other non-character columns are
left untouched.

## Usage

``` r
clean_strings(
  data,
  cols = NULL,
  trim = FALSE,
  tolower = FALSE,
  toupper = FALSE,
  pattern = NULL,
  replacement = NULL,
  verbose = FALSE
)
```

## Arguments

- data:

  A data frame, matrix, or character vector.

- cols:

  Columns to clean. If `NULL` (default), all character and factor
  columns are selected. Columns that are neither character nor factor
  are skipped, so the numeric and logical columns of `data` are never
  silently coerced to character.

- trim:

  Logical; if `TRUE`, leading and trailing whitespace is removed with
  [`trimws`](https://rdrr.io/r/base/trimws.html).

- tolower:

  Logical; if `TRUE`, converted to lowercase.

- toupper:

  Logical; if `TRUE`, converted to uppercase.

- pattern:

  Optional regular expression passed to
  [`gsub`](https://rdrr.io/r/base/grep.html).

- replacement:

  Replacement string for `pattern`. Defaults to `""` (deletion) when
  `pattern` is supplied and `replacement` is `NULL`.

- verbose:

  Logical; if `TRUE`, prints timing message.

## Value

A data frame (or character vector, if the input was a vector) with the
selected columns cleaned.

## Details

The operations are applied in a fixed order:

1.  `trim` (via `trimws`),

2.  `tolower` then `toupper`,

3.  `pattern` replacement (via `gsub`).

When both `tolower` and `toupper` are `TRUE`, the uppercase conversion
wins (it is applied last). In practice only one of the two should be
set.

For factor columns, the underlying integer codes are dropped and the
column becomes a character vector. If you need to keep the factor type,
convert the cleaned values back with
[`factor()`](https://rdrr.io/r/base/factor.html).

## Examples

``` r
df <- data.frame(
  id   = 1:3,
  name = c("  Alice ", "BOB", "Charlie "),
  city = c("New York", "london", "Paris"),
  stringsAsFactors = FALSE
)

# Only the character columns are touched by default
clean_strings(df, trim = TRUE, tolower = TRUE)
#>   id    name     city
#> 1  1   alice new york
#> 2  2     bob   london
#> 3  3 charlie    paris

# Explicit column selection
clean_strings(df, cols = "name", trim = TRUE, toupper = TRUE)
#>   id    name     city
#> 1  1   ALICE New York
#> 2  2     BOB   london
#> 3  3 CHARLIE    Paris

# Regex replacement
clean_strings(df, cols = "city",
              pattern = "\\s+", replacement = "_")
#>   id     name     city
#> 1  1   Alice  New_York
#> 2  2      BOB   london
#> 3  3 Charlie     Paris

# Vector input
clean_strings(c(" A ", " b ", "C"), trim = TRUE, tolower = TRUE)
#> [1] "a" "b" "c"
```
