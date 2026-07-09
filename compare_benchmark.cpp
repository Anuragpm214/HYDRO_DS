// =============================================================================
//  compare_benchmark.cpp
//  
//  Compares pure single-threaded HydroDS (zero locking overhead) vs
//  ConcurrentHydroDS (OLC lock coupling) on N = 5M insert / search / range.
//
//  Build:  cmake --build build --target compare_benchmark
//  Run:    ./build/compare_benchmark [--n 5000000] [--cap 500]
// =============================================================================

#include "engine/hydrods.hpp"
#include "engine/hydrods_concurrent.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>
#include <string>
#include <cstdio>
#include <cstring>
#include <thread>
#include <omp.h>

using namespace std;
using clk = chrono::high_resolution_clock;

// ─── helpers ────────────────────────────────────────────────────────────────

size_t get_rss_mb() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[128];
    while (fgets(line, 128, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            size_t v;
            sscanf(line, "VmRSS: %zu kB", &v);
            fclose(f);
            return v / 1024;
        }
    }
    fclose(f);
    return 0;
}

struct BenchResult {
    string label;
    int    threads;
    double t_insert;
    double t_search;
    double t_range_small;
    double t_range_medium;
    double t_range_large;
    size_t mem_mb;
    // throughput (Mops/s)
    double mops_insert;
    double mops_search;
};

// ─── Single-threaded HydroDS (no locks at all) ─────────────────────────────

template <int C>
BenchResult run_single_threaded(int N, int SEARCH_N,
                                double eps_high, double eps_low,
                                const vector<int32_t>& data,
                                const vector<int32_t>& search_queries,
                                const vector<int32_t>& range_starts_s, int RANGE_LEN_S, int RANGE_CNT_S,
                                const vector<int32_t>& range_starts_m, int RANGE_LEN_M, int RANGE_CNT_M,
                                const vector<int32_t>& range_starts_l, int RANGE_LEN_L, int RANGE_CNT_L)
{
    BenchResult r;
    r.label   = "HydroDS (single, no lock)";
    r.threads = 1;

    HydroDS<int32_t, C> h;
    h.set_thresholds(eps_high, eps_low);

    size_t mem0 = get_rss_mb();

    // INSERT
    auto t0 = clk::now();
    for (int i = 0; i < N; ++i) h.insert(data[i]);
    r.t_insert = chrono::duration<double>(clk::now() - t0).count();
    r.mem_mb   = get_rss_mb() - mem0;

    // SEARCH
    uint64_t chk = 0;
    t0 = clk::now();
    for (int i = 0; i < SEARCH_N; ++i) chk += h.search(search_queries[i]);
    r.t_search = chrono::duration<double>(clk::now() - t0).count();
    (void)chk;

    // RANGE — small
    int64_t rchk = 0;
    t0 = clk::now();
    for (int i = 0; i < RANGE_CNT_S; ++i)
        rchk += h.range_query(range_starts_s[i], range_starts_s[i] + RANGE_LEN_S);
    r.t_range_small = chrono::duration<double>(clk::now() - t0).count();

    // RANGE — medium
    t0 = clk::now();
    for (int i = 0; i < RANGE_CNT_M; ++i)
        rchk += h.range_query(range_starts_m[i], range_starts_m[i] + RANGE_LEN_M);
    r.t_range_medium = chrono::duration<double>(clk::now() - t0).count();

    // RANGE — large
    t0 = clk::now();
    for (int i = 0; i < RANGE_CNT_L; ++i)
        rchk += h.range_query(range_starts_l[i], range_starts_l[i] + RANGE_LEN_L);
    r.t_range_large = chrono::duration<double>(clk::now() - t0).count();
    (void)rchk;

    r.mops_insert = (N / 1e6)        / r.t_insert;
    r.mops_search = (SEARCH_N / 1e6) / r.t_search;
    return r;
}

// ─── Concurrent HydroDS (OLC, T threads) ────────────────────────────────────

