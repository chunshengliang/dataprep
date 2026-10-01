## R CMD check results

Ubuntu 25.10 (R 4.5.1, g++ 15.2.0):

    0 errors | 0 warnings | 4 notes

All four notes are specific to this local host and do not appear on
CRAN's own check machines:

* CRAN incoming feasibility — maintainer email change (already in
  DESCRIPTION) and two URLs that timed out from this host's network.
* `-mno-omit-leaf-frame-pointer` — injected by g++ 15.2.0 itself,
  not set by the package.
* Skipping HTML validation / math rendering — `tidy` and `V8` are
  not installed on this host.
* future file timestamps — local clock not verified.

The GitHub Actions `R-CMD-check` workflow passes on the standard
`r-lib/actions` matrix (Ubuntu release / oldrel-1 / devel, macOS,
Windows) with 0 notes on every platform.

## Resubmission after CRAN pretest feedback

* Title and Description now single-quote software names:
  'C++', 'OpenMP', 'SIMD', 'AVX2', 'AVX-512'.
* Removed clang-flagged unused symbols in `src/dcast.cpp`,
  `src/filter_high_cor_cpp.cpp`, `src/melt.cpp`. Source-level only;
  no behaviour or performance change.
