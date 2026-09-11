// File: src/desc_stats.cpp
#include <Rcpp.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "utils.h"
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace Rcpp;

// [[Rcpp::plugins(openmp)]]
// [[Rcpp::export]]
NumericMatrix desc_stats_cpp(NumericMatrix x, IntegerVector stats_idx, int n_threads = 0) {
  int n = x.nrow();
  int p = x.ncol();
  int m = stats_idx.size();
  NumericMatrix out(p, m);
  rownames(out) = colnames(x);

  // Set number of threads for OpenMP
  int threads_used = n_threads;
#ifdef _OPENMP
  if (threads_used <= 0) threads_used = omp_get_max_threads();
#else
  threads_used = 1;
#endif

  #pragma omp parallel for schedule(dynamic) num_threads(threads_used) if(n * p > 100000 && n_threads != 0)
  for (int j = 0; j < p; ++j) {
    std::vector<double> vals;
    vals.reserve(n);
    int na_count = 0;

    for (int i = 0; i < n; ++i) {
      double v = x(i, j);
      if (!R_IsNA(v)) {
        vals.push_back(v);
      } else {
        na_count++;
      }
    }

    int n_obs = vals.size();
    if (n_obs == 0) {
      for (int k = 0; k < m; ++k) out(j,k) = NA_REAL;
      for (int k = 0; k < m; ++k) {
        int stat = stats_idx[k];
        if (stat == 1) out(j,k) = 0;
        if (stat == 2) out(j,k) = na_count;
      }
      continue;
    }

    // Single pass for sum and sum_sq
    double sum = 0.0, sum_sq = 0.0;
    for (double v : vals) {
      sum += v;
      sum_sq += v * v;
    }
    double mean_val = sum / n_obs;
    double sd_val = sqrt( (sum_sq - n_obs * mean_val * mean_val) / (n_obs - 1) );

    // Sort for median, quantiles, trimmed mean
    std::vector<double> sorted = vals;
    std::sort(sorted.begin(), sorted.end());

    int trim_n = std::max(1, (int)std::floor(n_obs * 0.1));
    double trim_sum = 0.0;
    for (int i = trim_n; i < n_obs - trim_n; ++i) trim_sum += sorted[i];
    double trimmed_mean = trim_sum / (n_obs - 2 * trim_n);

    double median_val = (n_obs % 2 == 0) ?
      (sorted[n_obs/2 - 1] + sorted[n_obs/2]) / 2.0 : sorted[n_obs/2];

    double min_val = sorted[0];
    double max_val = sorted[n_obs - 1];
    double q1 = quantile7_vec(sorted, 0.25);
    double q3 = quantile7_vec(sorted, 0.75);
    double iqr_val = q3 - q1;

    for (int k = 0; k < m; ++k) {
      int stat = stats_idx[k];
      switch(stat) {
        case 1: out(j,k) = n_obs; break;
        case 2: out(j,k) = na_count; break;
        case 3: out(j,k) = mean_val; break;
        case 4: out(j,k) = sd_val; break;
        case 5: out(j,k) = median_val; break;
        case 6: out(j,k) = trimmed_mean; break;
        case 7: out(j,k) = min_val; break;
        case 8: out(j,k) = max_val; break;
        case 9: out(j,k) = iqr_val; break;
      }
    }
  }
  return out;
}