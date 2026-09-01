// =============================================================================
//  range_benchmark.cpp
//  Comprehensive & Unbiased Range Query Benchmark Suite
//
//  Compares Range Scan Throughput & Latency across:
//    1. HydroDS (Branchless, Contiguous 256-element Bucket Scans)
//    2. HydroDS-Eytzinger (Contiguous key traversal)
//    3. ALEX (Learned Index - SIGMOD 2020)
//    4. TLX B-Tree (Cache-conscious B+ tree)
//    5. Dynamic PGM-Index (Piecewise Geometric Models - VLDB 2020)
//    6. std::multiset (STL Red-Black Tree reference)
//
//  Range Scan Sizes Evaluated:
//    - Small Scans (Range Length = 10 elements)
//    - Medium Scans (Range Length = 100 elements)
//    - Large Scans (Range Length = 1,000 elements)
//    - Extra Large Scans (Range Length = 10,000 elements)
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

enum class RangeScanSize {
    Small10,        // Scan 10 elements
    Medium100,      // Scan 100 elements
    Large1000,      // Scan 1,000 elements
    ExtraLarge10000 // Scan 10,000 elements
};

static string scan_size_to_string(RangeScanSize s) {
    switch (s) {
        case RangeScanSize::Small10:         return "Small Scan (Len = 10)";
        case RangeScanSize::Medium100:       return "Medium Scan (Len = 100)";
        case RangeScanSize::Large1000:       return "Large Scan (Len = 1000)";
        case RangeScanSize::ExtraLarge10000: return "Extra Large Scan (Len = 10k)";
    }
    return "Unknown";
}

static size_t get_scan_len(RangeScanSize s) {
    switch (s) {
        case RangeScanSize::Small10:         return 10;
        case RangeScanSize::Medium100:       return 100;
        case RangeScanSize::Large1000:       return 1000;
        case RangeScanSize::ExtraLarge10000: return 10000;
    }
    return 10;
}

// Generate range queries [low, high]
static vector<pair<int32_t, int32_t>> generate_range_queries(RangeScanSize s, const vector<int32_t>& dataset, size_t num_queries, uint32_t seed = 77) {
    vector<pair<int32_t, int32_t>> queries(num_queries);
    mt19937 rng(seed);
    size_t len = get_scan_len(s);

    int32_t max_key = *max_element(dataset.begin(), dataset.end());
    int32_t min_key = *min_element(dataset.begin(), dataset.end());

    int32_t upper_bound_start = max<int32_t>(min_key, max_key - static_cast<int32_t>(len) - 1);
    uniform_int_distribution<int32_t> start_dist(min_key, upper_bound_start);

    for (size_t i = 0; i < num_queries; ++i) {
        int32_t low = start_dist(rng);
        int32_t high = low + static_cast<int32_t>(len);
        queries[i] = {low, high};
    }
    return queries;
}

// =============================================================================
//  Abstract Benchmark Wrapper Interface
// =============================================================================
class RangeIndexBase {
public:
    virtual ~RangeIndexBase() = default;
    virtual string name() const = 0;
    virtual void build_index(const vector<int32_t>& dataset) = 0;
    virtual int64_t run_range_queries(const vector<pair<int32_t, int32_t>>& queries) = 0;
};

// 1. HydroDS
class HydroDSRangeBenchmark : public RangeIndexBase {
    HydroDS<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS (C=256)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) ds_.insert(k);
    }
    int64_t run_range_queries(const vector<pair<int32_t, int32_t>>& queries) override {
        volatile int64_t total = 0;
        int64_t local_total = 0;
        for (const auto& q : queries) {
            local_total += ds_.range_query(q.first, q.second);
        }
        total = local_total;
        return total;
    }
};

// 2. HydroDS-Eytzinger
class HydroDSEytzingerRangeBenchmark : public RangeIndexBase {
    HydroDSEytzinger<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS-Eytzinger"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) ds_.insert(k);
    }
    int64_t run_range_queries(const vector<pair<int32_t, int32_t>>& queries) override {
        volatile int64_t total = 0;
        int64_t local_total = 0;
        for (const auto& q : queries) {
            local_total += ds_.range_query(q.first, q.second);
        }
        total = local_total;
        return total;
    }
};

// 3. ALEX
class AlexRangeBenchmark : public RangeIndexBase {
    alex::Alex<int32_t, int32_t> idx_;
public:
    string name() const override { return "ALEX (Learned)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) idx_.insert(k, k);
    }
    int64_t run_range_queries(const vector<pair<int32_t, int32_t>>& queries) override {
        volatile int64_t total = 0;
        int64_t local_total = 0;
        for (const auto& q : queries) {
            int64_t cnt = 0;
            for (auto it = idx_.lower_bound(q.first); it != idx_.end() && it.key() <= q.second; ++it) {
                cnt++;
            }
            local_total += cnt;
        }
        total = local_total;
        return total;
    }
};

