// =============================================================================
//  delete_benchmark.cpp
//  Comprehensive & Unbiased Delete (Erase) Benchmark Suite
//
//  Compares Erase Latency & Throughput across:
//    1. HydroDS (Branchless, Reverse Fluid Rebalancing)
//    2. HydroDS-Eytzinger (Eytzinger shadow update)
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

enum class DeleteWorkload {
    RandomErase,
    SequentialAscErase,
    SequentialDescErase,
    NonExistingErase
};

static string workload_to_string(DeleteWorkload w) {
    switch (w) {
        case DeleteWorkload::RandomErase:         return "Random Erase (50%)";
        case DeleteWorkload::SequentialAscErase:  return "Sequential (Asc)";
        case DeleteWorkload::SequentialDescErase: return "Sequential (Desc)";
        case DeleteWorkload::NonExistingErase:   return "Non-Existing Keys";
    }
    return "Unknown";
}

// Generate delete target keys
static vector<int32_t> generate_delete_targets(DeleteWorkload w, const vector<int32_t>& dataset, size_t num_deletes, uint32_t seed = 88) {
    vector<int32_t> targets;
    targets.reserve(num_deletes);
    mt19937 rng(seed);
    size_t n = dataset.size();

    switch (w) {
        case DeleteWorkload::RandomErase: {
            vector<int32_t> temp = dataset;
            shuffle(temp.begin(), temp.end(), rng);
            targets.assign(temp.begin(), temp.begin() + min(num_deletes, n));
            break;
        }
        case DeleteWorkload::SequentialAscErase: {
            vector<int32_t> sorted = dataset;
            sort(sorted.begin(), sorted.end());
            targets.assign(sorted.begin(), sorted.begin() + min(num_deletes, n));
            break;
        }
        case DeleteWorkload::SequentialDescErase: {
            vector<int32_t> sorted = dataset;
            sort(sorted.rbegin(), sorted.rend());
            targets.assign(sorted.begin(), sorted.begin() + min(num_deletes, n));
            break;
        }
        case DeleteWorkload::NonExistingErase: {
            uniform_int_distribution<int32_t> dist(30000000, 50000000);
            for (size_t i = 0; i < num_deletes; ++i) {
                targets.push_back(dist(rng));
            }
            break;
        }
    }
    return targets;
}

// =============================================================================
//  Abstract Benchmark Wrapper Interface
// =============================================================================
class DeleteIndexBase {
public:
    virtual ~DeleteIndexBase() = default;
    virtual string name() const = 0;
    virtual void build_index(const vector<int32_t>& dataset) = 0;
    virtual size_t run_deletes(const vector<int32_t>& targets) = 0;
    virtual size_t remaining_size() const = 0;
};

// 1. HydroDS
class HydroDSDeleteBenchmark : public DeleteIndexBase {
    HydroDS<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS (C=256)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) ds_.insert(k);
    }
    size_t run_deletes(const vector<int32_t>& targets) override {
        size_t erased = 0;
        for (int32_t k : targets) {
            if (ds_.erase(k)) ++erased;
        }
        return erased;
    }
    size_t remaining_size() const override { return ds_.size(); }
};

// 2. HydroDS-Eytzinger
class HydroDSEytzingerDeleteBenchmark : public DeleteIndexBase {
    HydroDSEytzinger<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS-Eytzinger"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) ds_.insert(k);
    }
    size_t run_deletes(const vector<int32_t>& targets) override {
        size_t erased = 0;
        for (int32_t k : targets) {
            if (ds_.erase(k)) ++erased;
        }
        return erased;
    }
    size_t remaining_size() const override { return ds_.size(); }
};

// 3. ALEX
class AlexDeleteBenchmark : public DeleteIndexBase {
    alex::Alex<int32_t, int32_t> idx_;
public:
    string name() const override { return "ALEX (Learned)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) idx_.insert(k, k);
    }
    size_t run_deletes(const vector<int32_t>& targets) override {
        size_t erased = 0;
        for (int32_t k : targets) {
            int num = idx_.erase(k);
            if (num > 0) ++erased;
        }
        return erased;
    }
    size_t remaining_size() const override { return idx_.size(); }
};

