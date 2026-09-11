// ==================== condextr_all_cpp.cpp ====================
#include <Rcpp.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <climits>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace Rcpp;

// -------------------- Local thread-safe helper functions (static) --------------------
static thread_local std::vector<double> scratch_vals;

static inline bool same_value(double a, double b) {
    return (R_IsNA(a) && R_IsNA(b)) || (!R_IsNA(a) && !R_IsNA(b) && std::fabs(a - b) < 1e-12);
}

static inline double quantile_nth(std::vector<double>& vals, double p) {
    int n = vals.size();
    if (n == 0) return NA_REAL;
    double index = (n - 1.0) * p;
    int lo = (int)std::floor(index);
    int hi = (int)std::ceil(index);
    double h = index - lo;
    // Copy to avoid modifying original
    std::vector<double> copy = vals;
    std::nth_element(copy.begin(), copy.begin() + lo, copy.end());
    double vlo = copy[lo];
    if (lo == hi) return vlo;
    std::nth_element(copy.begin(), copy.begin() + hi, copy.end());
    double vhi = copy[hi];
    return vlo + h * (vhi - vlo);
}

static inline void mark_outliers_inplace(double* y, int n,
                                         double top, double toperr, double topmag,
                                         double bottom, double boterr, double botmag,
                                         bool use_threshold_error) {
    scratch_vals.clear();
    scratch_vals.reserve(n);

    double min_val = std::numeric_limits<double>::max();
    double max_val = -std::numeric_limits<double>::max();

    for (int i = 0; i < n; ++i) {
        if (!ISNAN(y[i])) {
            const double v = y[i];
            scratch_vals.push_back(v);
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
        }
    }
    if (scratch_vals.empty()) return;

    double top_quant = quantile_nth(scratch_vals, top);
    double bottom_quant = quantile_nth(scratch_vals, bottom);

    if (use_threshold_error) {
        double thresh_top = top_quant * (1 + toperr) +
                            pow(10, floor(log10(top_quant))) * topmag;
        double thresh_bottom = bottom_quant * (1 - boterr) -
                               pow(10, floor(log10(bottom_quant))) * botmag;

        if (max_val > thresh_top) {
            for (int i = 0; i < n; ++i) {
                if (!ISNAN(y[i]) && same_value(y[i], max_val)) y[i] = NA_REAL;
            }
        }
        if (min_val < thresh_bottom) {
            for (int i = 0; i < n; ++i) {
                if (!ISNAN(y[i]) && same_value(y[i], min_val)) y[i] = NA_REAL;
            }
        }
    } else {
        for (int i = 0; i < n; ++i) {
            if (ISNAN(y[i])) continue;
            if (y[i] > top_quant || y[i] < bottom_quant) y[i] = NA_REAL;
        }
    }
}

// -------------------- Forward declaration of obsedele_cpp --------------------
LogicalVector obsedele_cpp(NumericVector time_sec, IntegerVector group_int,
                           NumericMatrix x, double step_sec, double half,
                           double threshold_sec, int n_threads = 0);

