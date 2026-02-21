# MaxCompression 🗜️

> **A revolutionary file compression library for humanity.**

MaxCompression is a cross-platform, lossless compression library written in C99.
It aims to push compression ratios beyond the current state of the art by fusing
techniques from information theory, nature-inspired algorithms, fractal mathematics,
and predictive coding.

## Quick Start

### Build

```bash
cmake -S . -B build
cmake --build build
```

### Compress a file

```bash
./build/bin/mcx compress -l 6 myfile.txt
```

### Decompress

```bash
./build/bin/mcx decompress myfile.txt.mcx
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

## Benchmarks (v1.0.0)

MaxCompression `v1.0.0` features a industry-standard **Professional Evaluation Suite** with multi-threading support and automated visualization.

### Benchmark Methodology & Protocol

To guarantee a professional and reproducible evaluation, all benchmarks are subject to the following strict constraints:

1. **In-Memory Loading (Zero Disk I/O)**: Strict memory-to-memory throughput measurement via `ctypes`.
2. **Memory Isolation (Peak RSS)**: Precise tracking of peak memory usage and leak detection via `psutil`.
3. **Weissman Score Integration**: Unified performance metric comparing ratio and speed against industry baselines.
4. **Multi-Thread Scaling**: Native OpenMP benchmarks for MCX and threaded Zstd comparison (1 to N cores).
5. **Automation & Reporting**: Auto-download of corpora, JSON/CSV/MD exports, and automatic PNG plotting.

### Running the Full Suite

Run the fully automated evaluation (including corpus download and plotting):

```powershell
python benchmarks/pro_bench.py --iter 5 --threads 1,4 --plot --export-md benchmark_results.md
```

The results include **Calgary**, **Canterbury**, **Silesia**, and the massive **Enwik9 (1GB)** corpora.

### Visualization Example
The suite generates efficiency curves (Ratio vs Speed) and Scaling graphs in the `./plots/` directory, allowing for immediate visual analysis of algorithmic behavior.

### Dataset: Calgary Corpus (3.0 MB - Structured & Text)
| Algorithm | Ratio | Comp Speed | Decomp Speed | Peak RAM |
|-----------|-------|------------|--------------|----------|
| **MCX Level 3**      |  **2.36x** |  186 MB/s | 292 MB/s | 1.3 MB |
| **MCX Level 9**      |  **2.47x** |  188 MB/s | 297 MB/s | 1.2 MB |
| LZ4 (Fast)           |   1.94x | 673 MB/s  | 1999 MB/s | 1.6 MB |
| Zstd (Level 3)       |   3.10x | 290 MB/s  | 1069 MB/s | 1.0 MB |

> MaxCompression's custom LZ77 Hash-Chain match finder consistently delivers robust compression ratios outperforming standard LZ4 pipelines, while easily handling multiple hundreds of megabytes per second natively in memory.

### Dataset: Canterbury Corpus (2.8 MB)
| Algorithm | Ratio | Comp Speed | Decomp Speed | Peak RAM |
|-----------|-------|------------|--------------|----------|
| **MCX Level 3**      | **2.87x** | 208 MB/s  | 344 MB/s | 0.9 MB |
| **MCX Level 9**      | **2.99x** | 206 MB/s | 349 MB/s | 0.9 MB |
| LZ4 (Fast)           |  2.29x | 712 MB/s | 1837 MB/s | 1.2 MB |
| Zstd (Level 3)       |  4.44x | 346 MB/s | 1180 MB/s | 0.6 MB |

> With continuous native bounds-safeguards added to our SIMD 16-byte wild-copy loops, our unrolled C decompressor ensures enterprise-ready stability at top speeds.

### Dataset: Enwik8 (100 MB - Natural Language / XML)
| Algorithm | Ratio | Comp Speed | Decomp Speed | Peak RAM |
|-----------|-------|------------|--------------|----------|
| **MCX Level 3**      | **2.13x** | 189 MB/s | 266 MB/s | 44.7 MB |
| **MCX Level 9**      | **2.23x** | 185 MB/s | 278 MB/s | 42.7 MB |
| LZ4 (Fast)           |  1.75x | 587 MB/s | 2161 MB/s | 54.6 MB |
| Zstd (Level 3)       |  2.82x | 271 MB/s | 984 MB/s | 33.8 MB |

### Dataset: Silesia Corpus (211 MB - Mixed Use)
| Algorithm | Ratio | Comp Speed | Decomp Speed | Peak RAM |
|-----------|-------|------------|--------------|----------|
| **MCX Level 3**      | **2.54x** | 224 MB/s | 324 MB/s | 79.5 MB |
| **MCX Level 9**      | **2.62x** | 218 MB/s | 334 MB/s | 77.2 MB |
| LZ4 (Fast)           |  2.10x | 806 MB/s | 2022 MB/s | 96.2 MB |
| Zstd (Level 3)       |  3.21x | 384 MB/s | 1176 MB/s | 63.1 MB |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      MCX v1.0 Pipeline                      │
├─────────────────────────────────────────────────────────────┤
│   Stage 0: Block Analyzer (entropy + structure profiling)   │
│            → Routes each block to optimal pipeline          │
├──────────────────────┬──────────────────────────────────────┤
│   FAST PATH (LZ)     │   BEST PATH (BWT+CM)                 │
│ ┌──────────────────┐ │ ┌──────────────────────────────────┐ │
│ │ Hash Match Find  │ │ │ BWT (SA-IS) → MTF → RLE          │ │
│ │ (4-byte hash,    │ │ │ → CM-rANS entropy                │ │
│ │  lazy parsing)   │ │ │ (Deep contextual probability)    │ │
│ ├──────────────────┤ │ └──────────────────────────────────┘ │
│ │ tANS/FSE Encoder │ │                                      │
│ ├──────────────────┤ │                                      │
│ │ SIMD Copy Loops  │ │                                      │
│ └──────────────────┘ │                                      │
├──────────────────────┴──────────────────────────────────────┤
│   Stage N: Frame/Block Multiplexor (constant-memory stream) │
└─────────────────────────────────────────────────────────────┘
```

## Project Structure

| Directory  | Purpose                                  |
|------------|------------------------------------------|
| `include/` | Public API header (`maxcomp/maxcomp.h`)  |
| `lib/`     | Library source (all modules)             |
| `cli/`     | Command-line tool (`mcx`)                |
| `tests/`   | Unit and round-trip tests                |
| `benchmarks/` | Performance benchmarks                |
| `docs/`    | Research and design documents            |

## API

```c
#include <maxcomp/maxcomp.h>

size_t mcx_compress(dst, dst_cap, src, src_size, level);
size_t mcx_decompress(dst, dst_cap, src, src_size);
```

## License

GNU General Public License v3.0 (GPL-3.0) — Free for everyone, forever. Prevents proprietary lock-in and abuse by larger corporate entities.
