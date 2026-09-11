// File: src/transform_elem.cpp
#include <Rcpp.h>
#include <cmath>
using namespace Rcpp;

// [[Rcpp::export]]
NumericVector transform_elem_cpp(NumericVector x, std::string method, double lambda = 1.0) {
  int n = x.size();
  NumericVector y(n);
  for (int i = 0; i < n; ++i) {
    if (NumericVector::is_na(x[i])) {
      y[i] = NA_REAL;
      continue;
    }
    double v = x[i];
    if (method == "log") {
      y[i] = (v > 0) ? log(v) : NA_REAL;
    } else if (method == "log1p") {
      y[i] = log1p(v);
    } else if (method == "sqrt") {
      y[i] = (v >= 0) ? sqrt(v) : NA_REAL;
    } else if (method == "inverse") {
      y[i] = (v != 0) ? 1.0 / v : NA_REAL;
    } else if (method == "boxcox") {
      if (v > 0) {
        y[i] = (std::pow(v, lambda) - 1.0) / lambda;
      } else {
        y[i] = NA_REAL;
      }
    } else if (method == "yeojohnson") {
      if (v >= 0) {
        y[i] = (lambda != 0) ? (std::pow(v + 1.0, lambda) - 1.0) / lambda : log1p(v);
      } else {
        y[i] = (lambda != 2) ? -(std::pow(-v + 1.0, 2.0 - lambda) - 1.0) / (2.0 - lambda) : -log1p(-v);
      }
    } else {
      y[i] = v;
    }
  }
  return y;
}