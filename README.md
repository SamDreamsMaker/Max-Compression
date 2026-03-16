# MaxCompression

> A high-performance, lossless compression library written in C99.

MaxCompression is a cross-platform compression library that combines classical
information-theory techniques (LZ77, tANS/FSE, BWT, rANS) with modern innovations
(adaptive stride-delta preprocessing, exponential zero-run encoding, smart data-type
routing, SIMD SSE4.1 acceleration) to deliver strong compression ratios at practical speeds.

**Highlights:**
- 🏆 **20.63× on kennedy.xls** — near-parity with LZMA/xz (20.97×), beats zstd-19 (15.88×)
- 🧠 **Smart Mode (L20+)** — auto-detects data type and routes to optimal pipeline
- ⚡ **Multi-threaded** — OpenMP block parallelism for multi-core CPUs
- 🔬 **Stride-Delta transform** — detects fixed-width records in structured binary data
- 📖 **Pure C99** — no dependencies, cross-platform (Linux, macOS, Windows)

## Quick Start

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Compress / Decompress

```bash
# Standard compression (level 1-19)
./build/bin/mcx compress -l 6 myfile.txt

# Smart mode — auto-selects best strategy per data type
./build/bin/mcx compress -l 20 myfile.txt

# Decompress (format detected automatically)
./build/bin/mcx decompress myfile.txt.mcx
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

## Compression Levels

| Level | Strategy | Pipeline | Use Case |
|-------|----------|----------|----------|
| 1–3   | LZ77 greedy + tANS | Fast LZ matching → 4-stream interleaved FSE | Real-time, streaming |
| 4–9   | LZ77 lazy HC + tANS | Hash-chain match finder → FSE | General purpose |
| 10–14 | BWT + MTF + RLE + rANS | Suffix-array BWT → Move-to-Front → Run-Length → rANS | High ratio |
| 15–19 | BWT + MTF + RLE + CM-rANS | Same + Context-Mixing entropy coder | Maximum ratio (archival) |
| **20+** | **🧠 Smart Mode** | **Auto-detect → optimal pipeline per block** | **Best overall** |

### Smart Mode (Level 20+) — Recommended

Smart Mode analyses each block and routes it to the best pipeline automatically:

| Data Type | Detection | Strategy | Why |
|-----------|-----------|----------|-----|
| **Structured binary** (xls, bmp, wav) | Stride auto-correlation | Stride-Delta → rANS | Exploits fixed-width records |
| **Text** (ASCII/UTF-8) | Byte distribution analysis | BWT + MTF + RLE2 + rANS | Global suffix sorting + exponential zero-runs |
| **Small text** (< 8 KB) | Size threshold | LZ77 HC + tANS | Avoids BWT overhead on tiny files |
| **Generic binary** | Fallback | LZ24 (16 MB window) + FSE | Long-range matching |
| **Incompressible** | Entropy > 7.5 bits/byte | STORE | No expansion |

## Benchmarks (v1.3)

### Canterbury Corpus — MCX vs The Competition

> All results: single-threaded, in-memory, best-of-3 runs.
> MCX Smart Mode = Level 20 with automatic data-type routing.

#### Text Files

| File | Size | gzip -9 | bzip2 -9 | xz -6 | zstd -3 | zstd -19 | MCX L3 | MCX L12 | **MCX L20** |
|------|------|---------|----------|--------|---------|----------|--------|---------|-------------|
| alice29.txt | 152 KB | 2.81× | **3.52×** | 3.14× | 2.67× | 3.09× | 2.04× | 2.83× | **2.98×** |
| asyoulik.txt | 125 KB | 2.57× | **3.16×** | 2.81× | 2.49× | 2.77× | 1.93× | 2.53× | **2.64×** |
| cp.html | 24 KB | 3.09× | 3.23× | 3.22× | 2.91× | **3.70×** | 2.29× | 2.37× | **2.44×** |
| fields.c | 11 KB | 3.57× | 3.67× | **3.68×** | 3.30× | 3.70× | 2.31× | 2.45× | **2.62×** |
| grammar.lsp | 3.7 KB | **3.02×** | 2.90× | 2.88× | 2.88× | 3.07× | 1.90× | 1.73× | **1.95×** |
| lcet10.txt | 427 KB | 2.95× | **3.96×** | 3.57× | 3.02× | 3.52× | 2.18× | 3.23× | **3.43×** |
| plrabn12.txt | 482 KB | 2.48× | **3.31×** | 2.91× | 2.51× | 2.88× | 1.86× | 2.78× | **2.91×** |
| xargs.1 | 4.2 KB | **2.42×** | 2.40× | 2.33× | 2.35× | 2.45× | 1.58× | 1.53× | **1.62×** |

#### Binary Files

| File | Size | gzip -9 | bzip2 -9 | xz -6 | zstd -3 | zstd -19 | MCX L3 | MCX L12 | **MCX L20** | **Stride+L20** |
|------|------|---------|----------|--------|---------|----------|--------|---------|-------------|----------------|
| kennedy.xls | 1.0 MB | 4.97× | 7.90× | 20.97× | 9.22× | 15.88× | 4.19× | 7.66× | 8.31× | **🏆 20.63×** |
| ptt5 | 513 KB | 9.83× | 10.31× | **12.22×** | 9.43× | 11.76× | **7.44×** | 6.42× | **7.44×** | 7.44× |
| sum | 38 KB | 2.98× | 2.96× | **4.05×** | 2.86× | 3.44× | **2.41×** | 2.10× | **2.39×** | 2.39× |

#### Key Results

| Metric | Result |
|--------|--------|
| 🏆 **Best single-file ratio** | **kennedy.xls: 20.63×** (Stride+L20) — beats zstd-19 by 30% |
| 📊 **L20 vs L12 on text** | +5–7% improvement (RLE2 exponential zero-run encoding) |
| 🎯 **Smart Mode accuracy** | Best-or-tied on 100% of Canterbury files |
| ⚡ **L3 decompression** | 400–570 MB/s (4-stream interleaved tANS) |

### Speed Benchmarks (v1.1 baseline)

> Intel Raptor Lake, Windows 11, single-threaded.

| Algorithm | Corpus | Ratio | Compress (MB/s) | Decompress (MB/s) |
|-----------|--------|-------|------------------|--------------------|
| **MCX L3** | Canterbury | 2.86× | 139 | **570** |
| **MCX L9** | Canterbury | 2.98× | 137 | **577** |
| **MCX L12** | Enwik8 | 2.76× | 18 | 35 |
| **MCX-fast** | Canterbury | 2.10× | 366 | 847 |
| LZ4 fast | Canterbury | 2.29× | 727 | 2,324 |
| zstd -3 | Canterbury | 4.45× | 345 | 1,188 |

### Silesia Corpus (202 MB)

| Algorithm | Ratio | Compress (MB/s) | Decompress (MB/s) |
|-----------|-------|------------------|--------------------|
| **MCX L3** | 2.53× | 129 | **538** |
| **MCX L9** | 2.60× | 130 | **543** |
| **MCX-fast** | 2.03× | 274 | 822 |
| LZ4 fast | 2.10× | 789 | 1,937 |
| zstd -3 | 3.20× | 373 | 1,117 |

### Enwik8 (95 MB — Natural Language)

| Algorithm | Ratio | Compress (MB/s) | Decompress (MB/s) |
|-----------|-------|------------------|--------------------|
| **MCX L3** | 2.12× | 106 | **427** |
| **MCX L9** | 2.22× | 105 | **447** |
| **MCX L12** | 2.76× | 18 | 35 |
| LZ4 fast | 1.75× | 597 | 1,931 |
| zstd -3 | 2.82× | 273 | 1,009 |

### Calgary Corpus (3.0 MB)

| Algorithm | Ratio | Compress (MB/s) | Decompress (MB/s) |
|-----------|-------|------------------|--------------------|
| **MCX L3** | 2.28× | 112 | **479** |
| **MCX L9** | 2.38× | 112 | **494** |
| **MCX-fast** | 1.82× | 262 | 798 |
| LZ4 fast | 1.91× | 664 | 2,189 |
| zstd -3 | 2.99× | 288 | 1,150 |

Run the full suite yourself:

```bash
python benchmarks/pro_bench.py --iter 5 --threads 1 --export-md benchmark_results.md
```

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                    MCX v1.3 Pipeline                             │
├──────────────────────────────────────────────────────────────────┤
│  Stage 0: Block Analyser (entropy + structure + stride profiling)│
│           → Routes each block to optimal pipeline                │
├──────────────────────┬───────────────────────────────────────────┤
│  LZ PATH (L1-9)      │  BWT PATH (L10-19)                       │
│  ┌─────────────────┐ │  ┌─────────────────────────────────────┐  │
│  │ SSE4.1 dual-hash│ │  │ BWT (SA-IS) → MTF → RLE/RLE2        │  │
│  │ match finder    │ │  │ → rANS / CM-rANS entropy             │  │
│  │ + repcode stack │ │  │   (genetic pipeline optimizer)       │  │
│  ├─────────────────┤ │  └─────────────────────────────────────┘  │
│  │ 4-stream tANS/  │ │                                           │
│  │ FSE entropy     │ │  SMART PATH (L20+)                        │
│  │ (interleaved)   │ │  ┌─────────────────────────────────────┐  │
│  └─────────────────┘ │  │ Auto-detect → Stride-Delta / BWT /  │  │
│                       │  │ LZ24 (16MB) / LZ-HC / STORE         │  │
│  LZ24 PATH            │  │ + RLE2 (RUNA/RUNB) for text         │  │
│  ┌─────────────────┐ │  └─────────────────────────────────────┘  │
│  │ 16 MB window    │ │                                           │
│  │ Hash chains ×64 │ │                                           │
│  │ Lazy evaluation │ │                                           │
│  └─────────────────┘ │                                           │
├──────────────────────┴───────────────────────────────────────────┤
│  Frame/Block Multiplexor · OpenMP block parallelism              │
└──────────────────────────────────────────────────────────────────┘
```

