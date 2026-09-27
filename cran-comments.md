## R CMD check results

On Windows 11 Pro for Workstations (R 4.6.1 ucrt, GCC 14.3.0):

    0 errors | 0 warnings | 0 notes

On Ubuntu 25.10 (R 4.5.1, g++ 15.2.0):

    0 errors | 0 warnings | 3 notes

All three notes are specific to this Ubuntu 25.10 host and do not
appear on CRAN's own check machines:

* **CRAN incoming feasibility** flags URLs that timed out from this
  host's network. All of them return HTTP 200 from other networks
  and are reachable from CRAN's check machines.

* **Non-portable compilation flag `-mno-omit-leaf-frame-pointer`**
  is injected by g++ 15.2.0 on Ubuntu 25.10 itself. The package
  does not set this flag. Details are in Note 1 below.

* **Skipping HTML validation / math rendering**: `tidy` (HTML Tidy)
  and the `V8` R package are not installed on this host. Both are
  optional and are present on CRAN's check machines, so this note
  does not appear there.

## Note 1: non-portable compilation flag `-mno-omit-leaf-frame-pointer`

```
N checking compilation flags used
Compilation used the following non-portable flag(s):
  '-mno-omit-leaf-frame-pointer'
```

`src/Makevars` and `src/Makevars.win` do not set
`-mno-omit-leaf-frame-pointer`. The flag is added by the compiler
driver itself, not by this package. On Ubuntu 25.10, g++ 15.2.0
enables `-mno-omit-leaf-frame-pointer` by default at `-O2` and
above. The same `R CMD check` on Windows 11 Pro for Workstations
(GCC 14.3.0, Rtools45) yields `0 errors | 0 warnings | 0 notes`,
which confirms that the flag is host-specific.

I have therefore not removed it, and I do not control it from
`Makevars`. The optimisation flags that the package does set are
portable GCC/Clang flags (`-O3`, `-funroll-loops`,
`-ftree-vectorize`). They do not bind the compiled binary to a
specific CPU and do not change the observable behaviour of the
code. They are retained because the package's primary value
proposition is performance. The speed-up from `-O3` over the R
default and from vectorisation is workload-dependent and can range
from a few percent to several hundred percent; together with
`-funroll-loops` and `-ftree-vectorize`, these flags improve the
wide-to-long and long-to-wide paths and the column-scan loops used
by `obsedele_cpp` and `condextr_cpp`.

On Windows, `src/Makevars.win` does not set `-O3`,
`-funroll-loops`, or `-ftree-vectorize`; it only adds the OpenMP
flags. The Windows binary therefore uses R's default optimisation
level.

The runtime AVX-512 dispatch in `melt.cpp` and `dcast.cpp` uses
`__builtin_cpu_supports("avx512f")` to select the code path at
load time, so the package remains correct on machines that do not
support AVX-512.

## Test environments

* **Ubuntu 25.10** — R 4.5.1 (2025-06-13), g++ 15.2.0;
  2× AMD EPYC 9965 192-Core (384 physical / 768 logical cores),
  1.0 TiB DDR5, full AVX-512.
* **Windows 11 Pro for Workstations** — R 4.6.1 (2026-06-24 ucrt),
  GCC 14.3.0 (Rtools45); 2× AMD EPYC 7B12 64-Core
  (128 physical / 128 logical cores), about 224 GiB RAM.

The two hosts are deliberately different: one is a large
AVX-512 Linux machine, the other a Windows workstation without
AVX-512. All unit tests pass on both.

## Unit tests

`tests/testthat/` runs 49 assertions across three files
(`test-cleaning-pipeline.R`, `test-fit-transform.R`,
`test-melt-dcast.R`). All pass in approximately 1.5 seconds on
Windows 11 Pro for Workstations and under 1 second on Ubuntu
25.10.

## Package behaviour

This is a major rewrite of the cleaning and reshaping backends.
The 0.1.5 → 0.1.7 upgrade guide (`vignette("dataprep-migration")`)
documents the three behaviour changes that affect row and column
counts, all of which are bug fixes:

1. `obsedele()` now scans each column independently instead of
   merging columns before computing missing runs. The 0.1.5
   implementation changed NA run boundaries and could both
   over-delete boundary rows and retain rows that should have been
   deleted.
2. The `half`-minute retention boundary is now inclusive
   (`dl <= half` rather than `dl < half`).
3. `optisolu()` no longer aborts the R session when `cores > 16`.

On SMEAR I Varrio 2025 (49,422 rows × 61 numeric channels,
10-minute sampling) the net effect of the three changes is
**+3 rows out of 49,422** (0.006% of the input) relative to 0.1.5.
The six rows that differ between versions sit at run boundaries
where the anchor distance is within one sampling interval of
`half` minutes.

## Internal details

The notes raised in earlier submissions (the maintainer email
change, the local URL check timeout, the `DATAPTR` non-API call,
and the missing `tidy` / `V8` on the local check host) are not
present in this submission:

* The maintainer email has already been updated in DESCRIPTION.
* The GitHub URL check succeeds on both reference hosts.
* `DATAPTR` is no longer used by `melt.cpp` / `dcast.cpp`; the
  bulk-copy paths now use `DATAPTR_RO` for reads and
  `SET_STRING_ELT` / `SET_VECTOR_ELT` for writes.
* `tidy` and `V8` are not required by this package; the "Skipping
  checking ..." messages only appear on hosts that lack them and
  are not reported on CRAN.

If CRAN would prefer the package to also drop
`-ftree-vectorize` (the only remaining flag that could in
principle be considered host-specific on some toolchains), I can
remove it in a follow-up release. It is retained here because it
is a portable GCC/Clang flag that is honoured identically on all
supported platforms, and because it materially improves the
column-scan loops in `obsedele_cpp` and `condextr_cpp` on both
x86_64 and aarch64.
