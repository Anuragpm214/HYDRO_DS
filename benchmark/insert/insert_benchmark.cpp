// =============================================================================
//  insert_benchmark.cpp
//  Comprehensive & Unbiased Insertion Benchmark Suite
//
//  Compares:
//    1. HydroDS (Branchless, Cache-aligned, Fluid Rebalancing)
//    2. HydroDS-Eytzinger (BFS order + SW prefetch)
//    3. ALEX (Learned Index - SIGMOD 2020)
//    4. TLX B-Tree (Cache-conscious B+ tree)
//    5. Dynamic PGM-Index (Piecewise Geometric Models - VLDB 2020)
//    6. std::multiset (STL Red-Black Tree reference)
//
//  Distributions Evaluated:
//    - Uniform Random (i.i.d. random keys)
//    - Sequential Ascending (0, 1, 2, ..., N-1)
//    - Sequential Descending (N-1, N-2, ..., 0)
//    - Zipfian / Heavy Skew (theta = 0.99)
//    - Gaussian / Clustered (Multi-modal normal mixture)
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

// =============================================================================
//  Memory Measurement Helper (Linux VmRSS)
// =============================================================================
static size_t get_peak_rss_kb() {
    FILE* file = fopen("/proc/self/status", "r");
    if (!file) return 0;
    char line[128];
    size_t val = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %zu kB", &val);
            break;
        }
    }
    fclose(file);
    return val;
}

// =============================================================================
//  Workload Generator (Unbiased Pre-generation)
// =============================================================================
enum class Distribution {
    UniformRandom,
    SequentialAscending,
    SequentialDescending,
    Zipfian,
    GaussianClustered
};

static string dist_to_string(Distribution d) {
    switch (d) {
        case Distribution::UniformRandom:         return "Uniform Random";
        case Distribution::SequentialAscending:   return "Sequential (Asc)";
        case Distribution::SequentialDescending:  return "Sequential (Desc)";
        case Distribution::Zipfian:               return "Zipfian (Skewed)";
        case Distribution::GaussianClustered:     return "Gaussian Clustered";
    }
    return "Unknown";
}

// Fast Zipfian random number generator
class FastZipfian {
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
    FastZipfian(uint64_t n, double theta = 0.99, uint64_t seed = 42)
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
        return static_cast<uint64_t>(static_cast<double>(n_) * pow(eta_ * u - eta_ + 1.0, alpha_));
    }
};

static vector<int32_t> generate_keys(Distribution dist, size_t n, uint32_t seed = 42) {
    vector<int32_t> keys(n);
    mt19937 rng(seed);

    switch (dist) {
        case Distribution::UniformRandom: {
            iota(keys.begin(), keys.end(), 1);
            shuffle(keys.begin(), keys.end(), rng);
            break;
        }
        case Distribution::SequentialAscending: {
            iota(keys.begin(), keys.end(), 1);
            break;
        }
        case Distribution::SequentialDescending: {
            for (size_t i = 0; i < n; ++i) {
                keys[i] = static_cast<int32_t>(n - i);
            }
            break;
        }
        case Distribution::Zipfian: {
            FastZipfian zipf(n, 0.99, seed);
            int64_t curr = 1;
            for (size_t i = 0; i < n; ++i) {
                curr += static_cast<int64_t>(zipf.next()) + 1;
                keys[i] = static_cast<int32_t>(curr & 0x7FFFFFFF);
            }
            shuffle(keys.begin(), keys.end(), rng);
            break;
        }
        case Distribution::GaussianClustered: {
            normal_distribution<double> g1(static_cast<double>(n) * 0.15, static_cast<double>(n) * 0.03);
            normal_distribution<double> g2(static_cast<double>(n) * 0.40, static_cast<double>(n) * 0.05);
            normal_distribution<double> g3(static_cast<double>(n) * 0.70, static_cast<double>(n) * 0.04);
            normal_distribution<double> g4(static_cast<double>(n) * 0.90, static_cast<double>(n) * 0.02);
            uniform_int_distribution<int> pick(0, 3);

            vector<double> raw(n);
            for (size_t i = 0; i < n; ++i) {
                int cluster = pick(rng);
                if (cluster == 0) raw[i] = g1(rng);
                else if (cluster == 1) raw[i] = g2(rng);
                else if (cluster == 2) raw[i] = g3(rng);
                else raw[i] = g4(rng);
            }
            sort(raw.begin(), raw.end());
            int32_t curr = 1;
            for (size_t i = 0; i < n; ++i) {
                int32_t step = max<int32_t>(1, static_cast<int32_t>(abs(raw[i] - (i > 0 ? raw[i-1] : 0.0))));
                curr += step;
                keys[i] = curr;
            }
            shuffle(keys.begin(), keys.end(), rng);
            break;
        }
    }
    return keys;
}

