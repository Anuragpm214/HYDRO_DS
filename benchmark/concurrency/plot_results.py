#!/usr/bin/env python3
"""
plot_results.py — Generates publication-ready multi-core scaling & speedup curves.
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "benchmark/concurrency/results/concurrency_results.csv"
    output_dir = os.path.dirname(csv_path)

    if not os.path.exists(csv_path):
        print(f"Error: CSV file not found at {csv_path}")
        sys.exit(1)

    df = pd.read_csv(csv_path)
    print(f"[+] Loaded {len(df)} rows from {csv_path}")

    sns.set_theme(style="whitegrid", font_scale=1.1)
    workloads = df["Workload"].unique()

    for wl in workloads:
        sub_df = df[df["Workload"] == wl]
        safe_wl = wl.replace(" ", "_").replace("/", "_").replace("%", "").lower()

        # 1. Throughput Scaling (MOps/sec vs Threads)
        plt.figure(figsize=(10, 6))
        sns.lineplot(
            data=sub_df,
            x="Threads",
            y="Throughput_MOps",
            hue="Structure",
            style="Structure",
            markers=True,
            dashes=False,
            linewidth=2.5,
            markersize=9
        )
        plt.title(f"Multi-Threaded Throughput Scaling: {wl}", fontsize=14, weight="bold", pad=15)
        plt.xlabel("Number of Threads (Cores)", fontsize=12)
        plt.ylabel("Throughput (Million Ops / sec)", fontsize=12)
        plt.xticks([1, 2, 4, 8, 16])
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot1 = os.path.join(output_dir, f"concurrent_throughput_{safe_wl}.png")
        plt.savefig(plot1, dpi=300)
        plt.close()
        print(f"[+] Saved {plot1}")

        # 2. Speedup Factor (vs 1 Thread)
        plt.figure(figsize=(10, 6))
        sns.lineplot(
            data=sub_df,
            x="Threads",
            y="Speedup",
            hue="Structure",
            style="Structure",
            markers=True,
            dashes=False,
            linewidth=2.5,
            markersize=9
        )
        # Add ideal linear speedup reference line
        max_t = sub_df["Threads"].max()
        plt.plot([1, max_t], [1, max_t], 'k--', alpha=0.5, label="Ideal Linear (1x - 16x)")
        
        plt.title(f"Multi-Core Speedup Factor (S_N = T_N / T_1): {wl}", fontsize=14, weight="bold", pad=15)
        plt.xlabel("Number of Threads", fontsize=12)
        plt.ylabel("Speedup Factor", fontsize=12)
        plt.xticks([1, 2, 4, 8, 16])
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot2 = os.path.join(output_dir, f"concurrent_speedup_{safe_wl}.png")
        plt.savefig(plot2, dpi=300)
        plt.close()
        print(f"[+] Saved {plot2}")

    print("\n[+] All concurrent scaling plots generated successfully!")

if __name__ == "__main__":
    main()
