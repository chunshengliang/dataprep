// File: src/shorvalu_fill.cpp
// [[Rcpp::plugins(openmp)]]
#include <Rcpp.h>
#include <cmath>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace Rcpp;

// [[Rcpp::export]]
void shorvalu_fill_cpp(NumericMatrix x, IntegerVector starts, IntegerVector lens,
                       int n_threads = 0) {
#ifdef _OPENMP
  if (n_threads > 0) omp_set_num_threads(n_threads);
#endif

  int p = x.ncol();
  int nseg = starts.size();

  #pragma omp parallel for schedule(dynamic)
  for (int s = 0; s < nseg; ++s) {
    int a = starts[s];
    int b = a + lens[s];

    for (int j = 0; j < p; ++j) {
      int first = -1, last = -1;

      for (int i = a; i < b; ++i) {
        if (!NumericVector::is_na(x(i, j))) {
          if (first < 0) first = i;
          last = i;
        }
      }
      if (first < 0) continue;

      for (int i = a; i < first; ++i) x(i, j) = x(first, j);
      for (int i = last + 1; i < b; ++i) x(i, j) = x(last, j);

      int cur = first;
      int i = first + 1;
      while (i <= last) {
        if (!NumericVector::is_na(x(i, j))) {
          cur = i;
          ++i;
          continue;
        }
        int start_na = i;
        while (i <= last && NumericVector::is_na(x(i, j))) ++i;
        if (i > last) break;
        int next_valid = i;
        double x0 = x(cur, j);
        double x1 = x(next_valid, j);
        for (int t = start_na; t < next_valid; ++t) {
          x(t, j) = x0 + (x1 - x0) * (double)(t - cur) / (double)(next_valid - cur);
        }
        cur = next_valid;
        i = next_valid + 1;
      }
    }
  }
}