// 4. TLX B-Tree
class TlxRangeBenchmark : public RangeIndexBase {
    tlx::btree_multiset<int32_t> btree_;
public:
    string name() const override { return "TLX B-Tree"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) btree_.insert(k);
    }
    int64_t run_range_queries(const vector<pair<int32_t, int32_t>>& queries) override {
        volatile int64_t total = 0;
        int64_t local_total = 0;
        for (const auto& q : queries) {
            int64_t cnt = 0;
            for (auto it = btree_.lower_bound(q.first); it != btree_.end() && *it <= q.second; ++it) {
                cnt++;
            }
            local_total += cnt;
        }
        total = local_total;
        return total;
    }
};

// 5. Dynamic PGM-Index
class PGMRangeBenchmark : public RangeIndexBase {
    pgm::DynamicPGMIndex<int32_t, int32_t> pgm_;
public:
    string name() const override { return "PGM-Index"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) pgm_.insert_or_assign(k, k);
    }
    int64_t run_range_queries(const vector<pair<int32_t, int32_t>>& queries) override {
        volatile int64_t total = 0;
        int64_t local_total = 0;
        for (const auto& q : queries) {
            int64_t cnt = 0;
            for (auto it = pgm_.lower_bound(q.first); it != pgm_.end() && it->first <= q.second; ++it) {
                cnt++;
            }
            local_total += cnt;
        }
        total = local_total;
        return total;
    }
};

// 6. std::multiset
class StdSetRangeBenchmark : public RangeIndexBase {
    multiset<int32_t> st_;
public:
    string name() const override { return "std::multiset (RBTree)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) st_.insert(k);
    }
    int64_t run_range_queries(const vector<pair<int32_t, int32_t>>& queries) override {
        volatile int64_t total = 0;
        int64_t local_total = 0;
        for (const auto& q : queries) {
            int64_t cnt = 0;
            for (auto it = st_.lower_bound(q.first); it != st_.end() && *it <= q.second; ++it) {
                cnt++;
            }
            local_total += cnt;
        }
        total = local_total;
        return total;
    }
};

// =============================================================================
//  Results Logging & Console Output
// =============================================================================
struct RangeResultRecord {
    string structure;
    string workload;
    size_t dataset_size;
    size_t num_queries;
    size_t target_len;
    double time_sec;
    double queries_per_sec;
    double us_per_query;
    int64_t total_keys_scanned;
    double scanned_mops;
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
         << left << setw(26) << "Range Workload"
         << right << setw(10) << "Time (s)"
         << right << setw(11) << "Queries/s"
         << right << setw(11) << "us/query"
         << right << setw(12) << "Scan MOps/s"
         << right << setw(8)  << "IPC"
         << right << setw(11) << "L1 Miss%"
         << right << setw(11) << "LLC Miss%"
         << right << setw(11) << "Br Miss%"
         << "\n";
    cout << string(134, '-') << "\n";
}

static void print_row(const RangeResultRecord& r) {
    cout << left  << setw(23) << r.structure
         << left  << setw(26) << r.workload
         << right << setw(10) << fixed << setprecision(4) << r.time_sec
         << right << setw(11) << fixed << setprecision(1) << r.queries_per_sec
         << right << setw(11) << fixed << setprecision(2) << r.us_per_query
         << right << setw(12) << fixed << setprecision(2) << r.scanned_mops
         << right << setw(8)  << fixed << setprecision(2) << r.ipc
         << right << setw(10) << fixed << setprecision(2) << r.l1_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.llc_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.branch_miss_rate << "%"
         << "\n";
}

