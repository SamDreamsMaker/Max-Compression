# Changelog

All notable changes to MaxCompression are documented in this file.

## [2.1.2] — 2026-03-20

### Added
- **`--json` flag** for `mcx info` and `mcx stat` — machine-readable JSON output for scripting/CI.
- **`mcx checksum` command** — CRC32 of compressed file with header metadata for transfer verification.
- **`mcx version --build`** — extended build info (SIMD, optimization, C std, BWT engine).
- **CMake export targets** — `find_package(MaxCompression)` support for downstream projects.
- **Fuzz corpus seed generator** — `tests/generate_fuzz_corpus.sh` creates 31 diverse seed files.
- **GitHub Sponsors** — `.github/FUNDING.yml` funding configuration.
- **Rust bindings** — `version()`, `get_frame_info()`, `check_error()`, `MaxCompressionError` type.
- **Python bindings docs** — `verify()` and `diff()` API reference and usage examples.

### Changed
- **Table-based Huffman decoder** — 9-bit lookup table replaces bit-by-bit tree walk; O(1) for codes ≤9 bits.
- **BWT inverse 2× unrolled** — reduced loop overhead in LF-mapping reconstruction.
- **Better CLI error messages** — decompress errors now include filename and specific error name.

### Documentation
- **FORMAT.md** — updated with all 8 frame flags (adaptive blocks, int-delta, LZP, nibble-split).

## [2.1.1] — 2026-03-19

### Added
- **`mcx test` self-test command** — verifies all compression levels in one command.
- **`mcx verify` command** — integrity check with optional original file comparison.
- **`mcx diff` command** — compare two compressed archives (size, ratio, strategy).
- **`mcx ls` command** — compact multi-file listing (compressed size, ratio, level, strategy).
- **Multi-file compress/decompress** — `mcx compress file1 file2 ...` and `mcx decompress *.mcx`.
- **Decompress aliases** — `mcx extract`, `mcx x`, `mcx d` (gzip/tar-style convenience).
- **`--recursive/-r` flag** — compress/decompress all files in a directory tree.
- **`--threads/-t` flag** — control OpenMP thread count.
- **`-q/--quiet` flag** — suppress non-error output for scripting.
- **`-f/--force`, `-c/--stdout`, `--delete`, `-k/--keep` flags** — gzip-compatible CLI options.
- **`.mcx` double-compress warning** — warns when compressing already-compressed files.
- **Shell completions** — Bash, Zsh, and Fish completion scripts.
- **Multi-file roundtrip CI test** — validates multi-file compress/decompress in ctest.
- **Edge case tests** — boundary conditions, patterns, frame info validation.
- **BENCHMARKS.md** — comprehensive standalone benchmark document.
- **Demo script** (`benchmarks/demo.sh`) — MCX vs gzip/bzip2/xz comparison.
- **`.gitattributes`** — consistent line endings across platforms.
- **`.clang-format`** — code formatting config.
- **GitHub issue templates** (bug report + feature request).
- **SECURITY.md** — vulnerability disclosure policy.
- **.editorconfig** — consistent code style across editors.
- CI status badge and version badge in README.

### Changed
- **rANS entropy for LZ blocks** — 0.5-2% smaller than FSE at L1-L9.
  - alice29 L6: 65739→63814 (-2.9%), L9: 64442→62728 (-2.9%).
- **Separate rep-match length model** (LZMA-style) — improved LZRC compression.
- **AAC enabled at L7+** (was L9+) — better ratio at moderate speed.
- **Larger HC hash table** (1M entries) — L6 -1.4%, L9 -0.9%.
- **Better greedy hash populating** — L1 -3.7% ratio improvement.
- **Scaled LZ hash table** with input size — +65% decompress speed on small files.
- **rANS decoder unrolled** — 2 symbols per iteration.
- **BWT inverse prefetch** for blocks >256KB (+5% decompress on large files).
- **RC_UNLIKELY branch hints** for range coder normalize.
- Updated man page with all new commands, flags, and aliases.
- Updated ROADMAP.md with rejected experiments table.
- Updated API.md with `mcx_get_frame_info` documentation.
- Updated README with enwik8 results and new CLI commands.

