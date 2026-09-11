#include <Rcpp.h>
#include <vector>
#include <unordered_map>
#include <random>
#include <algorithm>
using namespace Rcpp;

// [[Rcpp::export]]
IntegerVector sample_data_cpp(IntegerVector group, int size_per_group, 
                              bool replace = false, int seed = -1) {
    int n = group.size();
    // Build groups
    std::unordered_map<int, std::vector<int>> groups;
    for (int i = 0; i < n; ++i) {
        groups[group[i]].push_back(i);
    }
    // Sample from each group
    std::vector<int> sampled;
    std::mt19937 rng;
    if (seed >= 0) rng.seed(seed);
    else {
        std::random_device rd;
        rng.seed(rd());
    }
    for (auto &kv : groups) {
        auto &vec = kv.second;
        int m = vec.size();
        int k = std::min(size_per_group, m);
        if (!replace) k = std::min(k, m);
        // Random sample without replacement (or with)
        if (replace) {
            std::uniform_int_distribution<int> dist(0, m-1);
            for (int i = 0; i < k; ++i) {
                sampled.push_back(vec[dist(rng)]);
            }
        } else {
            // Shuffle and take first k
            std::shuffle(vec.begin(), vec.end(), rng);
            for (int i = 0; i < k; ++i) {
                sampled.push_back(vec[i]);
            }
        }
    }
    // Return indices of sampled rows as IntegerVector
    IntegerVector out(sampled.size());
    for (size_t i = 0; i < sampled.size(); ++i) out[i] = sampled[i] + 1; // R uses 1-index
    return out;
}