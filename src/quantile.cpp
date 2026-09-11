// ==================== quantile.cpp ====================
#include <Rcpp.h>
#include <vector>
#include <algorithm>

using namespace Rcpp;

// [[Rcpp::export]]
NumericVector quantile_cpp(NumericVector x, NumericVector probs) {
    std::vector<double> vals;
    vals.reserve(x.size());
    for (int i = 0; i < x.size(); ++i) if (!NumericVector::is_na(x[i])) vals.push_back(x[i]);
    int n = vals.size();
    if (n == 0) return NumericVector(probs.size(), NA_REAL);

    // 始终排序一次，简单可靠，性能可接受
    std::sort(vals.begin(), vals.end());
    NumericVector res(probs.size());
    for (int k = 0; k < probs.size(); ++k) {
        double p = probs[k];
        double index = (n - 1.0) * p;
        int lo = (int)std::floor(index);
        int hi = (int)std::ceil(index);
        double h = index - lo;
        res[k] = vals[lo] + h * (vals[hi] - vals[lo]);
    }
    return res;
}