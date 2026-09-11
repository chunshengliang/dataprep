#include <Rcpp.h>
#include <vector>
#include <algorithm>
using namespace Rcpp;

// [[Rcpp::export]]
IntegerVector bin_data_cpp(NumericVector x, NumericVector breaks, 
                           bool include_lowest = true) {
    int n = x.size();
    int nb = breaks.size();
    IntegerVector out(n, NA_INTEGER);
    
    // Sort breaks and remove duplicates
    std::vector<double> b = Rcpp::as<std::vector<double>>(breaks);
    std::sort(b.begin(), b.end());
    b.erase(std::unique(b.begin(), b.end()), b.end());
    int m = b.size();
    if (m < 2) return out; // not enough breaks
    
    for (int i = 0; i < n; ++i) {
        if (NumericVector::is_na(x[i])) continue;
        double val = x[i];
        // Binary search for interval
        int lo = 0, hi = m - 1;
        int idx = -1;
        if (include_lowest) {
            // lower bound inclusive: val >= b[0]
            if (val < b[0]) continue;
            if (val > b[m-1]) continue;
            // find first break > val
            auto it = std::upper_bound(b.begin(), b.end(), val);
            idx = std::distance(b.begin(), it) - 1;
        } else {
            // lower bound exclusive: val > b[0]?
            if (val <= b[0]) continue;
            if (val > b[m-1]) continue;
            auto it = std::lower_bound(b.begin(), b.end(), val);
            // if val equals a break, we need to exclude it, so use upper_bound if equal?
            // For exclusive, we need val > lower and val <= upper. 
            // Simpler: find first break >= val, then if val == break, put it in previous? 
            // Actually typical cut with include.lowest=FALSE means intervals are (b[i], b[i+1]]
            // So we need first break >= val, then subtract 1.
            auto it2 = std::lower_bound(b.begin(), b.end(), val);
            if (it2 != b.end() && *it2 == val) {
                // val equals a break, so belongs to previous bin (since left open)
                idx = std::distance(b.begin(), it2) - 1;
            } else {
                idx = std::distance(b.begin(), it2) - 1;
            }
            if (idx < 0 || idx >= m-1) idx = -1;
        }
        if (idx >= 0 && idx < m-1) {
            out[i] = idx + 1; // 1-indexed bins
        }
    }
    return out;
}