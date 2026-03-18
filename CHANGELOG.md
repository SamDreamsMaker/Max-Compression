# Changelog

All notable changes to MaxCompression are documented in this file.

## [1.9.3] — 2026-03-18

### Added
- **LZRC v2.0 prototype** — LZ + range coder with binary tree match finder (16 MB window). mozilla: 3.07× (best LZ result, +5% vs BWT L20).
- Adaptive arithmetic coding (AAC) on LZ output at L9+ — order-1 model with Fenwick-tree accelerated decoding.
- 64 MB block size — up from 32 MB. webster +2.1%, mozilla +0.3%.

### Fixed
- **L12 genome optimizer** — was skipping BWT on binary files. Silesia L12 total: 2.91× → 4.16× (+43%). nci: 3.29× → 25.65× (+680%).
- CLI version string now derived from header constants (was hardcoded).
- Fenwick tree decoder — merged `find` + `query` into single tree walk. Decompress +2–5%.

### Changed
- AAC only active at L9+ (was L6+). L6 keeps FSE for faster decompression (19 MB/s vs 3 MB/s).
- Multi-rANS speed: precomputed log2 LUT + sparse symbol lists + uint16 freqs. 41.5s → 12.1s on mr (-71%).

## [1.9.2] — 2026-03-18

### Added
- **Adaptive arithmetic coding** (AAC) — order-1 AC for LZ output. kennedy L9: 4.87× → 9.04× (+86%).
- E8/E9 x86 filter — auto-detects executables (≥0.5% E8/E9 opcodes). ooffice: 2.18× → 2.53× (+16%).

## [1.9.1] — 2026-03-17

### Added
- LZ-HC hash chains (depth 8/16). alice29 L9: 2.14× → 2.31× (+8%).
- CLI uses one-shot API for files ≤256 MB. alice29 CLI: 1.91× → 3.53×.

### Changed
- Block size: 32 MB (up from 16 MB).

## [1.9.0] — 2026-03-17

### Added
- **E8/E9 x86 filter** with auto-detection and multi-trial.
- 32 MB blocks.

## [1.8.1] — 2026-03-17

### Added
- Multi-rANS speed optimizations: precomputed LUT, sparse active lists, uint16 group freqs. Total: 41.5s → 12.1s (-71%).

## [1.8.0] — 2026-03-17

### Added
- Sequential K-means initialization for multi-table rANS. alice29: 43268 → 43144 bytes. **Beats bzip2** (43207).
- 15 K-means iterations (was 10).

## [1.7.8] — 2026-03-17

### Added
- **Varint frequency tables** for multi-rANS. Saves ~300 bytes per block. kennedy: 46.92× → 48.73×.
- Adaptive table count (4, 5, or 6 tables, keeps smallest).

## [1.7.7] — 2026-03-17

### Added
- Bitmap table format for multi-rANS (32-byte bitmap + varint freqs per active symbol).

## [1.7.5] — 2026-03-17

### Changed
- Block size: 16 MB (up from 8 MB). Beats bzip2 on 11/12 Silesia files.

## [1.7.1] — 2026-03-17

### Fixed
- Route ALL data types to BWT first (binary was incorrectly routed to LZ24). xml: 6.80× → 12.45× (+83%).

## [1.7.0] — 2026-03-17

### Fixed
- Text routing: removed 4 MB → LZ24 rule. dickens: 2.21× → 3.69× (+67%).

## [1.6.0] — 2026-03-17

### Added
- **Multi-table rANS** — 4 frequency tables with K-means clustering, group size 50. Near bzip2 on text.

## [1.5.1] — 2026-03-17

### Added
- RLE2 on stride-delta output. ptt5: 8.83× → 10.19× (+15%).

## [1.5.0] — 2026-03-16

### Added
- **Delta-fix** — forced `delta=0` for text. +13–35% on ALL text files.
- **Auto-RLE2** for all BWT levels (L10+). L12 = L20 on text.
- BWT threshold lowered 8 KB → 1 KB.
- rANS precision bumped to 14-bit.

## [1.4.0] — 2026-03-16

### Added
- **Stride-Delta + BWT + RLE2 pipeline**. kennedy.xls: 46.91× (2.2× better than xz).

## [1.3.0] — 2026-03-16

### Added
- **RLE2 (RUNA/RUNB)** — exponential zero-run encoding. +5–7% on text.
- Smart Mode (L20+) with auto data-type routing.

## [1.2.0] — 2026-03-16

### Added
- LZ24 (16 MB window, hash chains, lazy evaluation).
- Stride-delta transform with auto-detection.

## [1.1.0] — 2026-03-14

### Added
- 4-stream interleaved tANS (+64% decompress speed).
- CM-rANS (order-1 context-mixing entropy coder).

## [1.0.0] — 2026-03-12

### Added
- Initial release.
- LZ77 (greedy + lazy), BWT (SA-IS), tANS/FSE, rANS.
- Genetic pipeline optimizer.
- Multi-stream LZ77 with repcode stack.
- SIMD SSE4.1 hash computation.