static void save_csv(const string& filepath, const vector<RangeResultRecord>& results) {
    ofstream out(filepath);
    if (!out.is_open()) {
        cerr << "Warning: Could not open " << filepath << " for writing CSV.\n";
        return;
    }
    out << "Structure,Workload,Dataset_Size,Num_Queries,Target_Len,Time_Seconds,Queries_Per_Sec,Latency_us,Total_Keys_Scanned,Scanned_MOps,"
        << "IPC,L1_Miss_Rate_Pct,LLC_Miss_Rate_Pct,Branch_Miss_Rate_Pct,"
        << "Cycles,Instructions,L1D_Misses,LLC_Misses,Branch_Misses\n";
    for (const auto& r : results) {
        out << r.structure << ","
            << r.workload << ","
            << r.dataset_size << ","
            << r.num_queries << ","
            << r.target_len << ","
            << fixed << setprecision(6) << r.time_sec << ","
            << fixed << setprecision(2) << r.queries_per_sec << ","
            << fixed << setprecision(2) << r.us_per_query << ","
            << r.total_keys_scanned << ","
            << fixed << setprecision(2) << r.scanned_mops << ","
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
    size_t dataset_size = 1000000; // 1M keys in index
    size_t num_queries  = 50000;   // 50k range queries per test
    int repeats = 5;               // Default 5 runs as requested
    string output_csv = "benchmark/range/results/range_results.csv";

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
            num_queries  = 10000;
            repeats = 5;
        } else if (arg == "--heavy") {
            dataset_size = 5000000;
            num_queries  = 100000;
            repeats = 5;
        }
    }

    cout << "=============================================================================\n";
    cout << "          HYDRODS RANGE QUERY BENCHMARK (WITH HARDWARE PERF PROFILING)       \n";
    cout << "=============================================================================\n";
    cout << "  Dataset Size (Keys):   " << dataset_size << "\n";
    cout << "  Range Queries / Test:  " << num_queries << "\n";
    cout << "  Runs per test:         " << repeats << " (Averaged over 5 runs)\n";
    cout << "  Target CSV:            " << output_csv << "\n";
    cout << "=============================================================================\n";

    system("mkdir -p benchmark/range/results");

    PerfProfiler test_profiler;
    if (!test_profiler.is_supported()) {
        cerr << "\n[!] WARNING: Hardware perf counters could not be initialized.\n"
             << "    If non-root, run: sudo sysctl -w kernel.perf_event_paranoid=-1\n"
             << "    Hardware PMU counters (IPC, L1/LLC miss %, branch miss %) may be 0.\n\n";
    } else {
        cout << "[+] Hardware perf counters initialized successfully (L1, LLC, Branch, IPC enabled).\n\n";
    }

    // Build unique dataset
    vector<int32_t> dataset(dataset_size);
    iota(dataset.begin(), dataset.end(), 1);
    mt19937 rng(42);
    shuffle(dataset.begin(), dataset.end(), rng);

    vector<RangeScanSize> scan_sizes = {
        RangeScanSize::Small10,
        RangeScanSize::Medium100,
        RangeScanSize::Large1000,
        RangeScanSize::ExtraLarge10000
    };

    vector<RangeResultRecord> all_results;
    print_header();

    vector<pair<string, function<unique_ptr<RangeIndexBase>()>>> candidates = {
        {"HydroDS (C=256)",       []() { return make_unique<HydroDSRangeBenchmark>(); }},
        {"HydroDS-Eytzinger",     []() { return make_unique<HydroDSEytzingerRangeBenchmark>(); }},
        {"ALEX (Learned)",        []() { return make_unique<AlexRangeBenchmark>(); }},
        {"TLX B-Tree",            []() { return make_unique<TlxRangeBenchmark>(); }},
        {"PGM-Index",             []() { return make_unique<PGMRangeBenchmark>(); }},
        {"std::multiset (RBTree)",[]() { return make_unique<StdSetRangeBenchmark>(); }}
    };

    for (RangeScanSize s : scan_sizes) {
        string workload_name = scan_size_to_string(s);
        size_t target_len = get_scan_len(s);

        auto range_queries = generate_range_queries(s, dataset, num_queries, 77);

        for (auto& [cand_name, factory] : candidates) {
            double sum_time = 0.0;
            int64_t total_scanned = 0;

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
                total_scanned = index->run_range_queries(range_queries);
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
            double qps = static_cast<double>(num_queries) / avg_time;
            double us_per_query = (avg_time / static_cast<double>(num_queries)) * 1e6;
            double scanned_mops = (static_cast<double>(total_scanned) / avg_time) / 1e6;

            double avg_ipc = total_cycles > 0 ? static_cast<double>(total_instructions) / static_cast<double>(total_cycles) : 0.0;
            double avg_l1_miss_pct = total_l1d_access > 0 ? (static_cast<double>(total_l1d_miss) / static_cast<double>(total_l1d_access)) * 100.0 : 0.0;
            double avg_llc_miss_pct = total_llc_access > 0 ? (static_cast<double>(total_llc_miss) / static_cast<double>(total_llc_access)) * 100.0 : 0.0;
            double avg_br_miss_pct = total_branch_instr > 0 ? (static_cast<double>(total_branch_miss) / static_cast<double>(total_branch_instr)) * 100.0 : 0.0;

            RangeResultRecord record{
                cand_name,
                workload_name,
                dataset_size,
                num_queries,
                target_len,
                avg_time,
                qps,
                us_per_query,
                total_scanned,
                scanned_mops,
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
        cout << string(134, '-') << "\n";
    }

    save_csv(output_csv, all_results);

    return 0;
}
