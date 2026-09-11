#include <Rcpp.h>
#include <vector>
#include <cmath>
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace Rcpp;

// [[Rcpp::export]]
LogicalVector filter_high_cor_cpp(NumericMatrix x, double cutoff, 
                                  bool keep_first = true) {
    int n = x.nrow();
    int p = x.ncol();
    LogicalVector keep(p, true);
    if (p < 2) return keep;
    
    // Compute correlation matrix (Pearson only; Spearman could be added later)
    // Use symmetric, compute upper triangle only
    std::vector<std::vector<double>> cor(p, std::vector<double>(p, 0.0));
    
    #pragma omp parallel for schedule(dynamic) if(p > 50)
    for (int i = 0; i < p; ++i) {
        NumericVector col_i = x(_, i);
        double mean_i = mean(col_i);
        double sd_i = sd(col_i);
        if (sd_i == 0) { // constant column, mark for removal
            keep[i] = false;
            continue;
        }
        for (int j = i+1; j < p; ++j) {
            NumericVector col_j = x(_, j);
            double mean_j = mean(col_j);
            double sd_j = sd(col_j);
            if (sd_j == 0) {
                keep[j] = false;
                continue;
            }
            // Compute Pearson correlation
            double sum_xy = 0.0;
            for (int k = 0; k < n; ++k) {
                double a = col_i[k] - mean_i;
                double b = col_j[k] - mean_j;
                sum_xy += a * b;
            }
            double cor_val = sum_xy / ((n-1) * sd_i * sd_j);
            cor[i][j] = cor_val;
        }
    }
    
    // Now decide which to remove based on correlation > cutoff
    for (int i = 0; i < p; ++i) {
        if (!keep[i]) continue;
        for (int j = i+1; j < p; ++j) {
            if (!keep[j]) continue;
            if (std::fabs(cor[i][j]) > cutoff) {
                if (keep_first) {
                    keep[j] = false;
                } else {
                    // keep the one with higher variance (or keep first? we'll implement keep_first only for now)
                    // Simple: if keep_first is false, we keep the one with larger sd (or keep j if variance of j > variance of i)
                    // But to keep simple, we'll just remove the one with smaller variance
                    double var_i = var(x(_, i));
                    double var_j = var(x(_, j));
                    if (var_i >= var_j) {
                        keep[j] = false;
                    } else {
                        keep[i] = false;
                    }
                }
            }
        }
    }
    return keep;
}