// =============================================================================
//  Abstract Benchmark Wrapper Interface
// =============================================================================
class IndexBenchmarkBase {
public:
    virtual ~IndexBenchmarkBase() = default;
    virtual string name() const = 0;
    virtual void insert_all(const vector<int32_t>& keys) = 0;
    virtual size_t verify_sample(const vector<int32_t>& keys, size_t sample_count) = 0;
    virtual size_t total_count() const = 0;
};

// 1. HydroDS
class HydroDSBenchmark : public IndexBenchmarkBase {
    HydroDS<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS (C=256)"; }
    void insert_all(const vector<int32_t>& keys) override {
        for (int32_t k : keys) ds_.insert(k);
    }
    size_t verify_sample(const vector<int32_t>& keys, size_t sample_count) override {
        size_t found = 0;
        size_t step = max<size_t>(1, keys.size() / sample_count);
        for (size_t i = 0; i < keys.size(); i += step) {
            if (ds_.search(keys[i])) ++found;
        }
        return found;
    }
    size_t total_count() const override { return ds_.size(); }
};

// 2. HydroDS-Eytzinger
class HydroDSEytzingerBenchmark : public IndexBenchmarkBase {
    HydroDSEytzinger<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS-Eytzinger"; }
    void insert_all(const vector<int32_t>& keys) override {
        for (int32_t k : keys) ds_.insert(k);
    }
    size_t verify_sample(const vector<int32_t>& keys, size_t sample_count) override {
        size_t found = 0;
        size_t step = max<size_t>(1, keys.size() / sample_count);
        for (size_t i = 0; i < keys.size(); i += step) {
            if (ds_.search(keys[i])) ++found;
        }
        return found;
    }
    size_t total_count() const override { return ds_.size(); }
};

// 3. ALEX
class AlexBenchmark : public IndexBenchmarkBase {
    alex::Alex<int32_t, int32_t> idx_;
public:
    string name() const override { return "ALEX (Learned)"; }
    void insert_all(const vector<int32_t>& keys) override {
        for (int32_t k : keys) idx_.insert(k, k);
    }
    size_t verify_sample(const vector<int32_t>& keys, size_t sample_count) override {
        size_t found = 0;
        size_t step = max<size_t>(1, keys.size() / sample_count);
        for (size_t i = 0; i < keys.size(); i += step) {
            auto it = idx_.lower_bound(keys[i]);
            if (it != idx_.end() && it.key() == keys[i]) ++found;
        }
        return found;
    }
    size_t total_count() const override { return idx_.size(); }
};

// 4. TLX B-Tree
class TlxBenchmark : public IndexBenchmarkBase {
    tlx::btree_multiset<int32_t> btree_;
public:
    string name() const override { return "TLX B-Tree"; }
    void insert_all(const vector<int32_t>& keys) override {
        for (int32_t k : keys) btree_.insert(k);
    }
    size_t verify_sample(const vector<int32_t>& keys, size_t sample_count) override {
        size_t found = 0;
        size_t step = max<size_t>(1, keys.size() / sample_count);
        for (size_t i = 0; i < keys.size(); i += step) {
            auto it = btree_.lower_bound(keys[i]);
            if (it != btree_.end() && *it == keys[i]) ++found;
        }
        return found;
    }
    size_t total_count() const override { return btree_.size(); }
};

