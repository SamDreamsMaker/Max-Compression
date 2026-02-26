# Changelog

All notable changes to MaxCompression are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [1.1.0] - 2026-02-26

### New Features

- **Multi-stream LZ77+FSE engine** (`lib/lz/lz_multistream.c`):
  Separates LZ77 output into 4 independent FSE-coded streams (literals, literal
  lengths, match lengths, variable-length offsets). Each stream has its own
  frequency table → better entropy approximation → improved compression ratio.
  Accessible via `mcx_lzfse_compress` / `mcx_lzfse_decompress`.

- **Repcode stack** (inside multi-stream engine):
  Last 3 match offsets are cached (`rep[3] = {1, 4, 8}`). When the next match
  reuses a recent offset, a single-byte repcode (0/1/2) is emitted instead of a
  full offset. Effective on structured and text data.

- **SIMD SSE4.1 dual-hash match finding**:
  `mcx_simd_hash_dual()` computes two independent Knuth hashes in a single
  `_mm_mullo_epi32` pass. Both hash tables are probed and updated every step;
  the longer (or closer) match is selected. Cache-line prefetch (`MCX_PREFETCH`)
  issued 16 positions ahead hides L2 latency.

- **Real tANS entropy coder** (`lib/entropy/fse.c`):
  Replaced the former canonical Huffman stub with a complete table-based
  Asymmetric Numeral Systems (tANS / FSE) implementation:
  - `mcx_fse_normalize_freq`: normalises symbol counts to TABLE_SIZE = 1024
  - `mcx_fse_build_enc_table` / `mcx_fse_build_dec_table`: spread-based table
    construction matching the Yann Collet FSE reference layout
  - Encoder/decoder loops with bit-level arithmetic
  - **Magic byte 0xFD** distinguishes new tANS format from legacy Huffman

- **4-stream interleaved tANS**:
  Four independent ANS states (`state[4]`) encode/decode in round-robin. The
  resulting instruction-level parallelism gives a 2-4x decompression throughput
  improvement over single-state tANS. **Magic byte 0xFE**.

### Bug Fixes

- **Fitness function** (`lib/optimizer/genetic.c`): was computing `p * (1/p) = 1`
  instead of `p * log2(1/p)`. The genetic optimizer now correctly maximises
  Shannon entropy and produces meaningful genome selection.

- **HIGH_ENTROPY strategy ordering** (`lib/core.c`): the incompressibility
  early-exit was placed before the level selector, silently forcing STORE even at
  L1-9. Moved after the level check so LZ levels always attempt compression (LZ77
  can exploit long-range periodicity invisible to byte histograms).

- **LZ77-raw block type 0xAB** (`lib/core.c`): when FSE does not improve on the
  LZ77 output the compressor previously discarded the LZ77 result and stored raw
  original data. It now stores the LZ77 output directly (block type 0xAB), and
  the decompressor handles it accordingly.

- **`mcx_lz_fast_ctx` struct alignment** (`include/maxcomp/maxcomp.h`): fixed
  `dict` field declaration to match the dual-probe layout in `mcx_lz_fast.h`.

- **Debug file dumps removed** (`lib/model/context.c`): hardcoded
  `fopen("c:\\MaxCompression\\good_tables.bin", ...)` calls are gone.

- **Version string aligned**: `mcx_version_string()` now returns `"1.1.0"` to
  match `MCX_VERSION_MAJOR/MINOR/PATCH` and `CMakeLists.txt`.

- **`bench_fast.c` timer** (`tests/bench_fast.c`): replaced `clock()` (15 ms
  resolution on Windows) with `QueryPerformanceCounter` for accurate MB/s figures.

### New Tests

- `tests/test_lzfse.c`: 6 round-trip scenarios for the multi-stream engine
  (zeros, cycling pattern, repeated text, structured+random, small 32B, 512K text)
- `tests/test_bwt_levels.c`: validates L3/L9/L10/L15/L22 on text, binary, and
  zeros; confirms BWT ratios (L15 text: 3.52x) and correct STORE for
  incompressible data at BWT levels.
- `tests/bench_fast.c`: native speed harness for `mcx_lz_fast` with QPC timing.

### Block Type Reference (LZ path, `core.c`)

| Byte | Meaning |
|------|---------|
| `0x00` | STORE — neither LZ nor FSE compressed |
| `0xAA` | LZ77 output compressed with FSE |
| `0xAB` | LZ77 output stored raw (FSE couldn't improve) |

---

## [1.0.0] - 2026-02-24 (initial release)

- LZ77 greedy compressor (L1-3) and lazy HC compressor (L4-9)
- BWT + MTF + RLE + rANS pipeline (L10-14)
- CM-rANS context model (L15-22)
- SA-IS Burrows-Wheeler transform
- Genetic optimizer for pipeline genome selection
- Block-parallel compression via OpenMP
- CLI tool (`mcx compress` / `mcx decompress`)
- Python (ctypes) and Rust (FFI) bindings
- Calgary / Canterbury / Silesia benchmark suite
