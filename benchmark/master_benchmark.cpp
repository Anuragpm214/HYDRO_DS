// ============================================================================
// master_benchmark.cpp
// Evaluates HydroDS vs ALEX vs TLX B-Tree (CSB+) vs RBTree vs PMA
// ============================================================================

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>
#include <set>
#include <string>
#include <iomanip>
#include <cstdio>

// Includes
#include "../engine/hydrods.hpp"
#include "../engine/pma.hpp"
#include "../tlx_baseline/tlx/container/btree_multiset.hpp"
#include "../alex_baseline/src/core/alex.h"

using namespace std;
using clk = chrono::high_resolution_clock;

// Memory footprint helper
size_t get_memory_mb() {
    FILE* file = fopen("/proc/self/status", "r");
    if (!file) return 0;
    char line[128];
    while (fgets(line, 128, file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            size_t val;
            sscanf(line, "VmRSS: %zu kB", &val);
            fclose(file);
            return val / 1024;
        }
    }
    fclose(file);
    return 0;
}

// Wrapper for ALEX
class AlexWrapper {
    alex::Alex<int32_t, int32_t> idx;
public:
    void insert(int32_t k) { idx.insert(k, k); }
    int search(int32_t k) {
        auto it = idx.lower_bound(k);
        return (it != idx.end() && it.key() == k) ? 1 : 0;
    }
    int erase(int32_t k) { return idx.erase(k); }
    int64_t range_query(int32_t low, int32_t high) {
        int64_t sum = 0;
        for (auto it = idx.lower_bound(low); it != idx.end() && it.key() <= high; ++it) sum += it.key();
        return sum;
    }
};

// Wrapper for TLX (CSB+ Tree equivalent)
class TlxWrapper {
    tlx::btree_multiset<int32_t> idx;
public:
    void insert(int32_t k) { idx.insert(k); }
    int search(int32_t k) {
        auto it = idx.lower_bound(k);
        return (it != idx.end() && *it == k) ? 1 : 0;
    }
    int erase(int32_t k) { 
        auto it = idx.find(k);
        if (it != idx.end()) { idx.erase(it); return 1; }
        return 0;
    }
    int64_t range_query(int32_t low, int32_t high) {
        int64_t sum = 0;
        for (auto it = idx.lower_bound(low); it != idx.end() && *it <= high; ++it) sum += *it;
        return sum;
    }
};

// Wrapper for std::set (RBTree)
class RBTreeWrapper {
    std::multiset<int32_t> idx;
public:
    void insert(int32_t k) { idx.insert(k); }
    int search(int32_t k) {
        auto it = idx.lower_bound(k);
        return (it != idx.end() && *it == k) ? 1 : 0;
    }
    int erase(int32_t k) {
        auto it = idx.find(k);
        if (it != idx.end()) { idx.erase(it); return 1; }
        return 0;
    }
    int64_t range_query(int32_t low, int32_t high) {
        int64_t sum = 0;
        for (auto it = idx.lower_bound(low); it != idx.end() && *it <= high; ++it) sum += *it;
        return sum;
    }
};