// 5. Dynamic PGM-Index
class PGMBenchmark : public IndexBenchmarkBase {
    pgm::DynamicPGMIndex<int32_t, int32_t> pgm_;
public:
    string name() const override { return "PGM-Index"; }
    void insert_all(const vector<int32_t>& keys) override {
        for (int32_t k : keys) pgm_.insert_or_assign(k, k);
    }
    size_t verify_sample(const vector<int32_t>& keys, size_t sample_count) override {
        size_t found = 0;
        size_t step = max<size_t>(1, keys.size() / sample_count);
        for (size_t i = 0; i < keys.size(); i += step) {
            auto it = pgm_.find(keys[i]);
            if (it != pgm_.end()) ++found;
        }
        return found;
    }
    size_t total_count() const override { return pgm_.size(); }
};

// 6. std::multiset
class StdSetBenchmark : public IndexBenchmarkBase {
    multiset<int32_t> st_;
public:
    string name() const override { return "std::multiset (RBTree)"; }
    void insert_all(const vector<int32_t>& keys) override {
        for (int32_t k : keys) st_.insert(k);
    }
    size_t verify_sample(const vector<int32_t>& keys, size_t sample_count) override {
        size_t found = 0;
        size_t step = max<size_t>(1, keys.size() / sample_count);
        for (size_t i = 0; i < keys.size(); i += step) {
            auto it = st_.lower_bound(keys[i]);
            if (it != st_.end() && *it == keys[i]) ++found;
        }
        return found;
    }
    size_t total_count() const override { return st_.size(); }
};

// =============================================================================
//  Benchmark Execution & Results Logging
// =============================================================================
struct ResultRecord {
    string structure;
    string distribution;
    size_t n_keys;
    double time_sec;
    double mops;
    double ns_per_op;
    size_t mem_kb;
    double bytes_per_key;
    double ipc;
    double l1_miss_rate;
    double llc_miss_rate;
    double branch_miss_rate;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t l1d_misses;
    uint64_t llc_misses;
    uint64_t branch_misses;
    bool verified;
};

static void print_header() {
    cout << "\n"
         << left << setw(23) << "Data Structure"
         << left << setw(20) << "Distribution"
         << right << setw(10) << "Keys"
         << right << setw(10) << "Time (s)"
         << right << setw(10) << "MOps/s"
         << right << setw(10) << "ns/op"
         << right << setw(10) << "Mem (MB)"
         << right << setw(8)  << "IPC"
         << right << setw(11) << "L1 Miss%"
         << right << setw(11) << "LLC Miss%"
         << right << setw(11) << "Br Miss%"
         << right << setw(8)  << "Status"
         << "\n";
    cout << string(142, '-') << "\n";
}

static void print_row(const ResultRecord& r) {
    double mem_mb = static_cast<double>(r.mem_kb) / 1024.0;
    cout << left  << setw(23) << r.structure
         << left  << setw(20) << r.distribution
         << right << setw(10) << r.n_keys
         << right << setw(10) << fixed << setprecision(4) << r.time_sec
         << right << setw(10) << fixed << setprecision(2) << r.mops
         << right << setw(10) << fixed << setprecision(1) << r.ns_per_op
         << right << setw(10) << fixed << setprecision(2) << mem_mb
         << right << setw(8)  << fixed << setprecision(2) << r.ipc
         << right << setw(10) << fixed << setprecision(2) << r.l1_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.llc_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.branch_miss_rate << "%"
         << right << setw(8)  << (r.verified ? "[OK]" : "[FAIL]")
         << "\n";
}