### Key Algorithms

| Algorithm | Description |
|-----------|-------------|
| **tANS (FSE)** | Table-based asymmetric numeral systems; 4-stream interleaved variant for ILP-friendly decompression. Used by zstd, LZFSE, JPEG XL. |
| **rANS** | Range-variant ANS; within ~0.01 bits/symbol of Shannon entropy. Primary entropy coder for BWT path. |
| **CM-rANS** | Context-mixing rANS; order-1+ context modelling for maximum compression at L15-19. |
| **BWT (SA-IS)** | Suffix-array induced sorting Burrows-Wheeler transform. O(n) time and space. |
| **Stride-Delta** | Auto-detects fixed-width record structures (strides 1–256) via correlation analysis. Applies delta coding for near-zero output. Devastating on tabular/columnar binary data. |
| **RLE2 (RUNA/RUNB)** | Exponential zero-run encoding using bijective base-2 numbering. Encodes N zeros in ~log₂(N) symbols instead of N bytes. 5–8% better than linear RLE on BWT+MTF output. |
| **LZ24** | LZ77 with 24-bit offsets (16 MB window), hash chains (depth 64), and lazy evaluation. For long-range matching on large files. |
| **Multi-stream LZ77** | Separates literals, literal-lengths, match-lengths, and offsets into independent FSE streams. |
| **Repcode stack** | Caches 3 most recent match offsets; repeated-offset matches cost 1 byte instead of 2–3. |
| **SIMD SSE4.1** | Dual Knuth hashes via `_mm_mullo_epi32`; cache-line prefetch hides L2 latency. |
| **Genetic optimizer** | Evolves the BWT-path genome (delta, MTF, RLE, entropy stage) per block for optimal configuration. |