template <int C>
BenchResult run_concurrent(int T, int N, int SEARCH_N,
                           double eps_high, double eps_low,
                           const vector<int32_t>& data,
                           const vector<int32_t>& search_queries,
                           const vector<int32_t>& range_starts_s, int RANGE_LEN_S, int RANGE_CNT_S,
                           const vector<int32_t>& range_starts_m, int RANGE_LEN_M, int RANGE_CNT_M,
                           const vector<int32_t>& range_starts_l, int RANGE_LEN_L, int RANGE_CNT_L)
{
    BenchResult r;
    r.label   = "ConcurrentHydroDS (T=" + to_string(T) + ")";
    r.threads = T;

    omp_set_num_threads(T);
    ConcurrentHydroDS<int32_t, C> h;
    h.set_thresholds(eps_high, eps_low);

    size_t mem0 = get_rss_mb();

    // INSERT
    auto t0 = clk::now();
    #pragma omp parallel for
    for (int i = 0; i < N; ++i) h.insert(data[i]);
    r.t_insert = chrono::duration<double>(clk::now() - t0).count();
    r.mem_mb   = get_rss_mb() - mem0;

    // SEARCH
    uint64_t chk = 0;
    t0 = clk::now();
    #pragma omp parallel for reduction(+:chk)
    for (int i = 0; i < SEARCH_N; ++i) chk += h.search(search_queries[i]);
    r.t_search = chrono::duration<double>(clk::now() - t0).count();
    (void)chk;

    // RANGE — small
    int64_t rchk = 0;
    t0 = clk::now();
    #pragma omp parallel for reduction(+:rchk)
    for (int i = 0; i < RANGE_CNT_S; ++i)
        rchk += h.range_query(range_starts_s[i], range_starts_s[i] + RANGE_LEN_S);
    r.t_range_small = chrono::duration<double>(clk::now() - t0).count();

    // RANGE — medium
    t0 = clk::now();
    #pragma omp parallel for reduction(+:rchk)
    for (int i = 0; i < RANGE_CNT_M; ++i)
        rchk += h.range_query(range_starts_m[i], range_starts_m[i] + RANGE_LEN_M);
    r.t_range_medium = chrono::duration<double>(clk::now() - t0).count();

    // RANGE — large
    t0 = clk::now();
    #pragma omp parallel for reduction(+:rchk)
    for (int i = 0; i < RANGE_CNT_L; ++i)
        rchk += h.range_query(range_starts_l[i], range_starts_l[i] + RANGE_LEN_L);
    r.t_range_large = chrono::duration<double>(clk::now() - t0).count();
    (void)rchk;

    r.mops_insert = (N / 1e6)        / r.t_insert;
    r.mops_search = (SEARCH_N / 1e6) / r.t_search;
    return r;
}

// ─── Pretty print ───────────────────────────────────────────────────────────

void print_header() {
    cout << "\n"
         << string(130, '=') << "\n"
         << "  HYDRODS:  Single-Threaded (no lock)  vs  ConcurrentHydroDS (OLC)\n"
         << string(130, '=') << "\n\n";

    cout << left
         << setw(32) << "Configuration"
         << right
         << setw(10) << "Ins(s)"
         << setw(12) << "Ins Mop/s"
         << setw(10) << "Srch(s)"
         << setw(12) << "Srch Mop/s"
         << setw(10) << "R_Sml(s)"
         << setw(10) << "R_Med(s)"
         << setw(10) << "R_Lrg(s)"
         << setw(10) << "Mem(MB)"
         << "\n";
    cout << string(130, '-') << "\n";
}

void print_row(const BenchResult& r) {
    cout << left  << setw(32) << r.label
         << right << fixed << setprecision(4)
         << setw(10) << r.t_insert
         << setw(12) << setprecision(2) << r.mops_insert
         << setw(10) << setprecision(4) << r.t_search
         << setw(12) << setprecision(2) << r.mops_search
         << setw(10) << setprecision(4) << r.t_range_small
         << setw(10) << r.t_range_medium
         << setw(10) << r.t_range_large
         << setw(10) << r.mem_mb
         << "\n";
}