static void save_csv(const string& filepath, const vector<ResultRecord>& results) {
    ofstream out(filepath);
    if (!out.is_open()) {
        cerr << "Warning: Could not open " << filepath << " for writing CSV.\n";
        return;
    }
    out << "Structure,Distribution,N_Keys,Time_Seconds,Throughput_MOps,Latency_ns,Memory_KB,Bytes_Per_Key,"
        << "IPC,L1_Miss_Rate_Pct,LLC_Miss_Rate_Pct,Branch_Miss_Rate_Pct,"
        << "Cycles,Instructions,L1D_Misses,LLC_Misses,Branch_Misses,Verified\n";
    for (const auto& r : results) {
        out << r.structure << ","
            << r.distribution << ","
            << r.n_keys << ","
            << fixed << setprecision(6) << r.time_sec << ","
            << fixed << setprecision(4) << r.mops << ","
            << fixed << setprecision(2) << r.ns_per_op << ","
            << r.mem_kb << ","
            << fixed << setprecision(2) << r.bytes_per_key << ","
            << fixed << setprecision(4) << r.ipc << ","
            << fixed << setprecision(4) << r.l1_miss_rate << ","
            << fixed << setprecision(4) << r.llc_miss_rate << ","
            << fixed << setprecision(4) << r.branch_miss_rate << ","
            << r.cycles << ","
            << r.instructions << ","
            << r.l1d_misses << ","
            << r.llc_misses << ","
            << r.branch_misses << ","
            << (r.verified ? "1" : "0") << "\n";
    }
    out.close();
    cout << "\n[+] Results successfully exported to: " << filepath << "\n";
}

