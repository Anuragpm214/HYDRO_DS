#!/usr/bin/env python3
"""
plot_results.py — Generates publication-ready figures for range benchmark results,
including throughput, latency, IPC, L1 cache miss %, and branch miss %.
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "benchmark/range/results/range_results.csv"
    output_dir = os.path.dirname(csv_path)

    if not os.path.exists(csv_path):
        print(f"Error: CSV file not found at {csv_path}")
        sys.exit(1)

    df = pd.read_csv(csv_path)
    print(f"[+] Loaded {len(df)} rows from {csv_path}")

    sns.set_theme(style="whitegrid", font_scale=1.1)
    palette = sns.color_palette("tab10", n_colors=df["Structure"].nunique())

    # 1. Range Scan Bandwidth (Scanned Keys per sec - MOps/s)
    plt.figure(figsize=(12, 6))
    ax = sns.barplot(
        data=df,
        x="Workload",
        y="Scanned_MOps",
        hue="Structure",
        palette=palette
    )
    plt.title("Range Scan Throughput (Keys Scanned / Sec - Higher is Better)", fontsize=14, weight="bold", pad=15)
    plt.ylabel("Scan Throughput (Million Keys / sec)", fontsize=12)
    plt.xlabel("Range Scan Selectivity / Length", fontsize=12)
    plt.xticks(rotation=15)
    plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
    plt.tight_layout()
    plot1_path = os.path.join(output_dir, "range_scan_throughput.png")
    plt.savefig(plot1_path, dpi=300)
    plt.close()
    print(f"[+] Saved range scan throughput plot to {plot1_path}")

    # 2. Range Query Latency (us / query)
    plt.figure(figsize=(12, 6))
    ax = sns.barplot(
        data=df,
        x="Workload",
        y="Latency_us",
        hue="Structure",
        palette=palette
    )
    plt.title("Range Query Latency (Lower is Better)", fontsize=14, weight="bold", pad=15)
    plt.ylabel("Latency (µs / query)", fontsize=12)
    plt.xlabel("Range Scan Selectivity / Length", fontsize=12)
    plt.xticks(rotation=15)
    plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
    plt.tight_layout()
    plot2_path = os.path.join(output_dir, "range_latency.png")
    plt.savefig(plot2_path, dpi=300)
    plt.close()
    print(f"[+] Saved range latency plot to {plot2_path}")

    # 3. IPC
    if "IPC" in df.columns and (df["IPC"] > 0).any():
        plt.figure(figsize=(12, 6))
        ax = sns.barplot(
            data=df,
            x="Workload",
            y="IPC",
            hue="Structure",
            palette=palette
        )
        plt.title("Hardware IPC in Range Scans (Higher is Better)", fontsize=14, weight="bold", pad=15)
        plt.ylabel("IPC (Instructions Per Cycle)", fontsize=12)
        plt.xlabel("Range Scan Selectivity / Length", fontsize=12)
        plt.xticks(rotation=15)
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot_ipc_path = os.path.join(output_dir, "range_ipc.png")
        plt.savefig(plot_ipc_path, dpi=300)
        plt.close()
        print(f"[+] Saved IPC plot to {plot_ipc_path}")

    # 4. L1 Cache Miss %
    if "L1_Miss_Rate_Pct" in df.columns and (df["L1_Miss_Rate_Pct"] > 0).any():
        plt.figure(figsize=(12, 6))
        ax = sns.barplot(
            data=df,
            x="Workload",
            y="L1_Miss_Rate_Pct",
            hue="Structure",
            palette=palette
        )
        plt.title("L1 Data Cache Miss Rate (%) in Range Scans (Lower is Better)", fontsize=14, weight="bold", pad=15)
        plt.ylabel("L1 Miss Rate (%)", fontsize=12)
        plt.xlabel("Range Scan Selectivity / Length", fontsize=12)
        plt.xticks(rotation=15)
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot_l1_path = os.path.join(output_dir, "range_l1_miss.png")
        plt.savefig(plot_l1_path, dpi=300)
        plt.close()
        print(f"[+] Saved L1 miss plot to {plot_l1_path}")

    # 5. Branch Miss %
    if "Branch_Miss_Rate_Pct" in df.columns and (df["Branch_Miss_Rate_Pct"] > 0).any():
        plt.figure(figsize=(12, 6))
        ax = sns.barplot(
            data=df,
            x="Workload",
            y="Branch_Miss_Rate_Pct",
            hue="Structure",
            palette=palette
        )
        plt.title("Branch Misprediction Rate (%) in Range Scans (Lower is Better)", fontsize=14, weight="bold", pad=15)
        plt.ylabel("Branch Misprediction Rate (%)", fontsize=12)
        plt.xlabel("Range Scan Selectivity / Length", fontsize=12)
        plt.xticks(rotation=15)
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot_br_path = os.path.join(output_dir, "range_branch_miss.png")
        plt.savefig(plot_br_path, dpi=300)
        plt.close()
        print(f"[+] Saved branch miss plot to {plot_br_path}")

    print("\n[+] All range scan plots generated successfully!")

if __name__ == "__main__":
    main()