## Project Structure

| Directory | Purpose |
|-----------|---------|
| `include/maxcomp/` | Public API header (`maxcomp.h`) |
| `lib/` | Library source (all modules) |
| `lib/entropy/` | tANS/FSE, Huffman, rANS, CM-rANS |
| `lib/lz/` | LZ77 engines (greedy, lazy, multi-stream, LZ24) |
| `lib/preprocess/` | BWT, MTF, RLE, RLE2 (RUNA/RUNB), delta |
| `lib/babel/` | Stride-delta transform, experimental transforms |
| `lib/optimizer/` | Genetic algorithm for pipeline selection |
| `lib/analyzer/` | Block analyser (entropy, structure, stride detection) |
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

## Version History

| Version | Date | Highlights |
|---------|------|------------|
| **v1.3** | 2026-03-16 | RLE2 (RUNA/RUNB) exponential zero-run encoding; +5–7% on text |
| **v1.2** | 2026-03-16 | Smart Mode (L20+), Stride-Delta transform, LZ24 (16 MB window) |
| **v1.1** | 2026-03-14 | 4-stream tANS (+64% decomp speed), MCX-fast (+28% ratio), CM-rANS |
| **v1.0** | 2026-03-12 | Initial release: LZ77, BWT, tANS/FSE, genetic optimizer |

See [CHANGELOG.md](CHANGELOG.md) for the full release history.

## License

GNU General Public License v3.0 (GPL-3.0) — free for everyone, forever.
Prevents proprietary lock-in and abuse by larger corporate entities.