// 4. TLX B-Tree
class TlxDeleteBenchmark : public DeleteIndexBase {
    tlx::btree_multiset<int32_t> btree_;
public:
    string name() const override { return "TLX B-Tree"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) btree_.insert(k);
    }
    size_t run_deletes(const vector<int32_t>& targets) override {
        size_t erased = 0;
        for (int32_t k : targets) {
            if (btree_.erase_one(k)) ++erased;
        }
        return erased;
    }
    size_t remaining_size() const override { return btree_.size(); }
};

// 5. Dynamic PGM-Index
class PGMDeleteBenchmark : public DeleteIndexBase {
    pgm::DynamicPGMIndex<int32_t, int32_t> pgm_;
public:
    string name() const override { return "PGM-Index"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) pgm_.insert_or_assign(k, k);
    }
    size_t run_deletes(const vector<int32_t>& targets) override {
        size_t erased = 0;
        for (int32_t k : targets) {
            pgm_.erase(k);
            ++erased;
        }
        return erased;
    }
    size_t remaining_size() const override { return pgm_.size(); }
};

// 6. std::multiset
class StdSetDeleteBenchmark : public DeleteIndexBase {
    multiset<int32_t> st_;
public:
    string name() const override { return "std::multiset (RBTree)"; }
    void build_index(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) st_.insert(k);
    }
    size_t run_deletes(const vector<int32_t>& targets) override {
        size_t erased = 0;
        for (int32_t k : targets) {
            auto it = st_.find(k);
            if (it != st_.end()) {
                st_.erase(it);
                ++erased;
            }
        }
        return erased;
    }
    size_t remaining_size() const override { return st_.size(); }
};

// =============================================================================
//  Results Logging & Console Output
// =============================================================================
struct DeleteResultRecord {
    string structure;
    string workload;
    size_t dataset_size;
    size_t num_deletes;
    double time_sec;
    double mops;
    double ns_per_op;
    size_t erased_count;
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
         << left << setw(24) << "Delete Workload"
         << right << setw(10) << "Time (s)"
         << right << setw(10) << "MOps/s"
         << right << setw(10) << "ns/op"
         << right << setw(8)  << "IPC"
         << right << setw(11) << "L1 Miss%"
         << right << setw(11) << "LLC Miss%"
         << right << setw(11) << "Br Miss%"
         << right << setw(10) << "Erased"
         << "\n";
    cout << string(130, '-') << "\n";
}

static void print_row(const DeleteResultRecord& r) {
    cout << left  << setw(23) << r.structure
         << left  << setw(24) << r.workload
         << right << setw(10) << fixed << setprecision(4) << r.time_sec
         << right << setw(10) << fixed << setprecision(2) << r.mops
         << right << setw(10) << fixed << setprecision(1) << r.ns_per_op
         << right << setw(8)  << fixed << setprecision(2) << r.ipc
         << right << setw(10) << fixed << setprecision(2) << r.l1_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.llc_miss_rate << "%"
         << right << setw(10) << fixed << setprecision(2) << r.branch_miss_rate << "%"
         << right << setw(10) << r.erased_count
         << "\n";
}