// [[Rcpp::export]]
LogicalVector condextr_all_cpp(
    NumericVector time_sec,
    IntegerVector group_int,
    NumericMatrix x,
    double step_sec,
    double half,
    double threshold_sec,
    double top, double toperr, double topmag,
    double bottom, double boterr, double botmag,
    int interval, int times,
    int n_threads = 0)
{
    int n = x.nrow();
    int p = x.ncol();

    // Compress group IDs (0 = no group)
    std::unordered_map<int, int> gid2idx;
    IntegerVector compressed_group(n);
    int G = 0;
    bool has_group = false;
    for (int i = 0; i < n; ++i) {
        int g = group_int[i];
        if (g != 0) {
            has_group = true;
            auto it = gid2idx.find(g);
            if (it == gid2idx.end()) {
                gid2idx[g] = G++;
                compressed_group[i] = gid2idx[g];
            } else {
                compressed_group[i] = it->second;
            }
        } else {
            compressed_group[i] = 0;
        }
    }

    NumericMatrix cur = clone(x);
    NumericVector cur_time = clone(time_sec);
    IntegerVector cur_group = clone(compressed_group);
    std::vector<int> alive(n);
    for (int i = 0; i < n; ++i) alive[i] = i;
    int cur_n = n;

    // Mark outliers using in-place modifications
    auto mark_round = [&](NumericMatrix& mat, IntegerVector& grp) {
        if (!has_group) {
            // Process each column directly without copying
            for (int c = 0; c < p; ++c) {
                double* col_ptr = mat.begin() + c * mat.nrow();
                mark_outliers_inplace(col_ptr, mat.nrow(),
                                      top, toperr, topmag,
                                      bottom, boterr, botmag, true);
            }
        } else {
            // Group-wise processing: extract each group's column into a temporary buffer,
            // modify it, and write back.
            for (int g = 0; g < G; ++g) {
                std::vector<int> rows;
                for (int r = 0; r < cur_n; ++r)
                    if (grp[r] == g) rows.push_back(r);
                if (rows.empty()) continue;
                int m = rows.size();
                for (int c = 0; c < p; ++c) {
                    std::vector<double> col_vals(m);
                    for (int rr = 0; rr < m; ++rr)
                        col_vals[rr] = mat(rows[rr], c);
                    mark_outliers_inplace(col_vals.data(), m,
                                          top, toperr, topmag,
                                          bottom, boterr, botmag, true);
                    for (int rr = 0; rr < m; ++rr)
                        mat(rows[rr], c) = col_vals[rr];
                }
            }
        }
    };

    // Main loop
    for (int round = 0; round < times; ++round) {
        for (int rep = 0; rep < interval; ++rep) {
            mark_round(cur, cur_group);
        }

        LogicalVector keep_cur = obsedele_cpp(cur_time, cur_group, cur,
                                              step_sec, half, threshold_sec,
                                              n_threads);

        int new_n = 0;
        for (int r = 0; r < cur_n; ++r)
            if (keep_cur[r]) new_n++;

        if (new_n == 0) {
            cur_n = 0;
            break;
        }

        NumericMatrix newmat(new_n, p);
        NumericVector newtime(new_n);
        IntegerVector newgroup(new_n);
        std::vector<int> new_alive(new_n);

        // Column-wise copy (cache friendly)
        for (int c = 0; c < p; ++c) {
            int pos = 0;
            for (int r = 0; r < cur_n; ++r) {
                if (keep_cur[r]) {
                    newmat(pos, c) = cur(r, c);
                    ++pos;
                }
            }
        }
        int pos2 = 0;
        for (int r = 0; r < cur_n; ++r) {
            if (keep_cur[r]) {
                newtime[pos2] = cur_time[r];
                newgroup[pos2] = cur_group[r];
                new_alive[pos2] = alive[r];
                ++pos2;
            }
        }

        cur = newmat;
        cur_time = newtime;
        cur_group = newgroup;
        alive = new_alive;
        cur_n = new_n;
    }

    // Final observation deletion
    if (cur_n > 0) {
        LogicalVector final_keep_cur = obsedele_cpp(cur_time, cur_group, cur,
                                                    step_sec, half, threshold_sec,
                                                    n_threads);
        int final_n = 0;
        for (int r = 0; r < cur_n; ++r)
            if (final_keep_cur[r]) final_n++;

        NumericMatrix final_mat(final_n, p);
        NumericVector final_time(final_n);
        IntegerVector final_group(final_n);
        std::vector<int> final_alive(final_n);

        for (int c = 0; c < p; ++c) {
            int pos = 0;
            for (int r = 0; r < cur_n; ++r) {
                if (final_keep_cur[r]) {
                    final_mat(pos, c) = cur(r, c);
                    ++pos;
                }
            }
        }
        int pos2 = 0;
        for (int r = 0; r < cur_n; ++r) {
            if (final_keep_cur[r]) {
                final_time[pos2] = cur_time[r];
                final_group[pos2] = cur_group[r];
                final_alive[pos2] = alive[r];
                ++pos2;
            }
        }

        cur = final_mat;
        cur_time = final_time;
        cur_group = final_group;
        alive = final_alive;
        cur_n = final_n;
    }

    LogicalVector result(n, false);
    for (int r = 0; r < cur_n; ++r)
        result[alive[r]] = true;

    return result;
}