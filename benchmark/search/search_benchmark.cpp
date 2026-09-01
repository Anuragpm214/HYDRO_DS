// =============================================================================
//  search_benchmark.cpp
//  Comprehensive & Unbiased Search (Point Query) Benchmark Suite
//
//  Compares Point Query Latency & Throughput across:
//    1. HydroDS (Branchless, Cache-aligned, Model + Galloping Search)
//    2. HydroDS-Eytzinger (BFS order + SW Prefetching)
//    3. ALEX (Learned Index - SIGMOD 2020)
//    4. TLX B-Tree (Cache-conscious B+ tree)
//    5. Dynamic PGM-Index (Piecewise Geometric Models - VLDB 2020)
//    6. std::multiset (STL Red-Black Tree reference)
// =============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>
#include <memory>
#include <set>
#include <functional>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

// --- HydroDS Headers ---
#include "hydrods.hpp"
#include "hydrods_eytzinger.hpp"

// --- Baseline Headers ---
#include "tlx/container/btree_multiset.hpp"
#include "src/core/alex.h"
#include "pgm/pgm_index_dynamic.hpp"

// --- Hardware Profiler ---
#include "../perf_profiler.hpp"

using namespace std;
using clk = chrono::high_resolution_clock;

enum class SearchWorkload {
    Hit100,      // 100% Hit Rate (Uniform lookup over inserted keys)
    Hit50Miss50, // 50% Hit / 50% Miss (Mixed existing and absent keys)
    Miss100,     // 100% Miss Rate (Keys not in dataset)
    ZipfianHotspot // Skewed access (Zipfian popularity over inserted keys)
};

static string workload_to_string(SearchWorkload w) {
    switch (w) {
        case SearchWorkload::Hit100:         return "100% Hit Rate";
        case SearchWorkload::Hit50Miss50:    return "50% Hit / 50% Miss";
        case SearchWorkload::Miss100:        return "100% Miss Rate";
        case SearchWorkload::ZipfianHotspot: return "Zipfian Hotspot Queries";
    }
    return "Unknown";
}

// Fast Zipfian random index generator for queries
class FastZipfianQuery {
    double alpha_;
    double zetan_;
    double eta_;
    double theta_;
    uint64_t n_;
    mt19937_64 rng_;
    uniform_real_distribution<double> uniform_dist_{0.0, 1.0};

    static double zeta(uint64_t n, double theta) {
        double sum = 0;
        for (uint64_t i = 1; i <= n; ++i) {
            sum += 1.0 / pow(static_cast<double>(i), theta);
        }
        return sum;
    }

public:
    FastZipfianQuery(uint64_t n, double theta = 0.99, uint64_t seed = 123)
        : theta_(theta), n_(n), rng_(seed) {
        alpha_ = 1.0 / (1.0 - theta_);
        zetan_ = zeta(n_, theta_);
        eta_ = (1.0 - pow(2.0 / static_cast<double>(n_), 1.0 - theta_)) / (1.0 - zeta(2, theta_) / zetan_);
    }

    uint64_t next() {
        double u = uniform_dist_(rng_);
        double uz = u * zetan_;
        if (uz < 1.0) return 0;
        if (uz < 1.0 + pow(0.5, theta_)) return 1;
        uint64_t val = static_cast<uint64_t>(static_cast<double>(n_) * pow(eta_ * u - eta_ + 1.0, alpha_));
        return min(val, n_ - 1);
    }
};

static vector<int32_t> generate_queries(SearchWorkload w, const vector<int32_t>& dataset, size_t num_queries, uint32_t seed = 99) {
    vector<int32_t> queries(num_queries);
    mt19937 rng(seed);
    size_t n = dataset.size();

    switch (w) {
        case SearchWorkload::Hit100: {
            uniform_int_distribution<size_t> dist(0, n - 1);
            for (size_t i = 0; i < num_queries; ++i) {
                queries[i] = dataset[dist(rng)];
            }
            break;
        }
        case SearchWorkload::Hit50Miss50: {
            uniform_int_distribution<size_t> dist(0, n - 1);
            uniform_int_distribution<int32_t> miss_dist(10000000, 20000000);
            for (size_t i = 0; i < num_queries; ++i) {
                if (i % 2 == 0) queries[i] = dataset[dist(rng)];
                else queries[i] = miss_dist(rng);
            }
            break;
        }
        case SearchWorkload::Miss100: {
            uniform_int_distribution<int32_t> dist(20000000, 50000000);
            for (size_t i = 0; i < num_queries; ++i) {
                queries[i] = dist(rng);
            }
            break;
        }
        case SearchWorkload::ZipfianHotspot: {
            vector<int32_t> sorted_keys = dataset;
            sort(sorted_keys.begin(), sorted_keys.end());
            FastZipfianQuery zipf(n, 0.99, seed);

            for (size_t i = 0; i < num_queries; ++i) {
                size_t idx = zipf.next();
                queries[i] = sorted_keys[idx];
            }
            break;
        }
    }
    return queries;
}