static void save_csv(const string& filepath, const vector<DeleteResultRecord>& results) {
    ofstream out(filepath);
    if (!out.is_open()) {
        cerr << "Warning: Could not open " << filepath << " for writing CSV.\n";
        return;
    }
    out << "Structure,Workload,Dataset_Size,Num_Deletes,Time_Seconds,Throughput_MOps,Latency_ns,Erased_Count,"
        << "IPC,L1_Miss_Rate_Pct,LLC_Miss_Rate_Pct,Branch_Miss_Rate_Pct,"
        << "Cycles,Instructions,L1D_Misses,LLC_Misses,Branch_Misses\n";
    for (const auto& r : results) {
        out << r.structure << ","
            << r.workload << ","
            << r.dataset_size << ","
            << r.num_deletes << ","
            << fixed << setprecision(6) << r.time_sec << ","
            << fixed << setprecision(4) << r.mops << ","
            << fixed << setprecision(2) << r.ns_per_op << ","
            << r.erased_count << ","
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
    size_t num_deletes  = 500000;  // 500k erasures per test (50% dataset)
    int repeats = 5;                // Default 5 runs averaged
    string output_csv = "benchmark/delete/results/delete_results.csv";

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--n" && i + 1 < argc) {
            dataset_size = stoull(argv[++i]);
        } else if (arg == "--deletes" && i + 1 < argc) {
            num_deletes = stoull(argv[++i]);
        } else if (arg == "--repeats" && i + 1 < argc) {
            repeats = stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_csv = argv[++i];
        } else if (arg == "--quick") {
            dataset_size = 200000;
            num_deletes  = 100000;
            repeats = 5;
        } else if (arg == "--heavy") {
            dataset_size = 5000000;
            num_deletes  = 2500000;
            repeats = 5;
        }
    }

    cout << "=============================================================================\n";
    cout << "          HYDRODS DELETE BENCHMARK (WITH HARDWARE PERF PROFILING)             \n";
    cout << "=============================================================================\n";
    cout << "  Dataset Size (Keys):   " << dataset_size << "\n";
    cout << "  Deletions Executed:    " << num_deletes << "\n";
    cout << "  Runs per test:         " << repeats << " (Averaged over 5 runs)\n";
    cout << "  Target CSV:            " << output_csv << "\n";
    cout << "=============================================================================\n";

    system("mkdir -p benchmark/delete/results");

    PerfProfiler test_profiler;
    if (!test_profiler.is_supported()) {
        cerr << "\n[!] WARNING: Hardware perf counters could not be initialized.\n"
             << "    If non-root, run: sudo sysctl -w kernel.perf_event_paranoid=-1\n\n";
    } else {
        cout << "[+] Hardware perf counters initialized successfully (L1, LLC, Branch, IPC enabled).\n\n";
    }

    vector<int32_t> dataset(dataset_size);
    iota(dataset.begin(), dataset.end(), 1);
    mt19937 rng(42);
    shuffle(dataset.begin(), dataset.end(), rng);

    vector<DeleteWorkload> workloads = {
        DeleteWorkload::RandomErase,
        DeleteWorkload::SequentialAscErase,
        DeleteWorkload::SequentialDescErase,
        DeleteWorkload::NonExistingErase
    };

    vector<DeleteResultRecord> all_results;
    print_header();

    vector<pair<string, function<unique_ptr<DeleteIndexBase>()>>> candidates = {
        {"HydroDS (C=256)",       []() { return make_unique<HydroDSDeleteBenchmark>(); }},
        {"HydroDS-Eytzinger",     []() { return make_unique<HydroDSEytzingerDeleteBenchmark>(); }},
        {"ALEX (Learned)",        []() { return make_unique<AlexDeleteBenchmark>(); }},
        {"TLX B-Tree",            []() { return make_unique<TlxDeleteBenchmark>(); }},
        {"PGM-Index",             []() { return make_unique<PGMDeleteBenchmark>(); }},
        {"std::multiset (RBTree)",[]() { return make_unique<StdSetDeleteBenchmark>(); }}
    };

    for (DeleteWorkload w : workloads) {
        string workload_name = workload_to_string(w);
        auto targets = generate_delete_targets(w, dataset, num_deletes, 88);

        for (auto& [cand_name, factory] : candidates) {
            double sum_time = 0.0;
            size_t erased = 0;

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
                erased = index->run_deletes(targets);
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
            double mops = (static_cast<double>(num_deletes) / avg_time) / 1e6;
            double ns_per_op = (avg_time / static_cast<double>(num_deletes)) * 1e9;

            double avg_ipc = total_cycles > 0 ? static_cast<double>(total_instructions) / static_cast<double>(total_cycles) : 0.0;
            double avg_l1_miss_pct = total_l1d_access > 0 ? (static_cast<double>(total_l1d_miss) / static_cast<double>(total_l1d_access)) * 100.0 : 0.0;
            double avg_llc_miss_pct = total_llc_access > 0 ? (static_cast<double>(total_llc_miss) / static_cast<double>(total_llc_access)) * 100.0 : 0.0;
            double avg_br_miss_pct = total_branch_instr > 0 ? (static_cast<double>(total_branch_miss) / static_cast<double>(total_branch_instr)) * 100.0 : 0.0;

            DeleteResultRecord record{
                cand_name,
                workload_name,
                dataset_size,
                num_deletes,
                avg_time,
                mops,
                ns_per_op,
                erased,
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
