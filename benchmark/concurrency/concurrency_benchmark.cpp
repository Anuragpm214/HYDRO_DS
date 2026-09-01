// =============================================================================
//  concurrency_benchmark.cpp
//  Comprehensive & Scalable Multi-Threaded Index Benchmark Suite
//
//  Evaluates Multi-Core Scaling across 1, 2, 4, 8, 16 Threads for:
//    1. HydroDS-Concurrent (Fine-Grained Optimistic Lock Coupling - OLC)
//    2. TLX B-Tree (Global Shared-Mutex / RWLock)
//    3. ALEX (Global Mutex)
//    4. Dynamic PGM-Index (Global Mutex)
//    5. std::multiset (Global Mutex)
//
//  Concurrent Workloads:
//    - Read-Only (100% Reads / Point Lookups)
//    - Read-Heavy (90% Reads / 10% Inserts)
//    - Balanced OLTP (50% Reads / 50% Inserts)
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
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <set>

// --- HydroDS Concurrent Header ---
#include "hydrods_multi_threaded/hydrods_concurrent.hpp"

// --- Baselines ---
#include "tlx/container/btree_multiset.hpp"
#include "src/core/alex.h"
#include "pgm/pgm_index_dynamic.hpp"

using namespace std;
using clk = chrono::high_resolution_clock;

enum class ConcurrencyWorkload {
    ReadOnly100,  // 100% Reads
    ReadHeavy90,  // 90% Reads, 10% Writes
    Balanced50    // 50% Reads, 50% Writes
};

static string workload_to_string(ConcurrencyWorkload w) {
    switch (w) {
        case ConcurrencyWorkload::ReadOnly100: return "100% Read-Only";
        case ConcurrencyWorkload::ReadHeavy90: return "90% Read / 10% Write";
        case ConcurrencyWorkload::Balanced50:  return "50% Read / 50% Write";
    }
    return "Unknown";
}

// =============================================================================
//  Abstract Multi-Threaded Candidate Interface
// =============================================================================
class ConcurrentIndexBase {
public:
    virtual ~ConcurrentIndexBase() = default;
    virtual string name() const = 0;
    virtual void build_initial(const vector<int32_t>& dataset) = 0;
    virtual bool execute_read(int32_t key) = 0;
    virtual void execute_write(int32_t key) = 0;
};

// 1. HydroDS Concurrent (OLC)
class HydroDSConcurrentWrapper : public ConcurrentIndexBase {
    ConcurrentHydroDS<int32_t, 256> ds_;
public:
    string name() const override { return "HydroDS-Concurrent (OLC)"; }
    void build_initial(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) ds_.insert(k);
    }
    bool execute_read(int32_t key) override {
        return ds_.search(key);
    }
    void execute_write(int32_t key) override {
        ds_.insert(key);
    }
};

// 2. TLX B-Tree + RWLock
class TlxConcurrentWrapper : public ConcurrentIndexBase {
    tlx::btree_multiset<int32_t> btree_;
    mutable shared_mutex rwlock_;
public:
    string name() const override { return "TLX B-Tree (RWLock)"; }
    void build_initial(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) btree_.insert(k);
    }
    bool execute_read(int32_t key) override {
        shared_lock<shared_mutex> lock(rwlock_);
        return btree_.exists(key);
    }
    void execute_write(int32_t key) override {
        unique_lock<shared_mutex> lock(rwlock_);
        btree_.insert(key);
    }
};

// 3. ALEX + Global Mutex
class AlexConcurrentWrapper : public ConcurrentIndexBase {
    alex::Alex<int32_t, int32_t> idx_;
    mutable mutex mtx_;
public:
    string name() const override { return "ALEX (Global Mutex)"; }
    void build_initial(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) idx_.insert(k, k);
    }
    bool execute_read(int32_t key) override {
        lock_guard<mutex> lock(mtx_);
        return idx_.find(key) != idx_.end();
    }
    void execute_write(int32_t key) override {
        lock_guard<mutex> lock(mtx_);
        idx_.insert(key, key);
    }
};

// 4. Dynamic PGM-Index + Global Mutex
class PGMConcurrentWrapper : public ConcurrentIndexBase {
    pgm::DynamicPGMIndex<int32_t, int32_t> pgm_;
    mutable mutex mtx_;
public:
    string name() const override { return "PGM-Index (Global Mutex)"; }
    void build_initial(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) pgm_.insert_or_assign(k, k);
    }
    bool execute_read(int32_t key) override {
        lock_guard<mutex> lock(mtx_);
        return pgm_.find(key) != pgm_.end();
    }
    void execute_write(int32_t key) override {
        lock_guard<mutex> lock(mtx_);
        pgm_.insert_or_assign(key, key);
    }
};

