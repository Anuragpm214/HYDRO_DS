#!/usr/bin/env python3
"""
plot_results.py — Generates publication-ready figures from insert_benchmark CSV results,
including throughput, latency, memory, IPC, L1 cache miss %, LLC miss %, and branch miss %.
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "benchmark/insert/results/insert_results.csv"
    output_dir = os.path.dirname(csv_path)

    if not os.path.exists(csv_path):
        print(f"Error: CSV file not found at {csv_path}")
        sys.exit(1)

    df = pd.read_csv(csv_path)
    print(f"[+] Loaded {len(df)} rows from {csv_path}")

    # Set theme for academic style
    sns.set_theme(style="whitegrid", font_scale=1.1)
    palette = sns.color_palette("tab10", n_colors=df["Structure"].nunique())

    # 1. Throughput by Distribution
    plt.figure(figsize=(12, 6))
    ax = sns.barplot(
        data=df,
        x="Distribution",
        y="Throughput_MOps",
        hue="Structure",
        palette=palette
    )
    plt.title("Insertion Throughput Comparison Across Workloads (Higher is Better)", fontsize=14, weight="bold", pad=15)
    plt.ylabel("Throughput (Million Ops / sec)", fontsize=12)
    plt.xlabel("Key Distribution", fontsize=12)
    plt.xticks(rotation=15)
    plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
    plt.tight_layout()
    plot1_path = os.path.join(output_dir, "insert_throughput.png")
    plt.savefig(plot1_path, dpi=300)
    plt.close()
    print(f"[+] Saved throughput plot to {plot1_path}")

    # 2. Latency by Distribution
    plt.figure(figsize=(12, 6))
    ax = sns.barplot(
        data=df,
        x="Distribution",
        y="Latency_ns",
        hue="Structure",
        palette=palette
    )
    plt.title("Average Insertion Latency Across Workloads (Lower is Better)", fontsize=14, weight="bold", pad=15)
    plt.ylabel("Latency (ns / op)", fontsize=12)
    plt.xlabel("Key Distribution", fontsize=12)
    plt.xticks(rotation=15)
    plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
    plt.tight_layout()
    plot2_path = os.path.join(output_dir, "insert_latency.png")
    plt.savefig(plot2_path, dpi=300)
    plt.close()
    print(f"[+] Saved latency plot to {plot2_path}")

    # 3. Memory Footprint (Bytes per Key)
    plt.figure(figsize=(12, 6))
    ax = sns.barplot(
        data=df,
        x="Distribution",
        y="Bytes_Per_Key",
        hue="Structure",
        palette=palette
    )
    plt.title("Memory Overhead (Bytes per Key - Lower is Better)", fontsize=14, weight="bold", pad=15)
    plt.ylabel("Bytes / Key", fontsize=12)
    plt.xlabel("Key Distribution", fontsize=12)
    plt.xticks(rotation=15)
    plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
    plt.tight_layout()
    plot3_path = os.path.join(output_dir, "insert_memory.png")
    plt.savefig(plot3_path, dpi=300)
    plt.close()
    print(f"[+] Saved memory plot to {plot3_path}")

    # 4. IPC (Instructions Per Cycle)
    if "IPC" in df.columns and (df["IPC"] > 0).any():
        plt.figure(figsize=(12, 6))
        ax = sns.barplot(
            data=df,
            x="Distribution",
            y="IPC",
            hue="Structure",
            palette=palette
        )
        plt.title("Hardware IPC (Instructions Per Cycle - Higher is Better)", fontsize=14, weight="bold", pad=15)
        plt.ylabel("IPC", fontsize=12)
        plt.xlabel("Key Distribution", fontsize=12)
        plt.xticks(rotation=15)
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot_ipc_path = os.path.join(output_dir, "insert_ipc.png")
        plt.savefig(plot_ipc_path, dpi=300)
        plt.close()
        print(f"[+] Saved IPC plot to {plot_ipc_path}")

    # 5. L1 Cache Miss Rate (%)
    if "L1_Miss_Rate_Pct" in df.columns and (df["L1_Miss_Rate_Pct"] > 0).any():
        plt.figure(figsize=(12, 6))
        ax = sns.barplot(
            data=df,
            x="Distribution",
            y="L1_Miss_Rate_Pct",
            hue="Structure",
            palette=palette
        )
        plt.title("L1 Data Cache Miss Rate (%) (Lower is Better)", fontsize=14, weight="bold", pad=15)
        plt.ylabel("L1 D-Cache Miss Rate (%)", fontsize=12)
        plt.xlabel("Key Distribution", fontsize=12)
        plt.xticks(rotation=15)
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot_l1_path = os.path.join(output_dir, "insert_l1_miss.png")
        plt.savefig(plot_l1_path, dpi=300)
        plt.close()
        print(f"[+] Saved L1 cache miss plot to {plot_l1_path}")

    # 6. Branch Miss Rate (%)
    if "Branch_Miss_Rate_Pct" in df.columns and (df["Branch_Miss_Rate_Pct"] > 0).any():
        plt.figure(figsize=(12, 6))
        ax = sns.barplot(
            data=df,
            x="Distribution",
            y="Branch_Miss_Rate_Pct",
            hue="Structure",
            palette=palette
        )
        plt.title("Branch Misprediction Rate (%) (Lower is Better)", fontsize=14, weight="bold", pad=15)
        plt.ylabel("Branch Miss Rate (%)", fontsize=12)
        plt.xlabel("Key Distribution", fontsize=12)
        plt.xticks(rotation=15)
        plt.legend(bbox_to_anchor=(1.02, 1), loc="upper left", borderaxespad=0.)
        plt.tight_layout()
        plot_br_path = os.path.join(output_dir, "insert_branch_miss.png")
        plt.savefig(plot_br_path, dpi=300)
        plt.close()
        print(f"[+] Saved branch miss plot to {plot_br_path}")

    print("\n[+] All plots generated successfully!")

if __name__ == "__main__":
    main()
