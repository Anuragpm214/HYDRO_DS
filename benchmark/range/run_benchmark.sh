#!/usr/bin/env bash
set -e

# ==============================================================================
# run_benchmark.sh — One-command build and execute script for range benchmark
# ==============================================================================

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

echo "=========================================================="
echo " [1/3] Building HydroDS Range Benchmark..."
echo "=========================================================="
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target range_benchmark -j$(nproc)

echo ""
echo "=========================================================="
echo " [2/3] Running Range Benchmark Suite (5 runs averaged)..."
echo "=========================================================="
if [ $# -eq 0 ]; then
    ./build/range_benchmark --n 1000000 --queries 50000 --repeats 5
else
    ./build/range_benchmark "$@"
fi

echo ""
echo "=========================================================="
echo " [3/3] Generating Publication Plots..."
echo "=========================================================="
if [ -f "./venv/bin/python" ]; then
    ./venv/bin/python benchmark/range/plot_results.py benchmark/range/results/range_results.csv
elif command -v python3 &>/dev/null; then
    python3 benchmark/range/plot_results.py benchmark/range/results/range_results.csv || echo "Python plotting skipped."
fi

echo ""
echo " Done! Results & plots saved in: benchmark/range/results/"
