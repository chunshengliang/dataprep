// File: src/na_runs.cpp
#include <Rcpp.h>
using namespace Rcpp;

// [[Rcpp::export]]
List na_runs_cpp(NumericVector x) {
  int n = x.size();
  int n_na = 0;
  int n_runs = 0;
  int max_run = 0;
  int current_run = 0;
  for (int i = 0; i < n; ++i) {
    if (NumericVector::is_na(x[i])) {
      n_na++;
      current_run++;
      if (current_run == 1) n_runs++;
      if (current_run > max_run) max_run = current_run;
    } else {
      current_run = 0;
    }
  }
  return List::create(
    Named("n_na") = n_na,
    Named("n_runs") = n_runs,
    Named("max_run") = max_run
  );
}