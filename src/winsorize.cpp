// File: src/winsorize.cpp
#include <Rcpp.h>
#include <vector>
#include <algorithm>
#include "utils.h"
using namespace Rcpp;

// [[Rcpp::export]]
NumericVector winsorize_cpp(NumericVector x, double top, double bottom) {
  std::vector<double> vals;
  for (int i = 0; i < x.size(); ++i) {
    if (!NumericVector::is_na(x[i])) vals.push_back(x[i]);
  }
  int n = vals.size();
  if (n == 0) return clone(x);
  std::sort(vals.begin(), vals.end());
  double top_quant = quantile7_vec(vals, top);
  double bottom_quant = quantile7_vec(vals, bottom);
  NumericVector y = clone(x);
  for (int i = 0; i < y.size(); ++i) {
    if (NumericVector::is_na(y[i])) continue;
    if (y[i] > top_quant) y[i] = top_quant;
    if (y[i] < bottom_quant) y[i] = bottom_quant;
  }
  return y;
}