// =============================================================================
//  Main Runner
// =============================================================================
int main(int argc, char** argv) {
    size_t scale = 1000000; // Default 1M keys
    int repeats = 5;         // Default 5 runs as requested
    string output_csv = "benchmark/insert/results/insert_results.csv";

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--n" && i + 1 < argc) {
            scale = stoull(argv[++i]);
        } else if (arg == "--repeats" && i + 1 < argc) {
            repeats = stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_csv = argv[++i];
        } else if (arg == "--quick") {
            scale = 200000;
            repeats = 5;
        } else if (arg == "--heavy") {
            scale = 5000000;
            repeats = 5;
        }
    }

    cout << "=============================================================================\n";
    cout << "          HYDRODS INSERTION BENCHMARK (WITH HARDWARE PERF PROFILING)        \n";
    cout << "=============================================================================\n";
    cout << "  Scale (Keys per test): " << scale << "\n";
    cout << "  Runs per test:         " << repeats << " (Averaged over 5 runs)\n";
    cout << "  Target CSV:            " << output_csv << "\n";
    cout << "=============================================================================\n";

    // Ensure results folder exists
    system("mkdir -p benchmark/insert/results");

    PerfProfiler test_profiler;
    if (!test_profiler.is_supported()) {
        cerr << "\n[!] WARNING: Hardware perf counters could not be initialized.\n"
             << "    If non-root, run: sudo sysctl -w kernel.perf_event_paranoid=-1\n"
             << "    Hardware PMU counters (IPC, L1/LLC miss %, branch miss %) may be 0.\n\n";
    } else {
        cout << "[+] Hardware perf counters initialized successfully (L1, LLC, Branch, IPC enabled).\n\n";
    }

    vector<Distribution> distributions = {
        Distribution::UniformRandom,
        Distribution::SequentialAscending,
        Distribution::SequentialDescending,
        Distribution::Zipfian,
        Distribution::GaussianClustered
    };

    vector<ResultRecord> all_results;
    print_header();

    for (Distribution dist : distributions) {
        string dist_name = dist_to_string(dist);

        // Pre-generate insertion dataset upfront so timing ONLY measures pure data structure insertion
        auto dataset = generate_keys(dist, scale, 42);

        // List of factory functions for each candidate
        vector<pair<string, function<unique_ptr<IndexBenchmarkBase>()>>> candidates = {
            {"HydroDS (C=256)",       []() { return make_unique<HydroDSBenchmark>(); }},
            {"HydroDS-Eytzinger",     []() { return make_unique<HydroDSEytzingerBenchmark>(); }},
            {"ALEX (Learned)",        []() { return make_unique<AlexBenchmark>(); }},
            {"TLX B-Tree",            []() { return make_unique<TlxBenchmark>(); }},
            {"PGM-Index",             []() { return make_unique<PGMBenchmark>(); }},
            {"std::multiset (RBTree)",[]() { return make_unique<StdSetBenchmark>(); }}
        };

        for (auto& [cand_name, factory] : candidates) {
            double sum_time = 0.0;
            size_t peak_mem_delta_kb = 0;
            bool is_verified = true;

            uint64_t total_cycles = 0;
            uint64_t total_instructions = 0;
            uint64_t total_l1d_access = 0;
            uint64_t total_l1d_miss = 0;
            uint64_t total_llc_access = 0;
            uint64_t total_llc_miss = 0;
            uint64_t total_branch_instr = 0;
            uint64_t total_branch_miss = 0;

            for (int trial = 0; trial < repeats; ++trial) {
                // Measure memory before
                size_t mem_before = get_peak_rss_kb();

                auto index = factory();

                PerfProfiler profiler;
                
                // Timed & profiled insertion loop
                profiler.start();
                auto t_start = clk::now();
                index->insert_all(dataset);
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

                // Measure memory after
                size_t mem_after = get_peak_rss_kb();
                if (mem_after > mem_before) {
                    peak_mem_delta_kb = max(peak_mem_delta_kb, mem_after - mem_before);
                }

                // Verify correctness on sample
                size_t samples = min<size_t>(10000, dataset.size());
                size_t found = index->verify_sample(dataset, samples);
                if (found < samples * 0.99) { // Expect 100%
                    is_verified = false;
                }
            }

            // Compute Average across the 5 runs
            double avg_time = sum_time / static_cast<double>(repeats);
            double mops = (static_cast<double>(scale) / avg_time) / 1e6;
            double ns_per_op = (avg_time / static_cast<double>(scale)) * 1e9;
            double bytes_per_key = (scale > 0 && peak_mem_delta_kb > 0)
                ? (static_cast<double>(peak_mem_delta_kb) * 1024.0 / static_cast<double>(scale))
                : 0.0;

            double avg_ipc = total_cycles > 0 ? static_cast<double>(total_instructions) / static_cast<double>(total_cycles) : 0.0;
            double avg_l1_miss_pct = total_l1d_access > 0 ? (static_cast<double>(total_l1d_miss) / static_cast<double>(total_l1d_access)) * 100.0 : 0.0;
            double avg_llc_miss_pct = total_llc_access > 0 ? (static_cast<double>(total_llc_miss) / static_cast<double>(total_llc_access)) * 100.0 : 0.0;
            double avg_br_miss_pct = total_branch_instr > 0 ? (static_cast<double>(total_branch_miss) / static_cast<double>(total_branch_instr)) * 100.0 : 0.0;

            ResultRecord record{
                cand_name,
                dist_name,
                scale,
                avg_time,
                mops,
                ns_per_op,
                peak_mem_delta_kb,
                bytes_per_key,
                avg_ipc,
                avg_l1_miss_pct,
                avg_llc_miss_pct,
                avg_br_miss_pct,
                total_cycles / repeats,
                total_instructions / repeats,
                total_l1d_miss / repeats,
                total_llc_miss / repeats,
                total_branch_miss / repeats,
                is_verified
            };

            all_results.push_back(record);
            print_row(record);
        }
        cout << string(142, '-') << "\n";
    }

    // Save CSV
    save_csv(output_csv, all_results);

    return 0;
}
