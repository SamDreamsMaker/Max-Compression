# MaxCompression Professional Benchmark Suite (v1.0.0)

This directory contains the professional benchmarking tools and standard databases used to rigorously measure the performance, memory footprint, and compression ratio of `libmaxcomp` compared to industry-standard solutions (`zstd`, `lz4`).

## Performance Methodology & Test Protocol

To ensure a laboratory-grade, reproducible evaluation, all benchmarks adhere to the following strict protocol:

1.  **In-Memory Streaming (Zero Disk I/O)**:
    All corpus data is pre-loaded into RAM before the timers start. C algorithms are interfaced via `ctypes` to measure pure memory-to-memory throughput. This eliminates OS disk cache and hardware latency noise.
2.  **Memory Isolation (Peak RSS)**:
    Peak memory usage (RSS) is measured using `psutil`. A garbage collection pass is forced before and after each run to define a clean baseline. The maximum allocation delta is captured to detect potential memory leaks.
3.  **Iteration Smoothing & Warm-Up**:
    Each test begins with a dummy "Warm-Up" pass to align CPU instruction caches. Final metrics are computed as an average of $N$ iterations (default $N=3$, recommended $N \ge 10$ for production reports).
4.  **Multi-Thread Scaling**:
    The suite supports testing across 1, 2, 4, 8, or $N$ threads. It measures how effectively the engine scales its throughput as core count increases.

## Standard Datasets (Corpora)

The following scientific datasets are automatically managed by `corpus_manager.py`:

*   **Calgary Corpus (3.0 MB)**: Historical reference for information theory papers.
*   **Canterbury Corpus (2.8 MB)**: Modern balanced dataset (text, binary, code).
*   **Enwik8 (100 MB)**: 100 million characters from Wikipedia (NLP/XML standard).
*   **Enwik9 (1.0 GB)**: 1 billion characters (Stress test for memory and big data).
*   **Silesia Corpus (211 MB)**: Representation of modern workloads (DBs, binaries, logs).

## Metric Interpretation

*   **Ratio**: (Original Size / Compressed Size). Higher is better.
*   **Comp/Decomp Speed (MB/s)**: Native throughput in megabytes of original data processed per second.
*   **Weissman Score**: A unified metric combining ratio and speed against a baseline (typically `zstd-3`). 
    $W = \frac{r}{r_{ref}} \frac{\log_{10}(T_{ref})}{\log_{10}(T)}$
*   **Var Rel**: Relative variance (Standard Deviation / Mean). Values $< 2\%$ indicate highly reliable results.

## Usage

### 1. Provisioning
Download and extract all datasets:
```powershell
python benchmarks/corpus_manager.py
```

### 2. Running the Suite
Execute the benchmark with 10 iterations and multi-thread testing:
```powershell
python benchmarks/pro_bench.py --iter 10 --threads 1,2,4,8 --plot --export-json results.json
```

### 3. Visualization
If `--plot` is enabled, the script generates PNG graphs in `./plots/` showing:
*   Ratio vs. Speed (Efficiency curve)
*   Thread Scaling (Throughput per core)

## Hardware Logging
The benchmark automatically detects and logs `Processor`, `Architecture`, and `OS Version` to the final reports to ensure results remain contextualized for peer review.
