#!/usr/bin/env bash
set -e

# ==============================================================================
# run_benchmark.sh — One-command build and execute script for concurrency suite
# ==============================================================================

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

echo "=========================================================="
echo " [1/3] Building HydroDS Concurrency Benchmark..."
echo "=========================================================="
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target concurrency_benchmark -j$(nproc)

echo ""
echo "=========================================================="
echo " [2/3] Running Multi-Threaded Scalability Suite (1 - 16 Threads)..."
echo "=========================================================="
if [ $# -eq 0 ]; then
    ./build/concurrency_benchmark --n 1000000 --ops 2000000 --repeats 3
else
    ./build/concurrency_benchmark "$@"
fi

echo ""
echo "=========================================================="
echo " [3/3] Generating Scaling & Speedup Plots..."
echo "=========================================================="
if [ -f "./venv/bin/python" ]; then
    ./venv/bin/python benchmark/concurrency/plot_results.py benchmark/concurrency/results/concurrency_results.csv
elif command -v python3 &>/dev/null; then
    python3 benchmark/concurrency/plot_results.py benchmark/concurrency/results/concurrency_results.csv || echo "Python plotting skipped."
fi

echo ""
echo " Done! Results & plots saved in: benchmark/concurrency/results/"
