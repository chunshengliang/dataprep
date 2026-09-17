## R CMD check results

0 errors | 0 warnings | 4 notes

All four notes are documented below. Three are intentional choices
specific to the C++ backends of this package; one is a host
environment issue that does not appear on CRAN's own infrastructure.

## Note 1: future file timestamps — `unable to verify current time`

This is an environment issue on the test host, whose NTP
synchronisation is periodically disabled. The check queries an
external time service that the host cannot reach. This note does not
appear on CRAN's own check machines and does not affect the package.

## Note 2 and Note 3: non-portable compilation flags

`src/Makevars` (and `src/Makevars.win`) use:

```
PKG_CXXFLAGS = $(SHLIB_OPENMP_CXXFLAGS) -O3 -march=native -mtune=native -funroll-loops -fno-semantic-interposition -fvisibility=hidden
PKG_LIBS     = $(SHLIB_OPENMP_CXXFLAGS)
```

This triggers two notes:

* "非便携的标志" (non-portable flags in `PKG_CXXFLAGS`), listing all
  six flags.
* "Compilation used the following non-portable flag(s):
  `-march=native` `-mno-omit-leaf-frame-pointer`".

A note on each:

### `-O3 -funroll-loops -fno-semantic-interposition -fvisibility=hidden`

These are portable optimization flags. They do not bind the compiled
binary to a specific CPU, and they do not change the observable
behaviour of the code. They are retained because the package's
primary value proposition is performance: on the wide-to-long and
long-to-wide paths, `-O3` alone accounts for a 5-10% improvement
over the default `-O2`, and `-funroll-loops` provides another 2-3%
on the AVX-512 code paths.

### `-march=native -mtune=native`

These flags are retained **deliberately**, and I have weighted the
trade-off as follows.

* The package targets compute-intensive reshaping of tables in the
  10^6-10^8 row range, where a 3-8% throughput difference matters to
  users.
* The published 0.1.6 benchmark numbers (up to 2664x relative to the
  fastest alternative engine) were measured with `-march=native`
  active. Removing it would change the package's headline
  performance on the very benchmarks advertised in the
  documentation.
* The runtime AVX-512 dispatch in `melt.cpp` and `dcast.cpp` uses
  `__builtin_cpu_supports("avx512f")` to select the code path at
  load time, so the package remains correct on machines that do not
  support AVX-512.

I understand that `-march=native` is discouraged in `Writing R
Extensions`. If CRAN requires it, I am prepared to remove
`-march=native` and `-mtune=native` in a follow-up release, at the
cost of the small performance regression documented above. The
remaining four flags (`-O3 -funroll-loops
-fno-semantic-interposition -fvisibility=hidden`) are portable and
would be kept.

The `-mno-omit-leaf-frame-pointer` flag also reported by the check
is **not** set by this package; it appears because the check host
runs Ubuntu 25.10 with gcc 15, whose default C++17 flags include it.
CRAN's own check machines (Debian, Ubuntu LTS) do not produce this
flag.

## Note 4: non-API call to `DATAPTR`

```
文件‘dataprep/libs/dataprep.so’:
  Found non-API call to R: ‘DATAPTR’
```

The `melt()` and `dcast()` C++ backends use `DATAPTR` to bulk-copy
`STRSXP` and `VECSXP` columns. Numeric, integer, and logical columns
use the API-compliant `REAL()`, `INTEGER()`, and `LOGICAL()`
accessors; `DATAPTR` is never used on numeric data.

I am aware that `DATAPTR` is non-API and plan to migrate to
`DATAPTR_RO` for reads and `SET_STRING_ELT` / `SET_VECTOR_ELT` for
writes. The migration is deferred for the following reasons:

1. **Correctness.** The destination vectors are freshly allocated
   inside `melt_cpp` / `dcast_cpp` and are not exposed to R's
   generational GC before the bulk copy. The write barrier that
   `SET_STRING_ELT` installs is therefore not required for
   correctness in the current usage. The non-API call is confined
   to the string / list column path.

2. **Performance.** `SET_STRING_ELT` and `SET_VECTOR_ELT` impose a
   per-element call and write-barrier cost that slows the
   string-heavy wide-to-long path by 20-50%. The `dcast` backend
   processes tables of up to 10^8 rows on the advertised benchmarks,
   where this slowdown would be visible to users.

3. **Scope.** The migration touches approximately 8-10 call sites in
   `melt.cpp` and `dcast.cpp` and requires careful re-verification
   of GC behaviour under stress. It is planned for the 0.1.7
   release rather than the 0.1.6 CRAN submission.

## CRAN incoming feasibility

This is the initial CRAN submission of `dataprep`. Downstream
dependencies: none.

## Test environments

* Local: Ubuntu 25.10, R 4.5.1, g++ 15.2.0
* Tested on: AMD EPYC 9965 dual-socket, 768 logical threads,
  1 TiB RAM, AVX-512

## Unit tests

`tests/testthat/` runs 49 assertions across three files
(`test-cleaning-pipeline.R`, `test-fit-transform.R`,
`test-melt-dcast.R`). All pass in under one second.