// 5. std::multiset + Global Mutex
class StdSetConcurrentWrapper : public ConcurrentIndexBase {
    multiset<int32_t> st_;
    mutable mutex mtx_;
public:
    string name() const override { return "std::multiset (Global Mutex)"; }
    void build_initial(const vector<int32_t>& dataset) override {
        for (int32_t k : dataset) st_.insert(k);
    }
    bool execute_read(int32_t key) override {
        lock_guard<mutex> lock(mtx_);
        return st_.find(key) != st_.end();
    }
    void execute_write(int32_t key) override {
        lock_guard<mutex> lock(mtx_);
        st_.insert(key);
    }
};

// =============================================================================
//  Results Logging & Console Output
// =============================================================================
struct ConcurrencyResultRecord {
    string structure;
    string workload;
    int threads;
    size_t total_ops;
    double time_sec;
    double throughput_mops;
    double latency_ns;
    double speedup;
};

static void print_header() {
    cout << "\n"
         << left << setw(27) << "Data Structure"
         << left << setw(24) << "Workload"
         << right << setw(10) << "Threads"
         << right << setw(12) << "Ops"
         << right << setw(12) << "Time (s)"
         << right << setw(14) << "Throughput(M)"
         << right << setw(12) << "Latency(ns)"
         << right << setw(10) << "Speedup"
         << "\n";
    cout << string(125, '-') << "\n";
}

static void print_row(const ConcurrencyResultRecord& r) {
    cout << left  << setw(27) << r.structure
         << left  << setw(24) << r.workload
         << right << setw(10) << r.threads
         << right << setw(12) << r.total_ops
         << right << setw(12) << fixed << setprecision(4) << r.time_sec
         << right << setw(14) << fixed << setprecision(2) << r.throughput_mops
         << right << setw(12) << fixed << setprecision(1) << r.latency_ns
         << right << setw(9)  << fixed << setprecision(2) << r.speedup << "x"
         << "\n";
}

static void save_csv(const string& filepath, const vector<ConcurrencyResultRecord>& results) {
    ofstream out(filepath);
    if (!out.is_open()) {
        cerr << "Warning: Could not open " << filepath << " for writing CSV.\n";
        return;
    }
    out << "Structure,Workload,Threads,Total_Ops,Time_Seconds,Throughput_MOps,Latency_ns,Speedup\n";
    for (const auto& r : results) {
        out << r.structure << ","
            << r.workload << ","
            << r.threads << ","
            << r.total_ops << ","
            << fixed << setprecision(6) << r.time_sec << ","
            << fixed << setprecision(4) << r.throughput_mops << ","
            << fixed << setprecision(2) << r.latency_ns << ","
            << fixed << setprecision(2) << r.speedup << "\n";
    }
    out.close();
    cout << "\n[+] Results successfully exported to: " << filepath << "\n";
}

