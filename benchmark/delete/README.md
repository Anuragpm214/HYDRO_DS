# HydroDS Delete (Erase) Benchmark Suite

A standalone, unbiased delete benchmark suite evaluating item removal across index architectures.

## 🎯 What it Evaluates
- **HydroDS (`C=256`)**: Reverse fluid pressure flow (`flow(i-1, i)` under underflow).
- **HydroDS-Eytzinger**: Eytzinger shadow array updates.
- **ALEX**: Learned Index (SIGMOD 2020).
- **TLX B-Tree**: Cache-efficient B+ tree.
- **PGM-Index**: Piecewise Geometric Model Index (PVLDB 2020).
- **`std::multiset`**: Standard Red-Black Tree baseline.

## 📊 Workload Scenarios
1. **Random Erase**: Deleting 50% randomly chosen keys from a 1M inserted key index.
2. **Sequential (Asc)**: Deleting first N/2 keys in ascending order.
3. **Sequential (Desc)**: Deleting first N/2 keys in descending order.
4. **Non-Existing Keys**: Attempting to erase keys not present in dataset.

---

## 🚀 How to Run

```bash
# One-command build, execute, & plot
./benchmark/delete/run_benchmark.sh

# Custom execution (e.g. 2M keys dataset, 1M deletes)
./build/delete_benchmark --n 2000000 --deletes 1000000 --repeats 3
./venv/bin/python benchmark/delete/plot_results.py benchmark/delete/results/delete_results.csv
```
Outputs saved to `benchmark/delete/results/`:
- `delete_throughput.png`
- `delete_latency.png`
- `delete_results.csv`
