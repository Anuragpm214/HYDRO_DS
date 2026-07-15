#!/usr/bin/env python3
import subprocess
import re
import os
import matplotlib.pyplot as plt
from collections import defaultdict
import statistics

CONFIGS = ["hydrods", "alex", "csb", "rbtree", "pma"]
SIZES = [1000000, 5000000, 10000000]
RUNS = 2

def build_project():
    print("Building project...")
    os.makedirs("../build", exist_ok=True)
    subprocess.run(["cmake", "-DCMAKE_BUILD_TYPE=Release", ".."], cwd="../build", check=True)
    subprocess.run(["make", "master_benchmark", f"-j{os.cpu_count()}"], cwd="../build", check=True)

def parse_output(output):
    metrics = {}
    for line in output.split('\n'):
        if "Insert Time:" in line: metrics["Insert (s)"] = float(re.search(r"Insert Time: ([\d.]+) s", line).group(1))
        elif "Search Time:" in line: metrics["Search (s)"] = float(re.search(r"Search Time: ([\d.]+) s", line).group(1))
        elif "Delete Time:" in line: metrics["Delete (s)"] = float(re.search(r"Delete Time: ([\d.]+) s", line).group(1))
        elif "Small Range:" in line: metrics["Range Small (s)"] = float(re.search(r"Small Range: ([\d.]+) s", line).group(1))
        elif "Med Range  :" in line: metrics["Range Med (s)"] = float(re.search(r"Med Range  : ([\d.]+) s", line).group(1))
        elif "Large Range:" in line: metrics["Range Large (s)"] = float(re.search(r"Large Range: ([\d.]+) s", line).group(1))
        elif "Memory Used:" in line: metrics["Memory (MB)"] = float(re.search(r"Memory Used: (\d+) MB", line).group(1))
        
        elif "insn per cycle" in line:
            m = re.search(r"([\d.]+)\s+insn per cycle", line)
            if m: metrics["IPC"] = float(m.group(1))
            
    # L1 and Branch misses
    l1_miss = re.search(r"([\d,]+)\s+L1-dcache-load-misses", output)
    b_miss = re.search(r"([\d,]+)\s+branch-misses", output)
    if l1_miss: metrics["L1 Misses (M)"] = float(l1_miss.group(1).replace(',','')) / 1e6
    if b_miss: metrics["Branch Misses (M)"] = float(b_miss.group(1).replace(',','')) / 1e6
    
    return metrics

def run_benchmarks():
    build_project()
    perf_cmd = ["perf", "stat", "-e", "instructions,cycles,L1-dcache-loads,L1-dcache-load-misses,branches,branch-misses"]
    
    all_data = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    
    for size in SIZES:
        print(f"--- Size {size} ---")
        for cfg in CONFIGS:
            for r in range(RUNS):
                cmd = perf_cmd + ["../build/master_benchmark", "--mode", cfg, "--n", str(size)]
                proc = subprocess.run(cmd, capture_output=True, text=True)
                metrics = parse_output(proc.stdout + proc.stderr)
                for k, v in metrics.items():
                    all_data[size][cfg][k].append(v)
            print(f"Done: {cfg}")
            
    return all_data

def generate_plots(all_data):
    os.makedirs("plots", exist_ok=True)
    metrics_to_plot = ["Insert (s)", "Search (s)", "Range Med (s)", "Delete (s)", "Memory (MB)"]
    
    # Adding micro-arch metrics if available
    if "IPC" in all_data[SIZES[0]][CONFIGS[0]]:
        metrics_to_plot.extend(["IPC", "L1 Misses (M)", "Branch Misses (M)"])

    sizes = SIZES
    size_labels = ["1M", "5M", "10M"]
    
    for metric in metrics_to_plot:
        plt.figure(figsize=(10, 6))
        
        for cfg in CONFIGS:
            y_vals = []
            for size in sizes:
                vals = all_data[size][cfg].get(metric, [])
                y_vals.append(statistics.median(vals) if vals else 0)
                
            plt.plot(size_labels, y_vals, marker='o', label=cfg.upper(), linewidth=2)
            
            # Add text labels on points
            for i, y in enumerate(y_vals):
                if y > 0:
                    plt.text(i, y, f"{y:.2f}", fontsize=9, ha='right', va='bottom')
        
        plt.title(f"{metric} Comparison", fontsize=14, fontweight='bold')
        plt.xlabel("Dataset Size", fontsize=12)
        plt.ylabel(metric, fontsize=12)
        plt.legend()
        plt.grid(True, linestyle='--', alpha=0.6)
        
        plt.savefig(f"plots/{metric.replace(' ', '_').replace('(', '').replace(')', '').replace('/', '')}.png", dpi=200)
        plt.close()

def main():
    print("Starting master benchmark...")
    data = run_benchmarks()
    generate_plots(data)
    print("Plots generated in benchmark/plots/")

if __name__ == "__main__":
    main()
