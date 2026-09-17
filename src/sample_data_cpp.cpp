#include <Rcpp.h>
#include <vector>
#include <unordered_map>
#include <random>
#include <algorithm>
using namespace Rcpp;

// [[Rcpp::export]]
IntegerVector sample_data_cpp(IntegerVector group, int size_per_group,
                              bool replace = false, int seed = -1) {
    const int n = group.size();

    std::unordered_map<int, std::vector<int>> groups;
    for (int i = 0; i < n; ++i) {
        groups[group[i]].push_back(i);
    }

    std::vector<int> sampled;
    std::mt19937 rng;
    if (seed >= 0) {
        rng.seed((unsigned)seed);
    } else {
        std::random_device rd;
        rng.seed(rd());
    }

    for (auto& kv : groups) {
        auto& vec = kv.second;
        int m = (int)vec.size();
        // FIX: the previous version had a redundant second std::min call.
        int k = std::min(size_per_group, m);

        if (replace) {
            std::uniform_int_distribution<int> dist(0, m - 1);
            for (int i = 0; i < k; ++i) {
                sampled.push_back(vec[dist(rng)]);
            }
        } else {
            std::shuffle(vec.begin(), vec.end(), rng);
            for (int i = 0; i < k; ++i) {
                sampled.push_back(vec[i]);
            }
        }
    }

    IntegerVector out((R_xlen_t)sampled.size());
    for (size_t i = 0; i < sampled.size(); ++i) {
        out[(R_xlen_t)i] = sampled[i] + 1;  // 1-based for R
    }
    return out;
}