// =============================================================================
//  Abstract Benchmark Wrapper Interface
// =============================================================================
class SearchIndexBase {
public:
    virtual ~SearchIndexBase() = default;
    virtual string name() const = 0;
    virtual void build_index(const vector<int32_t>& dataset) = 0;
    virtual size_t run_queries(const vector<int32_t>& queries) = 0;
};

// 1. HydroDS
class HydroDSSearchBenchmark : public SearchIndexBase {
    HydroDS<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS (C=256)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) ds_.insert(k);
    }
    size_t run_queries(const vector<int32_t>& queries) override {
        size_t hits = 0;
        for (int32_t q : queries) {
            if (ds_.search(q)) ++hits;
        }
        return hits;
    }
};

// 2. HydroDS-Eytzinger
class HydroDSEytzingerSearchBenchmark : public SearchIndexBase {
    HydroDSEytzinger<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS-Eytzinger"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) ds_.insert(k);
    }
    size_t run_queries(const vector<int32_t>& queries) override {
        size_t hits = 0;
        for (int32_t q : queries) {
            if (ds_.search(q)) ++hits;
        }
        return hits;
    }
};

// 3. ALEX
class AlexSearchBenchmark : public SearchIndexBase {
    alex::Alex<int32_t, int32_t> idx_;
public:
    string name() const override { return "ALEX (Learned)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) idx_.insert(k, k);
    }
    size_t run_queries(const vector<int32_t>& queries) override {
        size_t hits = 0;
        for (int32_t q : queries) {
            auto it = idx_.find(q);
            if (it != idx_.end()) ++hits;
        }
        return hits;
    }
};

// 4. TLX B-Tree
class TlxSearchBenchmark : public SearchIndexBase {
    tlx::btree_multiset<int32_t> btree_;
public:
    string name() const override { return "TLX B-Tree"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) btree_.insert(k);
    }
    size_t run_queries(const vector<int32_t>& queries) override {
        size_t hits = 0;
        for (int32_t q : queries) {
            if (btree_.exists(q)) ++hits;
        }
        return hits;
    }
};

// 5. Dynamic PGM-Index
class PGMSearchBenchmark : public SearchIndexBase {
    pgm::DynamicPGMIndex<int32_t, int32_t> pgm_;
public:
    string name() const override { return "PGM-Index"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) pgm_.insert_or_assign(k, k);
    }
    size_t run_queries(const vector<int32_t>& queries) override {
        size_t hits = 0;
        for (int32_t q : queries) {
            auto it = pgm_.find(q);
            if (it != pgm_.end()) ++hits;
        }
        return hits;
    }
};

// 6. std::multiset
class StdSetSearchBenchmark : public SearchIndexBase {
    multiset<int32_t> st_;
public:
    string name() const override { return "std::multiset (RBTree)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) st_.insert(k);
    }
    size_t run_queries(const vector<int32_t>& queries) override {
        size_t hits = 0;
        for (int32_t q : queries) {
            auto it = st_.find(q);
            if (it != st_.end()) ++hits;
        }
        return hits;
    }
};

// =============================================================================
//  Results Logging & Console Output
// =============================================================================
struct SearchResultRecord {
    string structure;
    string workload;
    size_t dataset_size;
    size_t num_queries;
    double time_sec;
    double mops;
    double ns_per_query;
    size_t total_hits;
    double ipc;
    double l1_miss_rate;
    double llc_miss_rate;
    double branch_miss_rate;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t l1d_misses;
    uint64_t llc_misses;
    uint64_t branch_misses;
};

