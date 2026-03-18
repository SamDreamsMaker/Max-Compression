# MCX Architecture & Design

**Version:** 1.9.3  
**Last updated:** 2026-03-18

## Overview

MaxCompression (MCX) is a lossless compression library that combines multiple compression strategies under a unified API. The key design principle is **automatic strategy selection**: the library analyzes the input data and routes it through the optimal compression pipeline.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        MCX Public API                           │
│          mcx_compress() / mcx_decompress()                      │
├─────────────────────────────────────────────────────────────────┤
│                     Frame Encoder/Decoder                       │
│   Header (20 bytes) + Block multiplexer + CRC32                 │
├─────────────────────────────────────────────────────────────────┤
│                    Block Compression Loop                       │
│        (OpenMP parallel, up to 64 MB per block)                 │
├──────────┬──────────┬──────────┬──────────┬─────────────────────┤
│  LZ Path │ BWT Path │  Stride  │  LZRC    │  STORE              │
│  (L1-9)  │ (L10-19) │  (L20+)  │  (L26)   │  (incompressible)   │
├──────────┴──────────┴──────────┴──────────┴─────────────────────┤
│                    Smart Mode (L20)                              │
│   Analyzes → routes → multi-trial → keeps smallest              │
└─────────────────────────────────────────────────────────────────┘
```

## Compression Paths

### 1. LZ Path (Levels 1–9)

Classical LZ77 sliding-window compression with entropy coding.

```
Input → Match Finder → Token Stream → Entropy Coder → Output
```

**Match Finders:**
- L1–3: Greedy, single hash probe
- L4–8: Lazy evaluation, depth-8/16 hash chains (64 KB window)
- L9: Same as L8, but picks AAC over FSE when AAC produces smaller output

**Entropy Coders:**
- tANS (table Asymmetric Numeral Systems): 4-stream interleaved for fast decompression
- FSE (Finite State Entropy): Optimized tANS variant
- AAC (Adaptive Arithmetic Coding): Order-1 adaptive model with Fenwick-tree decode

**Block types:** `0xAA` (LZ16+FSE), `0xAB` (LZ16+RAW), `0xAE` (LZ16+AAC), `0xAC`/`0xAD`/`0xAF` (LZ24 variants)

### 2. BWT Path (Levels 10–19)

Burrows-Wheeler Transform followed by Move-to-Front and entropy coding.

```
Input → BWT (SA-IS) → MTF → RLE2 (RUNA/RUNB) → Multi-table rANS → Output
```

**Components:**
- **BWT via SA-IS:** O(n) suffix array construction, then BWT from SA
- **MTF (Move-to-Front):** Converts BWT output to small integers (lots of zeros)
- **RLE2:** Bijective base-2 encoding of zero runs — encodes N zeros in ~log₂(N) symbols
- **Multi-table rANS:** 4–6 frequency tables clustered with K-means, group size 50

**Genome byte:** Each block encodes its pipeline configuration in a single byte, allowing per-block optimization via the genetic algorithm (L10–14).

### 3. Stride-Delta Path (L20 Auto-detect)

For structured binary data with fixed-width records (spreadsheets, images, audio).

```
Input → Stride Detection → Delta at stride → RLE2 + rANS → Output
```

**Detection:** Autocorrelation analysis finds optimal stride (1–512 bytes).  
**Example:** kennedy.xls has stride=13 (record width). After delta, 86.9% zeros → 50.1× compression.

### 4. LZRC Path (Level 26 / L20 Multi-trial)

v2.0 engine: LZ77 with binary tree match finder and adaptive range coder.

```
Input → BT Match Finder (16 MB window) → Lazy Evaluation → Range Coder → Output
```

**Components:**
- **Binary tree match finder:** O(log n) per position, configurable window up to 64 MB
- **Lazy evaluation:** Defers match, checks if next position has a better match
- **4 rep distances:** Reusing a cached distance costs ~2 bits vs ~15–25 bits for a new one
- **Distance slot model:** 64 slots with context-coded extra bits and alignment bits
- **Adaptive range coder:** Subbotin-style, carry-based, 11-bit probability adaptation

**Block type:** `0xB0`

### 5. Smart Mode (Level 20)

Combines all paths with automatic routing and multi-trial selection.

```
Input → Analyzer → {BWT, LZ-HC, LZRC, Stride, E8E9} → Keep Smallest
```

**Analyzer detects:**
- Text vs binary (byte distribution)
- Structured binary (stride autocorrelation)
- x86 executables (E8/E9 opcode density)
- High entropy / incompressible

**Multi-trial:** Tries multiple strategies and keeps the smallest result. This ensures L20 ≥ max(L9, L12, L26) on every file.

## Preprocessing Filters

### E8/E9 x86 Filter
- Converts relative CALL/JMP addresses to absolute
- Auto-detected when ≥ 0.5% of bytes are 0xE8 or 0xE9
- Applied at frame level (before block compression)
- ooffice: 2.18× → 2.53× (+16%)

### RLE2 (RUNA/RUNB)
- Exponential zero-run encoding using bijective base-2 numbering
- Run of N zeros → ~log₂(N) symbols (vs N bytes with standard RLE)
- Reduces MTF output by 5–8% on typical BWT text

## Frame Format

See [`FORMAT.md`](FORMAT.md) for the complete wire format specification.

**Key design decisions:**
- 20-byte fixed header with CRC32
- Per-block strategy (block type byte determines decompression path)
- Blocks are independently decompressible (enables parallel decode)
- Max block size: 64 MB (configurable at compile time)

## Memory Usage

| Component | Memory |
|-----------|--------|
| BWT (SA-IS) | ~5× input size (suffix array + working space) |
| Multi-table rANS | ~40 KB per block (group frequencies) |
| LZRC binary tree | ~8 bytes × window_size + hash table |
| LZRC (16 MB window) | ~130 MB (tree + hash) |
| AAC model | ~258 KB (256 order-1 contexts × 257 Fenwick entries) |

## Thread Safety

- `mcx_compress` and `mcx_decompress` are **reentrant** — no global state
- Internal OpenMP parallelism at the block level
- Each block is compressed/decompressed independently

## Error Handling

Functions returning `size_t` use the top bit to signal errors. Always check with `mcx_is_error()`. Error codes provide specific failure information via `mcx_get_error_name()`.

## Versioning

MCX follows [Semantic Versioning 2.0](https://semver.org/):
- **MAJOR**: Breaking format changes (existing .mcx files incompatible)
- **MINOR**: New features, new compression strategies, backward-compatible format additions
- **PATCH**: Bug fixes, performance improvements, no format changes

Version is embedded in:
- `include/maxcomp/maxcomp.h` (`MCX_VERSION_MAJOR/MINOR/PATCH`)
- Frame header (format version byte)
- CLI output (`mcx --version`)