## [2.1.0] — 2026-03-18

### Added
- **`mcx_get_frame_info()` public API** — read frame metadata without decompressing.
- **Fuzz roundtrip test** — 1000 random inputs across all levels, catches edge cases.
- **Malformed input test** — 7 decompressor robustness tests (corrupt data, truncated, wrong magic).
- **Comparison benchmark script** (`benchmarks/compare.sh`) — MCX vs gzip/bzip2/xz.
- Doxyfile for API documentation generation.
- pkg-config template (`maxcomp.pc.in`).

### Changed
- **Embedded libdivsufsort** — BWT forward transform **2× faster** (Yuta Mori, MIT license).
  - alice29: 0.08→0.035s, dickens: 8.76→4.43s, ooffice: 4.0→1.91s.
- **LZRC fast mode (L24)** — hash chain match finder, **~3× faster** than L26 BT.
- CLI `info` command now uses public `mcx_get_frame_info()` API.
- CLI help shows level descriptions and L24 example.
- README updated with v2.1.0 benchmarks and LZRC/divsufsort features.

### Fixed
- MSVC compatibility for divsufsort (disabled `strings.h`, use `__inline`).

### Rejected experiments
- 4-state LZRC machine (model dilution, no gain on any file).

## [2.0.1] — 2026-03-18

### Added
- `mcx bench <file>` command — benchmark all compression levels with speed/ratio comparison.
- Man page (`docs/mcx.1`).
- Hash chain match finder (`hc_match.c`) — 4× faster alternative for future speed modes.
- PyPI publish workflow (`.github/workflows/pypi.yml`) + `pyproject.toml`.
- L26 (LZRC) roundtrip test in CI.
- Python bindings: `version()` and `compress_bound()` functions.

### Changed
- **MTF decode +67% faster** — replaced byte loop with `memmove` (29→49 MB/s).
- **RC decoder inline** — decompress +19–36% (cross-TU inlining of hot path).
- **RC encoder inline** — compress +57–113%.
- **BT depth 64→32** — same ratio, ~7% faster compression.
- Portable bench timing (Windows `QueryPerformanceCounter`, POSIX `clock_gettime`).

### Fixed
- CI: Windows `__builtin_ctzll` → `mcx_ctzll()` portability.
- CI: OpenMP optional (`find_package(QUIET)`).
- CI: `_GNU_SOURCE` for `bench_fast.c`.
- CI: Missing `preprocess.h` include in `genetic.c`.

## [2.0.0] — 2026-03-18

### Added
- **LZRC v2.0 engine** — LZ + adaptive range coder with binary tree match finder (16 MB window).
  - Lazy evaluation: universal +1–5% improvement.
  - 4 rep distances (rep0–rep3): cheaper repeat match encoding.
  - LZMA-style matched literal coding: bit-level prediction from match reference.
  - Integrated as block type `0xB0` (L26) + multi-trial at L20.
  - Binary data auto-routed to LZRC at L20+ — mozilla: 2.93× → **3.22×** (+10%).
- Smart routing: text→BWT, binary→LZRC, stride→delta, x86→E8/E9.
- GitHub Actions CI: Linux (GCC + Clang), macOS, Windows.
- Automated release workflow with pre-built binaries.
- `CONTRIBUTING.md`, `docs/DESIGN.md`, Doxyfile for API documentation.
- Python bindings packaging (`setup.py`).
- SemVer tagging (`v1.9.3`, `v2.0.0`).

### Changed
- **Silesia total: 4.21× → 4.35×** (+3.3%) thanks to LZRC routing.
- MCX L20 now beats xz on **7/12 Silesia files** (was 6/12).
- Total competitive with xz -9 (~4.35× vs ~4.34×).

### Fixed
- Cross-platform portability: `mcx_ctzll()` for MSVC, optional OpenMP.
- Implicit function declarations fixed for Clang strict mode.
- `_GNU_SOURCE` added to `bench_fast.c` for `CLOCK_MONOTONIC`.

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
