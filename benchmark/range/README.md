# HydroDS Range Query Benchmark Suite

A standalone, unbiased range query benchmark suite evaluating scan performance across index architectures.

## 🎯 What it Evaluates
- **HydroDS (`C=256`)**: Contiguous block scan across 256-element L1-resident buckets.
- **HydroDS-Eytzinger**: Eytzinger shadow array range scan.
- **ALEX**: Learned Index (SIGMOD 2020).
- **TLX B-Tree**: Cache-efficient B+ tree.
- **PGM-Index**: Piecewise Geometric Model Index (PVLDB 2020).
- **`std::multiset`**: Standard Red-Black Tree baseline.

## 📊 Workload Scenarios
1. **Small Scans (Len = 10)**: Short point scans.
2. **Medium Scans (Len = 100)**: Scans spanning 1-2 buckets.
3. **Large Scans (Len = 1,000)**: Multi-bucket scans.
4. **Extra Large Scans (Len = 10,000)**: Deep scans measuring raw contiguous memory scan bandwidth.

---

## 🚀 How to Run

```bash
# One-command build, execute, & plot
./benchmark/range/run_benchmark.sh

# Custom execution (e.g. 1M keys dataset, 50k range queries)
./build/range_benchmark --n 1000000 --queries 50000 --repeats 3
./venv/bin/python benchmark/range/plot_results.py benchmark/range/results/range_results.csv
```
Outputs saved to `benchmark/range/results/`:
- `range_scan_throughput.png`
- `range_latency.png`
- `range_results.csv`