// =============================================================================
//  Main Runner
// =============================================================================
int main(int argc, char** argv) {
    size_t initial_keys = 1000000; // 1M keys initial dataset
    size_t total_ops    = 2000000; // 2M operations executed per test
    int repeats = 3;               // Trials to average out jitter
    string output_csv = "benchmark/concurrency/results/concurrency_results.csv";

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--n" && i + 1 < argc) {
            initial_keys = stoull(argv[++i]);
        } else if (arg == "--ops" && i + 1 < argc) {
            total_ops = stoull(argv[++i]);
        } else if (arg == "--repeats" && i + 1 < argc) {
            repeats = stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_csv = argv[++i];
        } else if (arg == "--quick") {
            initial_keys = 200000;
            total_ops    = 500000;
            repeats = 1;
        }
    }

    cout << "=============================================================================\n";
    cout << "          HYDRODS MULTI-THREADED CONCURRENCY BENCHMARK (1 - 16 THREADS)      \n";
    cout << "=============================================================================\n";
    cout << "  Initial Dataset:       " << initial_keys << " keys\n";
    cout << "  Operations per test:   " << total_ops << "\n";
    cout << "  Trials per test:       " << repeats << "\n";
    cout << "  Target CSV:            " << output_csv << "\n";
    cout << "=============================================================================\n";

    system("mkdir -p benchmark/concurrency/results");

    // Pre-generate base initial dataset
    vector<int32_t> initial_dataset(initial_keys);
    iota(initial_dataset.begin(), initial_dataset.end(), 1);
    mt19937 rng(42);
    shuffle(initial_dataset.begin(), initial_dataset.end(), rng);

    vector<int> thread_counts = {1, 2, 4, 8, 16};
    vector<ConcurrencyWorkload> workloads = {
        ConcurrencyWorkload::ReadOnly100,
        ConcurrencyWorkload::ReadHeavy90,
        ConcurrencyWorkload::Balanced50
    };

    vector<pair<string, function<unique_ptr<ConcurrentIndexBase>()>>> candidates = {
        {"HydroDS-Concurrent (OLC)",   []() { return make_unique<HydroDSConcurrentWrapper>(); }},
        {"TLX B-Tree (RWLock)",        []() { return make_unique<TlxConcurrentWrapper>(); }},
        {"ALEX (Global Mutex)",        []() { return make_unique<AlexConcurrentWrapper>(); }},
        {"PGM-Index (Global Mutex)",   []() { return make_unique<PGMConcurrentWrapper>(); }},
        {"std::multiset (Global Mutex)",[]() { return make_unique<StdSetConcurrentWrapper>(); }}
    };

    vector<ConcurrencyResultRecord> all_results;
    print_header();

    for (ConcurrencyWorkload w : workloads) {
        string workload_name = workload_to_string(w);

        for (auto& [cand_name, factory] : candidates) {
            double single_thread_mops = 1.0;

            for (int num_threads : thread_counts) {
                vector<double> trial_times;

                for (int trial = 0; trial < repeats; ++trial) {
                    auto index = factory();
                    index->build_initial(initial_dataset);

                    size_t ops_per_thread = total_ops / num_threads;
                    vector<thread> workers;
                    workers.reserve(num_threads);

                    auto t_start = clk::now();

                    for (int t = 0; t < num_threads; ++t) {
                        workers.emplace_back([&, t]() {
                            mt19937 local_rng(1000 + t * 37 + trial);
                            uniform_int_distribution<size_t> read_key_dist(0, initial_keys - 1);
                            uniform_int_distribution<int32_t> write_key_dist(20000000, 50000000);
                            uniform_int_distribution<int> op_dist(1, 100);

                            for (size_t i = 0; i < ops_per_thread; ++i) {
                                int p = op_dist(local_rng);
                                if (w == ConcurrencyWorkload::ReadOnly100) {
                                    int32_t k = initial_dataset[read_key_dist(local_rng)];
                                    index->execute_read(k);
                                } else if (w == ConcurrencyWorkload::ReadHeavy90) {
                                    if (p <= 90) {
                                        int32_t k = initial_dataset[read_key_dist(local_rng)];
                                        index->execute_read(k);
                                    } else {
                                        int32_t k = write_key_dist(local_rng);
                                        index->execute_write(k);
                                    }
                                } else { // Balanced50
                                    if (p <= 50) {
                                        int32_t k = initial_dataset[read_key_dist(local_rng)];
                                        index->execute_read(k);
                                    } else {
                                        int32_t k = write_key_dist(local_rng);
                                        index->execute_write(k);
                                    }
                                }
                            }
                        });
                    }

                    for (auto& worker : workers) {
                        worker.join();
                    }

                    auto t_end = clk::now();
                    double elapsed = chrono::duration<double>(t_end - t_start).count();
                    trial_times.push_back(elapsed);
                }

                sort(trial_times.begin(), trial_times.end());
                double median_time = trial_times[trial_times.size() / 2];
                double mops = (static_cast<double>(total_ops) / median_time) / 1e6;
                double latency_ns = (median_time / static_cast<double>(total_ops)) * 1e9;

                if (num_threads == 1) {
                    single_thread_mops = mops;
                }
                double speedup = mops / single_thread_mops;

                ConcurrencyResultRecord record{
                    cand_name,
                    workload_name,
                    num_threads,
                    total_ops,
                    median_time,
                    mops,
                    latency_ns,
                    speedup
                };

                all_results.push_back(record);
                print_row(record);
            }
            cout << string(125, '-') << "\n";
        }
    }

    save_csv(output_csv, all_results);

    return 0;
}
