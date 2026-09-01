# HydroDS Search (Point Query) Benchmark Suite

A standalone, unbiased search benchmark suite evaluating point query performance across data structures.

## 🎯 What it Evaluates
- **HydroDS (`C=256`)**: Model-based lookup + branchless intra-bucket search.
- **HydroDS-Eytzinger**: Eytzinger BFS layout with multi-level software prefetch.
- **ALEX**: Learned Index (SIGMOD 2020).
- **TLX B-Tree**: Cache-efficient B+ tree.
- **PGM-Index**: Piecewise Geometric Model Index (PVLDB 2020).
- **`std::multiset`**: Standard Red-Black Tree baseline.

## 📊 Workload Scenarios
1. **100% Hit Rate**: Uniform point lookups over inserted keys.
2. **50% Hit / 50% Miss Rate**: Mixed queries of existing and absent keys.
3. **100% Miss Rate**: Queries for keys outside index domain or key gaps.
4. **Zipfian Hotspot Queries**: Queries following a heavy Zipfian popularity skew ($\theta = 0.99$).

---

## 🚀 How to Run

### Option 1: Quick Run (200k dataset / 1M queries)
```bash
./build/search_benchmark --quick
```

### Option 2: Standard Benchmark (1M dataset / 5M queries, 3 trials)
```bash
./build/search_benchmark --n 1000000 --queries 5000000 --repeats 3
```

### Option 3: One-Command Execution & Plot Generation
```bash
./benchmark/search/run_benchmark.sh
```
Outputs are automatically saved to `benchmark/search/results/`:
- `search_throughput.png`
- `search_latency.png`
- `search_results.csv`