template <typename Index>
void run_benchmark(Index& idx, const string& name, int N,
                   const vector<int32_t>& data,
                   const vector<int32_t>& search_queries,
                   const vector<int32_t>& delete_queries,
                   const vector<int32_t>& range_s, int len_s,
                   const vector<int32_t>& range_m, int len_m,
                   const vector<int32_t>& range_l, int len_l) 
{
    size_t mem_before = get_memory_mb();

    auto start = clk::now();
    for (int x : data) idx.insert(x);
    double t_ins = chrono::duration<double>(clk::now() - start).count();

    size_t mem_after = get_memory_mb();

    start = clk::now();
    uint64_t sum_s = 0;
    for (int q : search_queries) sum_s += idx.search(q);
    double t_srch = chrono::duration<double>(clk::now() - start).count();

    start = clk::now();
    uint64_t sum_rs = 0;
    for (int start_val : range_s) sum_rs += idx.range_query(start_val, start_val + len_s);
    double t_rs = chrono::duration<double>(clk::now() - start).count();

    start = clk::now();
    uint64_t sum_rm = 0;
    for (int start_val : range_m) sum_rm += idx.range_query(start_val, start_val + len_m);
    double t_rm = chrono::duration<double>(clk::now() - start).count();

    start = clk::now();
    uint64_t sum_rl = 0;
    for (int start_val : range_l) sum_rl += idx.range_query(start_val, start_val + len_l);
    double t_rl = chrono::duration<double>(clk::now() - start).count();

    start = clk::now();
    uint64_t sum_d = 0;
    for (int q : delete_queries) sum_d += idx.erase(q);
    double t_del = chrono::duration<double>(clk::now() - start).count();

    cout << "--- " << name << " Results ---\n"
         << fixed << setprecision(4)
         << "Insert Time: " << t_ins << " s\n"
         << "Search Time: " << t_srch << " s\n"
         << "Delete Time: " << t_del << " s\n"
         << "Small Range: " << t_rs << " s\n"
         << "Med Range  : " << t_rm << " s\n"
         << "Large Range: " << t_rl << " s\n"
         << "Memory Used: " << (mem_after - mem_before) << " MB\n";
    if (sum_s == 99999) cout << "ignore" << sum_rs << sum_rm << sum_rl << sum_d;
}

int main(int argc, char** argv) {
    string mode = "hydrods";
    int N = 5000000;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--n" && i + 1 < argc) N = stoi(argv[++i]);
    }

    int search_N = N / 10;
    int rq_s = 50000, rlen_s = 100;
    int rq_m = 5000, rlen_m = 10000;
    int rq_l = 50, rlen_l = 500000;

    vector<int32_t> data(N);
    iota(data.begin(), data.end(), 0);
    mt19937 rng(42);
    shuffle(data.begin(), data.end(), rng);

    uniform_int_distribution<int> dist(0, N-1);
    vector<int32_t> search_queries(search_N), delete_queries(search_N);
    for(int i=0; i<search_N; i++) {
        search_queries[i] = data[dist(rng)];
        delete_queries[i] = data[dist(rng)];
    }

    auto make_ranges = [&](int count, int len) {
        vector<int32_t> r(count);
        uniform_int_distribution<int> rdist(0, max(1, N - len - 1));
        for(int i=0; i<count; i++) r[i] = rdist(rng);
        return r;
    };

    vector<int32_t> range_s = make_ranges(rq_s, rlen_s);
    vector<int32_t> range_m = make_ranges(rq_m, rlen_m);
    vector<int32_t> range_l = make_ranges(rq_l, rlen_l);

    if (mode == "hydrods") {
        HydroDS<int32_t, 500> idx; idx.set_thresholds(0.85, 0.60);
        run_benchmark(idx, "HydroDS", N, data, search_queries, delete_queries, range_s, rlen_s, range_m, rlen_m, range_l, rlen_l);
    } else if (mode == "alex") {
        AlexWrapper idx;
        run_benchmark(idx, "ALEX", N, data, search_queries, delete_queries, range_s, rlen_s, range_m, rlen_m, range_l, rlen_l);
    } else if (mode == "csb") {
        TlxWrapper idx;
        run_benchmark(idx, "CSB+Tree", N, data, search_queries, delete_queries, range_s, rlen_s, range_m, rlen_m, range_l, rlen_l);
    } else if (mode == "rbtree") {
        RBTreeWrapper idx;
        run_benchmark(idx, "RBTree", N, data, search_queries, delete_queries, range_s, rlen_s, range_m, rlen_m, range_l, rlen_l);
    } else if (mode == "pma") {
        PMA idx;
        run_benchmark(idx, "PMA", N, data, search_queries, delete_queries, range_s, rlen_s, range_m, rlen_m, range_l, rlen_l);
    } else {
        cout << "Unknown mode\n";
    }

    return 0;
}
