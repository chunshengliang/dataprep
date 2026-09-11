// ==================== obsedele.cpp ====================
#include <Rcpp.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <climits>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace Rcpp;

inline bool same_value(double a, double b) {
    return (R_IsNA(a) && R_IsNA(b)) || (!R_IsNA(a) && !R_IsNA(b) && std::fabs(a - b) < 1e-12);
}

inline bool time_equal_cpp(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

// Thread‑local buffers reused across group/period iterations
static thread_local std::vector<int>    tl_orig_at_row;
static thread_local std::vector<char>   tl_invalid_rows;   // 1 = row will be removed
static thread_local std::vector<double> tl_col_vec;

// [[Rcpp::export]]
LogicalVector obsedele_cpp(NumericVector time_sec,
                           IntegerVector group_int,
                           NumericMatrix x,
                           double step_sec,
                           double half,
                           double threshold_sec,
                           int n_threads = 0) {
#ifdef _OPENMP
    if (n_threads > 0) omp_set_num_threads(n_threads);
#endif

    int n = x.nrow();
    int p = x.ncol();
    std::vector<int> keep(n, 0);

    // Group mapping using an array (faster than unordered_map)
    int max_gid = 0;
    for (int i = 0; i < n; ++i) if (group_int[i] > max_gid) max_gid = group_int[i];
    std::vector<int> gid2idx(max_gid + 1, -1);
    std::vector<int> remap(n);
    int G = 0;
    for (int i = 0; i < n; ++i) {
        int g = group_int[i];
        if (g == 0) {
            remap[i] = -1;   // no group
        } else {
            if (gid2idx[g] == -1) gid2idx[g] = G++;
            remap[i] = gid2idx[g];
        }
    }

    std::vector<std::vector<int>> group_rows(G);
    if (G > 0) {
        std::vector<int> counts(G, 0);
        for (int i = 0; i < n; ++i) if (remap[i] >= 0) ++counts[remap[i]];
        for (int g = 0; g < G; ++g) group_rows[g].reserve(counts[g]);
        for (int i = 0; i < n; ++i) if (remap[i] >= 0) group_rows[remap[i]].push_back(i);
    }

    auto process_subset = [&](const std::vector<int>& sub_idx) -> void {
        int m = sub_idx.size();
        if (m == 0) return;

        // Determine time range
        double t_min = time_sec[sub_idx[0]];
        double t_max = time_sec[sub_idx[0]];
        for (int idx : sub_idx) {
            double t = time_sec[idx];
            if (t < t_min) t_min = t;
            if (t > t_max) t_max = t;
        }

        long long L_ll = (long long)std::llround((t_max - t_min) / step_sec) + 1;
        int L = (int)L_ll;
        if (L <= 0 || L_ll > INT_MAX) return;

        // Allocate thread‑local buffers (reused across calls)
        std::vector<int>&    orig_at_row = tl_orig_at_row;
        std::vector<char>&   invalid_rows = tl_invalid_rows;
        std::vector<double>& col_vec      = tl_col_vec;

        orig_at_row.assign(L, -1);
        invalid_rows.assign(L, 0);

        // Map observed time points to grid rows
        for (int k = 0; k < m; ++k) {
            int orig_idx = sub_idx[k];
            double t_obs = time_sec[orig_idx];
            long long row_ll = (long long)std::llround((t_obs - t_min) / step_sec);
            if (row_ll < 0 || row_ll >= L) continue;
            int row = (int)row_ll;
            double t_grid = t_min + (double)row * step_sec;
            if (!time_equal_cpp(t_grid, t_obs)) continue;
            orig_at_row[row] = orig_idx;
        }

        // Determine global start_row and end_row by examining each column
        int start_row = 0;
        int end_row   = L;
        bool any_col_with_na = false;

        for (int j = 0; j < p; ++j) {
            // Fill column vector with observed values (NA elsewhere)
            col_vec.assign(L, NA_REAL);
            const double* x_col = x.begin() + j * n;
            for (int i = 0; i < L; ++i) {
                int orig = orig_at_row[i];
                if (orig >= 0) col_vec[i] = x_col[orig];
            }

            // Find first and last non‑NA positions
            int first = -1, last = -1;
            for (int i = 0; i < L; ++i) {
                if (!R_IsNA(col_vec[i])) {
                    if (first < 0) first = i;
                    last = i;
                }
            }
            if (first < 0) return;   // all NA in this column -> discard whole group

            // Adjust start_row if leading NAs exceed half
            if (first >= (int)half) {
                int candidate = first - (int)half;
                if (candidate > start_row) start_row = candidate;
            }
            // Adjust end_row if trailing NAs exceed half
            if (last < L - (int)half) {
                int candidate = last + (int)half;
                if (candidate < end_row) end_row = candidate;
            }
        }

        if (end_row <= start_row) return;

        // Now process each column within [start_row, end_row) to mark invalid rows
        for (int j = 0; j < p; ++j) {
            col_vec.assign(L, NA_REAL);
            const double* x_col = x.begin() + j * n;
            for (int i = start_row; i < end_row; ++i) {
                int orig = orig_at_row[i];
                if (orig >= 0) col_vec[i] = x_col[orig];
            }

            // Scan NA runs inside the trimmed interval
            int i = start_row;
            while (i < end_row) {
                if (!R_IsNA(col_vec[i])) {
                    ++i;
                    continue;
                }
                // Found an NA run
                int run_start = i;
                while (i < end_row && R_IsNA(col_vec[i])) ++i;
                int run_end = i - 1;          // inclusive
                int run_len = run_end - run_start + 1;

                // Mark positions that are farther than 'half' from both ends
                for (int pos = run_start; pos <= run_end; ++pos) {
                    int dist_left  = pos - run_start + 1;
                    int dist_right = run_end - pos + 1;
                    if (dist_left > half && dist_right > half) {
                        invalid_rows[pos] = 1;
                    }
                }
            }
        }

        // Keep rows that are not invalid and have an actual observation
        for (int i = start_row; i < end_row; ++i) {
            if (invalid_rows[i] == 0 && orig_at_row[i] >= 0) {
                keep[orig_at_row[i]] = 1;
            }
        }
    };

    if (G == 0) {
        // No group: split by time gaps
        std::vector<int> period(n);
        period[0] = 0;
        for (int i = 1; i < n; ++i) {
            double diff = time_sec[i] - time_sec[i - 1];
            if (diff > threshold_sec || time_equal_cpp(diff, 0.0))
                period[i] = period[i - 1] + 1;
            else
                period[i] = period[i - 1];
        }
        int num_periods = period[n - 1] + 1;
        std::vector<std::vector<int>> period_map(num_periods);
        for (int i = 0; i < n; ++i) period_map[period[i]].push_back(i);

        #pragma omp parallel for schedule(dynamic) if(num_periods > 1)
        for (int k = 0; k < num_periods; ++k)
            process_subset(period_map[k]);
    } else {
        #pragma omp parallel for schedule(dynamic) if(G > 1)
        for (int g = 0; g < G; ++g)
            process_subset(group_rows[g]);
    }

    LogicalVector result(n);
    for (int i = 0; i < n; ++i) result[i] = keep[i] != 0;
    return result;
}