<div align="center">
  <h1>HydroDS</h1>
  <p><strong>A Cache-Conscious, Branchless Data Structure for Extreme Read Performance</strong></p>
</div>

HydroDS is a cache-conscious index structure designed to provide state-of-the-art point query and range scan latencies. Inspired by fluid dynamics, it relies on pressure-based element flowing across flat, cache-aligned memory blocks instead of traditional tree-based node splitting.

By replacing standard binary searches with **purely branchless execution (via `cmov` instructions)**, HydroDS practically eliminates CPU pipeline flushes caused by branch mispredictions, extracting maximum instructions-per-cycle (IPC) from modern deeply pipelined architectures.

---

## ⚡ Key Optimizations

- **Branchless Binary Search**: Point queries execute without branching, dropping search latencies by **~36%** over traditional search algorithms.
- **Cache-Line Aligned Buckets**: Intra-bucket operations use raw contiguous memory, ensuring optimal L1/L2 cache locality.
- **Multi-Threaded Scalability (Phase 2)**: An alternate concurrent variant (`hydrods_concurrent.hpp`) uses Optimistic Lock Coupling (OLC) to scale read operations almost linearly up to 16 threads.

---

## 📊 Performance & Evaluation

HydroDS was benchmarked against leading state-of-the-art structures including **ALEX (Learned Index)**, **CSB+Tree (TLX)**, **Red-Black Tree (std::set)**, and **PMA (Packed Memory Arrays)** up to 10M keys.

*Note: The complete theoretical methodology, architectural analysis, and hardware counter data (IPC, L1 Misses) are available in our primary research paper.*

### 🔍 1. Point Queries (Search Latency)
HydroDS achieves best-in-class read latency. The branchless design ensures that even as the dataset scales to 10M items, the CPU never stalls on pipeline flushes.

![Search Performance](benchmark/plots/Search_s.png)

### 🏎️ 2. Range Scans
Unlike pointer-chasing structures (B+-Trees, RBTrees) which suffer from severe memory fragmentation, HydroDS scans flat, contiguous memory.

![Range Queries](benchmark/plots/Range_Med_s.png)

### ✍️ 3. Update (Insert) Latency
HydroDS avoids the extreme, latency-spiking array reallocations seen in standard PMAs and remains highly competitive with highly tuned B-Trees.

![Insert Performance](benchmark/plots/Insert_s.png)

---
### 📈 Concurrent Scaling (Optimistic Lock Coupling)

> Thread scalability for 5 Million elements inserted and queried concurrently.

![Concurrent Scaling](benchmark/plots/Concurrent_Scaling.png)

---
<div align="center">
  <i>Developed for Advanced Cache-Conscious Indexing Research.</i>
</div>
