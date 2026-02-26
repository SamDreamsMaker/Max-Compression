# MaxCompression

> A high-performance, lossless compression library written in C99.

MaxCompression is a cross-platform compression library that combines classical
information-theory techniques (LZ77, tANS/FSE, BWT) with modern CPU optimisations
(SIMD SSE4.1, cache prefetching, instruction-level parallelism) to deliver strong
compression ratios at practical speeds.

## Quick Start

### Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Compress / Decompress

```bash
./build/bin/mcx compress -l 6 myfile.txt
./build/bin/mcx decompress myfile.txt.mcx
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

## Compression Levels

| Level | Strategy | Notes |
|-------|----------|-------|
| 1–3   | LZ77 greedy + tANS | Fastest; good for real-time |
| 4–9   | LZ77 lazy HC + tANS | Better ratio, still fast |
| 10–14 | BWT + MTF + RLE + rANS | High ratio; ~10× slower than L9 |
| 15–22 | BWT + MTF + RLE + CM-rANS | Maximum ratio; archival use |

The block analyser automatically routes incompressible data (entropy > 7.5 bits)
to STORE mode at BWT levels. LZ levels always attempt compression first.

## Benchmarks (v1.1.0)

> Intel Raptor Lake, Windows 11, single-threaded, in-memory.
> `benchmarks/pro_bench.py --iter 5 --threads 1`

### Calgary Corpus (3.0 MB)
| Algorithm | Ratio | Comp (MB/s) | Decomp (MB/s) |
|-----------|-------|-------------|---------------|
| **MCX L3** | 2.28x | 112 | **479** |
| **MCX L9** | 2.38x | 112 | **494** |
| **MCX-fast** | 1.82x | 262 | 798 |
| LZ4 fast | 1.91x | 664 | 2189 |
| zstd-3 | 2.99x | 288 | 1150 |

### Canterbury Corpus (2.7 MB)
| Algorithm | Ratio | Comp (MB/s) | Decomp (MB/s) |
|-----------|-------|-------------|---------------|
| **MCX L3** | 2.86x | 139 | **570** |
| **MCX L9** | 2.98x | 137 | **577** |
| **MCX-fast** | 2.10x | 366 | 847 |
| LZ4 fast | 2.29x | 727 | 2324 |
| zstd-3 | 4.45x | 345 | 1188 |

### Silesia Corpus (202 MB)
| Algorithm | Ratio | Comp (MB/s) | Decomp (MB/s) |
|-----------|-------|-------------|---------------|
| **MCX L3** | 2.53x | 129 | **538** |
| **MCX L9** | 2.60x | 130 | **543** |
| **MCX-fast** | 2.03x | 274 | 822 |
| LZ4 fast | 2.10x | 789 | 1937 |
| zstd-3 | 3.20x | 373 | 1117 |

### Enwik8 (95 MB — natural language)
| Algorithm | Ratio | Comp (MB/s) | Decomp (MB/s) |
|-----------|-------|-------------|---------------|
| **MCX L3** | 2.12x | 106 | **427** |
| **MCX L9** | 2.22x | 105 | **447** |
| **MCX L12** | 2.76x | 18 | 35 |
| LZ4 fast | 1.75x | 597 | 1931 |
| zstd-3 | 2.82x | 273 | 1009 |

**v1.1 vs v1.0 highlights:** decompression speed +64% (tANS 4-stream ILP),
MCX-fast ratio +28% (ctypes struct fix), MCX L12 enwik8 +0.19x (fitness fix).

Run the full suite yourself:

```bash
python benchmarks/pro_bench.py --iter 5 --threads 1 --export-md benchmark_results.md
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   MCX v1.1 Pipeline                         │
├─────────────────────────────────────────────────────────────┤
│  Stage 0: Block Analyser (entropy + structure profiling)    │
│           → Routes each block to optimal pipeline           │
├──────────────────────────┬──────────────────────────────────┤
│  LZ PATH (L1-9)          │  BWT PATH (L10-22)               │
│  ┌──────────────────┐    │  ┌──────────────────────────────┐ │
│  │ SSE4.1 dual-hash │    │  │ BWT (SA-IS) → MTF → RLE      │ │
│  │ match finder     │    │  │ → rANS / CM-rANS entropy     │ │
│  │ + repcode stack  │    │  │   (genetic pipeline select)  │ │
│  ├──────────────────┤    │  └──────────────────────────────┘ │
│  │ 4-stream tANS/   │    │                                   │
│  │ FSE entropy      │    │                                   │
│  │ (interleaved 4x) │    │                                   │
│  └──────────────────┘    │                                   │
├──────────────────────────┴──────────────────────────────────┤
│  Frame/Block Multiplexor · OpenMP block parallelism         │
└─────────────────────────────────────────────────────────────┘
```

### Key Algorithms (v1.1)

- **tANS (Asymmetric Numeral Systems)**: table-based entropy coder approaching
  Shannon entropy; 4-stream interleaved variant exploits instruction-level
  parallelism for high decompression throughput.
- **Multi-stream LZ77**: separates literals, literal-lengths, match-lengths, and
  offsets into independent FSE streams for better per-stream entropy modelling.
  Available via `mcx_lzfse_compress` / `mcx_lzfse_decompress`.
- **Repcode stack**: caches the 3 most recent match offsets; repeated-offset
  matches cost 1 byte instead of 2-3.
- **SIMD SSE4.1 dual hash**: two Knuth hashes computed in a single `_mm_mullo_epi32`
  pass; cache-line prefetch 16 positions ahead hides L2 latency.
- **BWT (SA-IS)**: suffix-array Burrows-Wheeler transform at O(n) time/space.
- **Genetic pipeline optimizer**: evolves the BWT-path genome (delta, MTF, RLE,
  entropy stage) per block.

## Project Structure

| Directory | Purpose |
|-----------|---------|
| `include/maxcomp/` | Public API header |
| `lib/` | Library source (all modules) |
| `lib/entropy/` | tANS/FSE, Huffman, rANS, CM-rANS |
| `lib/lz/` | LZ77 engines (greedy, lazy, multi-stream) |
| `lib/preprocess/` | BWT, MTF, RLE, delta |
| `lib/optimizer/` | Genetic algorithm for pipeline selection |
| `lib/simd/` | SSE4.1 / AVX2 helpers |
| `cli/` | `mcx` command-line tool |
| `tests/` | Unit, round-trip, and level-validation tests |
| `benchmarks/` | Professional benchmark suite (Python) |
| `docs/` | Research and design documents |

## API

```c
#include <maxcomp/maxcomp.h>

/* One-shot compression / decompression */
size_t mcx_compress  (void* dst, size_t dst_cap, const void* src, size_t src_size, int level);
size_t mcx_decompress(void* dst, size_t dst_cap, const void* src, size_t src_size);

/* Multi-stream LZ77+FSE (experimental) */
size_t mcx_lzfse_compress  (void* dst, size_t dst_cap, const void* src, size_t src_size);
size_t mcx_lzfse_decompress(void* dst, size_t dst_cap, const void* src, size_t src_size);

/* Error checking */
int mcx_is_error(size_t result);
const char* mcx_get_error_name(size_t result);
```

## License

GNU General Public License v3.0 (GPL-3.0) — free for everyone, forever.
Prevents proprietary lock-in and abuse by larger corporate entities.

See [CHANGELOG.md](CHANGELOG.md) for the full release history.