void print_speedup(const BenchResult& baseline, const BenchResult& r) {
    auto sp = [](double base, double cur) -> string {
        if (cur <= 0) return "  -  ";
        double s = base / cur;
        char buf[16];
        snprintf(buf, sizeof(buf), "%5.2fx", s);
        return string(buf);
    };
    cout << left  << setw(32) << ("  -> speedup vs baseline")
         << right
         << setw(10) << sp(baseline.t_insert,       r.t_insert)
         << setw(12) << ""
         << setw(10) << sp(baseline.t_search,        r.t_search)
         << setw(12) << ""
         << setw(10) << sp(baseline.t_range_small,   r.t_range_small)
         << setw(10) << sp(baseline.t_range_medium,  r.t_range_medium)
         << setw(10) << sp(baseline.t_range_large,   r.t_range_large)
         << "\n";
}

// ─── main ───────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    int N        = 5000000;
    int capacity = 500;
    double eps_h = 0.85, eps_l = 0.60;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--n"    && i+1 < argc) N        = stoi(argv[++i]);
        if (a == "--cap"  && i+1 < argc) capacity = stoi(argv[++i]);
        if (a == "--high" && i+1 < argc) eps_h    = stod(argv[++i]);
        if (a == "--low"  && i+1 < argc) eps_l    = stod(argv[++i]);
        if (a == "--help" || a == "-h") {
            cout << "Usage: ./compare_benchmark [--n N] [--cap C] [--high f] [--low f]\n";
            return 0;
        }
    }

    const int SEARCH_N        = N / 10;
    const int RANGE_LEN_S     = 100;
    const int RANGE_CNT_S     = 50000;
    const int RANGE_LEN_M     = 10000;
    const int RANGE_CNT_M     = 5000;
    const int RANGE_LEN_L     = 500000;
    const int RANGE_CNT_L     = 50;
    int hw_threads             = static_cast<int>(thread::hardware_concurrency());

    cout << "╔══════════════════════════════════════════╗\n"
         << "║   HydroDS Comparison Benchmark           ║\n"
         << "╠══════════════════════════════════════════╣\n"
         << "║  N            = " << setw(12) << N           << "           ║\n"
         << "║  Search N     = " << setw(12) << SEARCH_N    << "           ║\n"
         << "║  Capacity (C) = " << setw(12) << capacity    << "           ║\n"
         << "║  EPS_HIGH     = " << setw(12) << eps_h       << "           ║\n"
         << "║  EPS_LOW      = " << setw(12) << eps_l       << "           ║\n"
         << "║  HW threads   = " << setw(12) << hw_threads  << "           ║\n"
         << "╚══════════════════════════════════════════╝\n";

    // ─── Generate workload ──────────────────────────────────────────────
    cout << "\n[*] Generating workload data..." << flush;
    vector<int32_t> data(N);
    iota(data.begin(), data.end(), 0);
    mt19937 rng(42);
    shuffle(data.begin(), data.end(), rng);

    uniform_int_distribution<size_t> dist(0, data.size() - 1);
    vector<int32_t> search_q(SEARCH_N);
    for (int i = 0; i < SEARCH_N; ++i) search_q[i] = data[dist(rng)];

    auto make_range_starts = [&](int range_len, int count) {
        int mx = max(1, N - range_len - 1);
        uniform_int_distribution<int32_t> d(0, mx);
        vector<int32_t> v(count);
        for (int i = 0; i < count; ++i) v[i] = d(rng);
        return v;
    };
    auto rs_s = make_range_starts(RANGE_LEN_S, RANGE_CNT_S);
    auto rs_m = make_range_starts(RANGE_LEN_M, RANGE_CNT_M);
    auto rs_l = make_range_starts(RANGE_LEN_L, RANGE_CNT_L);
    cout << " done.\n";

    // ─── Dispatch by capacity (compile-time template) ───────────────────
    // We'll use a lambda dispatcher to avoid duplicating the template call
    // for each capacity value.

    vector<BenchResult> results;

    auto run_all = [&]<int Cap>() {
        // 1) Single-threaded baseline
        cout << "\n[*] Running single-threaded HydroDS (no locks)..." << flush;
        results.push_back(run_single_threaded<Cap>(
            N, SEARCH_N, eps_h, eps_l, data, search_q,
            rs_s, RANGE_LEN_S, RANGE_CNT_S,
            rs_m, RANGE_LEN_M, RANGE_CNT_M,
            rs_l, RANGE_LEN_L, RANGE_CNT_L));
        cout << " done.\n";

        // 2) Concurrent at T=1 (shows lock overhead)
        cout << "[*] Running ConcurrentHydroDS T=1..." << flush;
        results.push_back(run_concurrent<Cap>(
            1, N, SEARCH_N, eps_h, eps_l, data, search_q,
            rs_s, RANGE_LEN_S, RANGE_CNT_S,
            rs_m, RANGE_LEN_M, RANGE_CNT_M,
            rs_l, RANGE_LEN_L, RANGE_CNT_L));
        cout << " done.\n";

        // 3) Concurrent at 2, 4, 8, max threads
        vector<int> thread_counts;
        for (int t : {2, 4, 8}) {
            if (t <= hw_threads) thread_counts.push_back(t);
        }
        if (hw_threads > 8) thread_counts.push_back(hw_threads);
        // If hw_threads is exactly one of {2,4,8}, it's already in; avoid duplicate
        if (!thread_counts.empty() && thread_counts.back() != hw_threads && hw_threads > 1)
            thread_counts.push_back(hw_threads);

        for (int t : thread_counts) {
            cout << "[*] Running ConcurrentHydroDS T=" << t << "..." << flush;
            results.push_back(run_concurrent<Cap>(
                t, N, SEARCH_N, eps_h, eps_l, data, search_q,
                rs_s, RANGE_LEN_S, RANGE_CNT_S,
                rs_m, RANGE_LEN_M, RANGE_CNT_M,
                rs_l, RANGE_LEN_L, RANGE_CNT_L));
            cout << " done.\n";
        }
    };

    // Dispatch on capacity template parameter
    if      (capacity <= 100)  run_all.template operator()<100>();
    else if (capacity <= 256)  run_all.template operator()<256>();
    else if (capacity <= 500)  run_all.template operator()<500>();
    else if (capacity <= 1024) run_all.template operator()<1024>();
    else if (capacity <= 2048) run_all.template operator()<2048>();
    else                       run_all.template operator()<4096>();

    // ─── Print results table ────────────────────────────────────────────
    print_header();
    const auto& baseline = results[0];
    for (size_t i = 0; i < results.size(); ++i) {
        print_row(results[i]);
        if (i > 0) print_speedup(baseline, results[i]);
    }
    cout << string(130, '=') << "\n";

    // ─── Summary insight ────────────────────────────────────────────────
    cout << "\n╔══════════════════════════════════════════════════════════════════╗\n"
         << "║  KEY INSIGHTS                                                    ║\n"
         << "╠══════════════════════════════════════════════════════════════════╣\n";

    if (results.size() >= 2) {
        double overhead = (results[1].t_insert / baseline.t_insert - 1.0) * 100.0;
        cout << "║  Lock overhead (Concurrent T=1 vs Single):                      ║\n"
             << "║    Insert: " << setw(+6) << fixed << setprecision(1) << overhead << "%"
             << "                                                    ║\n";

        double overhead_s = (results[1].t_search / baseline.t_search - 1.0) * 100.0;
        cout << "║    Search: " << setw(6) << fixed << setprecision(1) << overhead_s << "%"
             << "                                                    ║\n";
    }

    if (results.size() >= 3) {
        auto& best = results.back();
        double speedup_ins = baseline.t_insert / best.t_insert;
        double speedup_src = baseline.t_search / best.t_search;
        cout << "║  Best concurrent (T=" << best.threads << ") vs single-threaded:                   ║\n"
             << "║    Insert speedup: " << setw(5) << fixed << setprecision(2) << speedup_ins << "x"
             << "                                              ║\n"
             << "║    Search speedup: " << setw(5) << fixed << setprecision(2) << speedup_src << "x"
             << "                                              ║\n";
    }

    cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";

    return 0;
}
