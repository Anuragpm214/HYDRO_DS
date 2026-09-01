#!/usr/bin/env bash
set -e

# ==============================================================================
# run_benchmark.sh — One-command build and execute script for delete benchmark
# ==============================================================================

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

echo "=========================================================="
echo " [1/3] Building HydroDS Delete Benchmark..."
echo "=========================================================="
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target delete_benchmark -j$(nproc)

echo ""
echo "=========================================================="
echo " [2/3] Running Delete Benchmark Suite (5 runs averaged)..."
echo "=========================================================="
if [ $# -eq 0 ]; then
    ./build/delete_benchmark --n 1000000 --deletes 500000 --repeats 5
else
    ./build/delete_benchmark "$@"
fi

echo ""
echo "=========================================================="
echo " [3/3] Generating Publication Plots..."
echo "=========================================================="
if [ -f "./venv/bin/python" ]; then
    ./venv/bin/python benchmark/delete/plot_results.py benchmark/delete/results/delete_results.csv
elif command -v python3 &>/dev/null; then
    python3 benchmark/delete/plot_results.py benchmark/delete/results/delete_results.csv || echo "Python plotting skipped."
fi

echo ""
echo " Done! Results & plots saved in: benchmark/delete/results/"
