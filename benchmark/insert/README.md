# HydroDS Insertion Benchmark Suite

A standalone, unbiased, and rigorous insertion benchmark suite designed for top-tier conference paper evaluation.

## 🎯 What it Evaluates
- **HydroDS (`C=256`)**: Pressure-based fluid load balancing with branchless search.
- **HydroDS-Eytzinger**: BFS layout with prefetching.
- **ALEX**: Learned Index (SIGMOD 2020).
- **TLX B-Tree**: Cache-efficient B+ tree.
- **PGM-Index**: Piecewise Geometric Model Index (PVLDB 2020).
- **`std::multiset`**: Standard Red-Black Tree baseline.

## 📊 Workload Distributions
1. **Uniform Random**: Dense and unique random keys.
2. **Sequential (Ascending)**: Strictly monotonic ($0, 1, 2, \dots, N-1$).
3. **Sequential (Descending)**: Strictly reverse monotonic ($N-1, N-2, \dots, 0$).
4. **Zipfian / Skewed**: Heavy skew ($\theta = 0.99$).
5. **Gaussian Clustered**: Multi-modal clustered distribution (4 peaks).

---

## 🚀 How to Run

### Option 1: Quick Run (200k keys / test)
```bash
./build/insert_benchmark --quick
```

### Option 2: Standard Benchmark (1M keys / test, 3 trials per test)
```bash
./build/insert_benchmark --n 1000000 --repeats 3
```

### Option 3: Heavy Stress Test (5M keys / test)
```bash
./build/insert_benchmark --heavy
```

---

## 📈 Generate Publication-Ready Plots
```bash
./venv/bin/python benchmark/insert/plot_results.py benchmark/insert/results/insert_results.csv
```
Plots are automatically saved to `benchmark/insert/results/`:
- `insert_throughput.png` (MOps/sec)
- `insert_latency.png` (ns/op)
- `insert_memory.png` (Bytes/key)