static void print_header() {
    cout << "\n"
         << left << setw(23) << "Data Structure"
         << left << setw(24) << "Search Workload"
         << right << setw(10) << "Time (s)"
         << right << setw(10) << "MOps/s"
         << right << setw(10) << "ns/op"
         << right << setw(8)  << "IPC"
         << right << setw(11) << "L1 Miss%"
         << right << setw(11) << "LLC Miss%"
         << right << setw(11) << "Br Miss%"
         << right << setw(10) << "Hits"
         << "\n";
    cout << string(130, '-') << "\n";
}

static void print_row(const SearchResultRecord& r) {
    cout << left  << setw(23) << r.structure
         << left  << setw(24) << r.workload
         << right << setw(10) << fixed << setprecision(4) << r.time_sec
         << right << setw(10) << fixed << setprecision(2) << r.mops
         << right << setw(10) << fixed << setprecision(1) << r.ns_per_query
         << right << setw(8)  << fixed << setprecision(2) << r.ipc
         << right << setw(10) << fixed << setprecision(2) << r.l1_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.llc_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.branch_miss_rate << "%"
         << right << setw(10) << r.total_hits
         << "\n";
}

static void save_csv(const string& filepath, const vector<SearchResultRecord>& results) {
    ofstream out(filepath);
    if (!out.is_open()) {
        cerr << "Warning: Could not open " << filepath << " for writing CSV.\n";
        return;
    }
    out << "Structure,Workload,Dataset_Size,Num_Queries,Time_Seconds,Throughput_MOps,Latency_ns,Total_Hits,"
        << "IPC,L1_Miss_Rate_Pct,LLC_Miss_Rate_Pct,Branch_Miss_Rate_Pct,"
        << "Cycles,Instructions,L1D_Misses,LLC_Misses,Branch_Misses\n";
    for (const auto& r : results) {
        out << r.structure << ","
            << r.workload << ","
            << r.dataset_size << ","
            << r.num_queries << ","
            << fixed << setprecision(6) << r.time_sec << ","
            << fixed << setprecision(4) << r.mops << ","
            << fixed << setprecision(2) << r.ns_per_query << ","
            << r.total_hits << ","
            << fixed << setprecision(4) << r.ipc << ","
            << fixed << setprecision(4) << r.l1_miss_rate << ","
            << fixed << setprecision(4) << r.llc_miss_rate << ","
            << fixed << setprecision(4) << r.branch_miss_rate << ","
            << r.cycles << ","
            << r.instructions << ","
            << r.l1d_misses << ","
            << r.llc_misses << ","
            << r.branch_misses << "\n";
    }
    out.close();
    cout << "\n[+] Results successfully exported to: " << filepath << "\n";
}

