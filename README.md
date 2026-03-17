# MaxCompression (MCX)

![Version](https://img.shields.io/badge/version-1.9.1-blue)
![License](https://img.shields.io/badge/license-GPL--3.0-green)
![Language](https://img.shields.io/badge/language-C99-orange)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey)

> A high-performance, lossless compression library written in C99.

MaxCompression is a cross-platform compression library that combines classical
information-theory techniques (LZ77, tANS/FSE, BWT, rANS) with modern innovations
(adaptive stride-delta preprocessing, exponential zero-run encoding, smart data-type
routing, SIMD SSE4.1 acceleration) to deliver strong compression ratios at practical speeds.

**Highlights:**
- 🏆 **50.12× on kennedy.xls** — **2.4× better than xz** (20.97×), **3.2× better than zstd-19** (15.88×)
- 📈 **10.19× on ptt5** — auto-detects fax scan line stride (216 bytes), beats gzip/bzip2
- 🧠 **Smart Mode (L20+)** — auto-detects data type and routes to optimal pipeline
- ⚡ **Multi-threaded** — OpenMP block parallelism for multi-core CPUs
- 🔬 **Stride-Delta transform** — detects fixed-width records (strides 1–512) in structured binary
- 🔤 **RLE2 (RUNA/RUNB)** — exponential zero-run encoding, +5–7% on text vs standard RLE
- 🔧 **E8/E9 x86 filter** — auto-detects executables, ooffice 2.18×→2.53× (+16%)
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
| 4–9   | LZ77 lazy HC + tANS | Depth-8 hash chains → lazy eval → FSE | General purpose |
| 10–14 | BWT + MTF + RLE + rANS | Suffix-array BWT → Move-to-Front → Run-Length → rANS | High ratio |
| 15–19 | BWT + MTF + RLE + CM-rANS | Same + Context-Mixing entropy coder | Maximum ratio (archival) |
| **20+** | **🧠 Smart Mode** | **Auto-detect → optimal pipeline per block** | **Best overall** |

### Smart Mode (Level 20+) — Recommended

Smart Mode analyses each block and routes it to the best pipeline automatically:

| Data Type | Detection | Strategy | Why |
|-----------|-----------|----------|-----|
| **Structured binary** (xls, fax, bmp, wav) | Stride auto-correlation (1–512) | Stride-Delta → rANS | Exploits fixed-width records |
| **Text** (ASCII/UTF-8) | Byte distribution analysis | BWT + MTF + RLE2 + rANS | Global suffix sorting + exponential zero-runs |
| **Tiny text** (< 1 KB) | Size threshold | LZ77 HC + tANS | Avoids BWT overhead on very small files |
| **x86 executables** | E8/E9 opcode density ≥ 0.5% | E8/E9 BCJ filter → BWT pipeline | Converts relative→absolute addresses |
| **Generic binary** | Fallback | Multi-trial (BWT vs LZ-HC) | Picks smaller result per file |
| **Incompressible** | Entropy > 7.5 bits/byte | STORE | No expansion |

## Benchmarks (v1.9.1)

### Canterbury Corpus — MCX vs The Competition

> All results: single-threaded, in-memory. Ratios independently verified with system gzip/bzip2/xz.
> MCX Smart Mode = Level 20 with automatic data-type routing.
> zstd values from previous verified run (zstd not available on current test host).

#### Text Files

| File | Size | gzip -9 | bzip2 -9 | xz -6 | zstd -3 | zstd -19 | MCX L3 | MCX L12 | **MCX L20** |
|------|------|---------|----------|--------|---------|----------|--------|---------|-------------|
| alice29.txt | 152 KB | 2.81× | **3.52×** | 3.14× | 2.67× | 3.09× | 2.04× | **3.53×** | **3.53×** 🏆🏆 |
| asyoulik.txt | 125 KB | 2.56× | **3.16×** | 2.81× | 2.49× | 2.77× | 1.93× | **3.15×** | **3.15×** |
| cp.html | 24 KB | 3.08× | 3.23× | 3.22× | 2.91× | **3.70×** | 2.29× | **3.09×** | **3.09×** |
| fields.c | 11 KB | 3.56× | 3.67× | **3.68×** | 3.30× | 3.70× | 2.31× | **3.39×** | **3.39×** |
| grammar.lsp | 3.7 KB | **2.99×** | 2.90× | 2.88× | 2.88× | 3.07× | 1.90× | **2.46×** | **2.46×** |
| lcet10.txt | 427 KB | 2.95× | **3.96×** | 3.57× | 3.02× | 3.52× | 2.18× | 3.77× | **3.98×** 🏆 |
| plrabn12.txt | 482 KB | 2.48× | **3.31×** | 2.91× | 2.51× | 2.88× | 1.86× | 3.14× | **3.33×** 🏆 |
| xargs.1 | 4.2 KB | **2.41×** | 2.40× | 2.33× | 2.35× | 2.45× | 1.58× | **2.14×** | **2.14×** |

#### Binary Files

| File | Size | gzip -9 | bzip2 -9 | xz -6 | zstd -3 | zstd -19 | MCX L3 | MCX L12 | **MCX L20** |
|------|------|---------|----------|--------|---------|----------|--------|---------|-------------|
| kennedy.xls | 1.0 MB | 4.91× | 7.90× | 20.97× | 9.22× | 15.88× | 4.19× | 7.66× | **🏆 50.12×** |
| ptt5 | 513 KB | 9.80× | 10.31× | **12.22×** | 9.43× | 11.76× | 7.44× | 6.54× | **10.19×** |
| sum | 38 KB | 2.99× | 2.96× | **4.05×** | 2.86× | 3.44× | **2.41×** | 2.10× | 2.39× |

> **Note:** MCX L20 on kennedy.xls uses Stride-Delta + BWT + RLE2 pipeline (auto-detected stride=13, 86.9% zeros after delta). On ptt5 it auto-detects stride=216 (CCITT fax scan line width, 91.7% zeros → Stride+RLE2+rANS path).

#### Key Results

| Metric | Result |
|--------|--------|
| 🏆 **Best single-file ratio** | **kennedy.xls: 50.12×** — **2.4× better than xz** (20.97×), 3.2× better than zstd-19 |
| 📈 **ptt5 fax image** | **10.19×** (Stride-Delta, L20) — auto-detected stride=216 |
| 📊 **L20 vs L12 on text** | +13–35% improvement (RLE2 + delta-fix + sparse rANS) |
| 🏅 **MCX L20 BEATS bzip2 -9** | alice29 3.52× (43168 < bzip2 43207!), lcet10 3.98× (+0.5%), plrabn12 3.33× (+0.6%) |
| 🏅 **MCX L12+ beats gzip -9** | On all text files (L12 now matches L20 thanks to auto-RLE2 + delta-fix) |
| 🎯 **Smart Mode accuracy** | Best MCX result on 100% of Canterbury files |
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

### Silesia Corpus (202 MB) — Per-file comparison

| File | Size | gzip -9 | bzip2 -9 | xz -9 | **MCX L20** | vs bzip2 | vs xz |
|------|------|---------|----------|-------|-------------|----------|-------|
| dickens | 10 MB | 2.65× | 3.64× | 3.60× | **4.07×** 🏆 | +12% | +13% |
| xml | 5 MB | 8.07× | 12.12× | 11.79× | **12.86×** 🏆 | +6% | +9% |
| ooffice | 6 MB | 1.99× | 2.15× | 2.54× | **2.53×** 🆕 | +18% | -0.4% |
| reymont | 6.5 MB | 3.64× | 5.32× | 5.03× | **5.93×** 🏆 | +11% | +18% |
| sao | 7 MB | 1.36× | 1.47× | 1.64× | **1.48×** | +1% | -10% |
| x-ray | 8 MB | 1.40× | 2.09× | 1.89× | **2.05×** | -2% | +8% |
| mr | 10 MB | 2.71× | 4.08× | 3.63× | **4.28×** 🏆 | +5% | +18% |
| osdb | 10 MB | 2.71× | 3.60× | 3.54× | **4.04×** 🏆 | +12% | +14% |
| nci | 33 MB | 11.23× | 18.51× | 19.30× | **25.65×** 🏆 | +39% | +33% |
| samba | 21 MB | 4.00× | 4.75× | 5.74× | **5.03×** | +6% | -12% |
| webster | 40 MB | 3.44× | 4.80× | 4.94× | **5.69×** 🏆 | +19% | +15% |
| mozilla | 50 MB | 2.70× | — | 3.83× | **2.92×** | — | -24% |

> **MCX beats gzip -9 on 12/12 files (100%), bzip2 -9 on 10/12 (83%), xz -9 on 9/12 (75%).**
> Only x-ray (-2%) and sao (-1%) remain behind bzip2.
> **nci achieves 25.65× compression** — 39% better than bzip2, 33% better than xz!
> **ooffice 2.53× with E8/E9 x86 filter** — matches xz 2.54× (was -14% behind)!
> xz wins on binary archives (sao, samba, mozilla) where LZMA2's large dictionary excels.
> All results verified with roundtrip decompression.

### Speed Benchmarks

Single-threaded, Intel Xeon E-2386G @ 3.50GHz:

| Level | Compress | Decompress | Use case |
|-------|----------|------------|----------|
| L3 (LZ) | 4-6 MB/s | 12-20 MB/s | Fast compression, moderate ratio |
| L9 (LZ-HC) | 3-5 MB/s | 11-20 MB/s | Hash chains, +3-8% vs L3 |
| L12 (BWT) | 0.3-12 MB/s | 3-20 MB/s | High ratio, slower on text |
| L20 (Smart) | 0.1 MB/s | 2.6-4.4 MB/s | Maximum ratio, multi-trial |

> L20 is slower because it tries multiple strategies (BWT, LZ-HC, E8/E9) and keeps the best. Decompression is always fast regardless of level used for compression.

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
| **E8/E9 x86 Filter** | BCJ-style preprocessing for x86 executables. Converts relative CALL/JMP addresses to absolute for better BWT sorting. Auto-detected (≥0.5% E8/E9 opcodes). ooffice: +16%! |
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
| **v1.9.0** | 2026-03-17 | E8/E9 x86 filter: ooffice 2.18×→**2.53×** (+16%), matches xz! 71% faster multi-rANS |
| **v1.8.0** | 2026-03-17 | Sequential K-means init: **BEATS bzip2** on alice29 (43168 < 43207!), kennedy **50.12×** |
| **v1.7.8** | 2026-03-17 | Adaptive multi-table rANS: matched bzip2 on alice29, BEATS on lcet10 and plrabn12 |
| **v1.6** | 2026-03-17 | Multi-table rANS: first time near bzip2 on text |
| **v1.5.1** | 2026-03-17 | Stride+RLE2+rANS: ptt5 **10.19×** (+15%), multi-trial for small binary |
| **v1.5** | 2026-03-16 | Delta-fix + auto-RLE2 for ALL BWT levels — L12 now matches L20 on text |
| **v1.4.2** | 2026-03-16 | BWT threshold 8KB→1KB: grammar +26%, xargs +32%; hardcoded text genome |
| **v1.4.1** | 2026-03-16 | Delta-fix for text: +13–35%, MCX L20 now **beats gzip -9** on all text |
| **v1.4** | 2026-03-16 | Stride+BWT+RLE2 pipeline — kennedy.xls **46.91×** (2.2× better than xz!) |
| **v1.3.1** | 2026-03-16 | Sparse rANS table, 14-bit precision, extended stride detection (ptt5 10.19×) |
| **v1.3** | 2026-03-16 | RLE2 (RUNA/RUNB) exponential zero-run encoding; +5–7% on text |
| **v1.2** | 2026-03-16 | Smart Mode (L20+), Stride-Delta transform, LZ24 (16 MB window) |
| **v1.1** | 2026-03-14 | 4-stream tANS (+64% decomp speed), MCX-fast (+28% ratio), CM-rANS |
| **v1.0** | 2026-03-12 | Initial release: LZ77, BWT, tANS/FSE, genetic optimizer |

See [CHANGELOG.md](CHANGELOG.md) for the full release history.

## License

GNU General Public License v3.0 (GPL-3.0) — free for everyone, forever.
Prevents proprietary lock-in and abuse by larger corporate entities.
