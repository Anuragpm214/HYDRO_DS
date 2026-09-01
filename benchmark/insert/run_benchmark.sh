#!/usr/bin/env bash
set -e

# ==============================================================================
# run_benchmark.sh — One-command build and execute script for insertion benchmark
# ==============================================================================

# Move to repository root
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

echo "=========================================================="
echo " [1/3] Building HydroDS Insert Benchmark..."
echo "=========================================================="
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target insert_benchmark -j$(nproc)

echo ""
echo "=========================================================="
echo " [2/3] Running Insert Benchmark Suite (5 runs averaged)..."
echo "=========================================================="
# Pass any CLI flags forwarded to this script (default: 1M keys, 5 trials)
if [ $# -eq 0 ]; then
    ./build/insert_benchmark --n 1000000 --repeats 5
else
    ./build/insert_benchmark "$@"
fi

echo ""
echo "=========================================================="
echo " [3/3] Generating Publication Plots..."
echo "=========================================================="
if [ -f "./venv/bin/python" ]; then
    ./venv/bin/python benchmark/insert/plot_results.py benchmark/insert/results/insert_results.csv
elif command -v python3 &>/dev/null; then
    python3 benchmark/insert/plot_results.py benchmark/insert/results/insert_results.csv || echo "Python plotting skipped."
fi

echo ""
echo " Done! Results & plots saved in: benchmark/insert/results/"