// =============================================================================
//  Main Runner
// =============================================================================
int main(int argc, char** argv) {
    size_t dataset_size = 1000000; // 1M keys inserted
    size_t num_queries  = 5000000; // 5M point queries executed
    int repeats = 5;                // Default 5 runs averaged
    string output_csv = "benchmark/search/results/search_results.csv";

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--n" && i + 1 < argc) {
            dataset_size = stoull(argv[++i]);
        } else if (arg == "--queries" && i + 1 < argc) {
            num_queries = stoull(argv[++i]);
        } else if (arg == "--repeats" && i + 1 < argc) {
            repeats = stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_csv = argv[++i];
        } else if (arg == "--quick") {
            dataset_size = 200000;
            num_queries  = 1000000;
            repeats = 5;
        } else if (arg == "--heavy") {
            dataset_size = 5000000;
            num_queries  = 10000000;
            repeats = 5;
        }
    }

    cout << "=============================================================================\n";
    cout << "          HYDRODS SEARCH BENCHMARK (WITH HARDWARE PERF PROFILING)            \n";
    cout << "=============================================================================\n";
    cout << "  Dataset Size (Keys):   " << dataset_size << "\n";
    cout << "  Queries Executed:      " << num_queries << "\n";
    cout << "  Runs per test:         " << repeats << " (Averaged over 5 runs)\n";
    cout << "  Target CSV:            " << output_csv << "\n";
    cout << "=============================================================================\n";

    system("mkdir -p benchmark/search/results");

    PerfProfiler test_profiler;
    if (!test_profiler.is_supported()) {
        cerr << "\n[!] WARNING: Hardware perf counters could not be initialized.\n"
             << "    If non-root, run: sudo sysctl -w kernel.perf_event_paranoid=-1\n\n";
    } else {
        cout << "[+] Hardware perf counters initialized successfully (L1, LLC, Branch, IPC enabled).\n\n";
    }

    // Build base unique dataset
    vector<int32_t> dataset(dataset_size);
    iota(dataset.begin(), dataset.end(), 1);
    mt19937 rng(42);
    shuffle(dataset.begin(), dataset.end(), rng);

    vector<SearchWorkload> workloads = {
        SearchWorkload::Hit100,
        SearchWorkload::Hit50Miss50,
        SearchWorkload::Miss100,
        SearchWorkload::ZipfianHotspot
    };

    vector<SearchResultRecord> all_results;
    print_header();

    vector<pair<string, function<unique_ptr<SearchIndexBase>()>>> candidates = {
        {"HydroDS (C=256)",       []() { return make_unique<HydroDSSearchBenchmark>(); }},
        {"HydroDS-Eytzinger",     []() { return make_unique<HydroDSEytzingerSearchBenchmark>(); }},
        {"ALEX (Learned)",        []() { return make_unique<AlexSearchBenchmark>(); }},
        {"TLX B-Tree",            []() { return make_unique<TlxSearchBenchmark>(); }},
        {"PGM-Index",             []() { return make_unique<PGMSearchBenchmark>(); }},
        {"std::multiset (RBTree)",[]() { return make_unique<StdSetSearchBenchmark>(); }}
    };

    for (SearchWorkload w : workloads) {
        string workload_name = workload_to_string(w);
        auto queries = generate_queries(w, dataset, num_queries, 99);

        for (auto& [cand_name, factory] : candidates) {
            double sum_time = 0.0;
            size_t hits = 0;

            uint64_t total_cycles = 0;
            uint64_t total_instructions = 0;
            uint64_t total_l1d_access = 0;
            uint64_t total_l1d_miss = 0;
            uint64_t total_llc_access = 0;
            uint64_t total_llc_miss = 0;
            uint64_t total_branch_instr = 0;
            uint64_t total_branch_miss = 0;

            for (int trial = 0; trial < repeats; ++trial) {
                auto index = factory();
                index->build_index(dataset);

                PerfProfiler profiler;

                profiler.start();
                auto t_start = clk::now();
                hits = index->run_queries(queries);
                auto t_end = clk::now();
                PerfCounters counters = profiler.stop();

                double elapsed = chrono::duration<double>(t_end - t_start).count();
                sum_time += elapsed;

                total_cycles += counters.cycles;
                total_instructions += counters.instructions;
                total_l1d_access += counters.l1d_read_access;
                total_l1d_miss += counters.l1d_read_miss;
                total_llc_access += counters.llc_read_access;
                total_llc_miss += counters.llc_read_miss;
                total_branch_instr += counters.branch_instructions;
                total_branch_miss += counters.branch_misses;
            }

            double avg_time = sum_time / static_cast<double>(repeats);
            double mops = (static_cast<double>(num_queries) / avg_time) / 1e6;
            double ns_per_query = (avg_time / static_cast<double>(num_queries)) * 1e9;

            double avg_ipc = total_cycles > 0 ? static_cast<double>(total_instructions) / static_cast<double>(total_cycles) : 0.0;
            double avg_l1_miss_pct = total_l1d_access > 0 ? (static_cast<double>(total_l1d_miss) / static_cast<double>(total_l1d_access)) * 100.0 : 0.0;
            double avg_llc_miss_pct = total_llc_access > 0 ? (static_cast<double>(total_llc_miss) / static_cast<double>(total_llc_access)) * 100.0 : 0.0;
            double avg_br_miss_pct = total_branch_instr > 0 ? (static_cast<double>(total_branch_miss) / static_cast<double>(total_branch_instr)) * 100.0 : 0.0;

            SearchResultRecord record{
                cand_name,
                workload_name,
                dataset_size,
                num_queries,
                avg_time,
                mops,
                ns_per_query,
                hits,
                avg_ipc,
                avg_l1_miss_pct,
                avg_llc_miss_pct,
                avg_br_miss_pct,
                total_cycles / repeats,
                total_instructions / repeats,
                total_l1d_miss / repeats,
                total_llc_miss / repeats,
                total_branch_miss / repeats
            };

            all_results.push_back(record);
            print_row(record);
        }
        cout << string(130, '-') << "\n";
    }

    save_csv(output_csv, all_results);

    return 0;
}
