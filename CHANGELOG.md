# Changelog

All notable changes to MaxCompression are documented in this file.

## [2.2.0] — 2026-03-20

### Added (Batch 18)
- **`mcx bench --percentile`** — report p5/p50/p95 speed distribution when iterations ≥ 5. Provides a full picture of benchmark variance alongside mean/median.
- **`mcx compress --exclude GLOB`** — skip files matching a glob pattern when using `-r` recursive mode (e.g. `--exclude '*.mcx'` or `--exclude '*.log'`).
- **Integration tests** — coverage for `--median` and `--percentile` bench flags.
- **L12 vs L14 analysis** — tested on dickens: L12/L13/L14 produce byte-identical output (2497882 bytes). All use MCX_STRATEGY_DEFAULT with same 64MB block size and entropy coding. No differentiation exists between L12-L14.
- **Man page + completions update** — `--percentile` and `--exclude` documented across man page, Bash, Zsh, and Fish completions.

### Added (Batch 17)
- **Adaptive hash table size for L1** — scale hash entries with input size (small files get smaller table, better cache usage). Tightened greedy multiplier from 4× to 2×, same ratio (2.15x alice29).
- **`mcx bench --median`** — report median instead of mean when iterations > 1 (more robust to outliers).
- **`mcx compress --keep-broken`** — keep partial output on error instead of deleting (useful for debugging). Also added `cleanup_partial()` to remove partial output by default on compression errors.
- **L6 vs L9 Silesia profiling** — L9 avg +6.3% ratio at -47% compress speed, -85% decompress speed. Best for archival; L6 best for speed/ratio balance.
- **Integration tests** — coverage for `--top` flag (bench --all-levels --sort ratio --top 3, verify exactly 3 lines).
- **Man page + completions update** — `--median` and `--keep-broken` documented across man page, Bash, Zsh, and Fish completions.

### Added (Batch 16)
- **Multi-rANS trial early exit** — at L12, skip context-variant trials when lo_tables already beats hi_tables by >1%. Reduces entropy encode time on blocks where simpler tables win.
- **`mcx bench --top N`** — only show top N results by ratio (useful with `--all-levels`).
- **`mcx compress --atomic`** — write compressed output to a temp file, rename on success (crash-safe writes).
- **L1 vs L3 Silesia profiling** — L3 wins all 12 files with avg +5.2% ratio at -13% speed. Lazy matching is the correct default for L2-L3.
- **Integration tests** — coverage for `--min-ratio` (high threshold skips output) and `--atomic` (roundtrip verified).
- **Man page + completions update** — `--top` and `--atomic` documented across man page, Bash, Zsh, and Fish completions.

### Added (Batch 15)
- **Nice-match early exit for HC chains** — L4-L9 now stop chain walking when a sufficiently long match is found (L4=16, L5-6=32, L7-8=64, L9=128). No ratio impact, faster compression on repetitive data.
- **`mcx bench --sort ratio|speed|level`** — sort benchmark output by compression ratio, speed, or level (default: level).
- **`mcx compress --min-ratio`** — only write compressed output if the achieved ratio meets or exceeds the given threshold (e.g. `--min-ratio 1.5`); skips writing if compression doesn't help enough.
- **Integration tests** — coverage for `--json bench`, `--csv bench`, `--decode-only bench`, and `--sort` bench flags.
- **L12 enwik8 profiling** — 10MB: BWT 35% (2718ms), MTF+RLE 5% (374ms), entropy encode 60% (4709ms). Multi-trial rANS is the clear bottleneck on 10MB blocks.
- **Man page + completions update** — `--sort` and `--min-ratio` documented across man page, Bash, Zsh, and Fish completions.

### Added (Batch 14)
- **`mcx bench --all-levels`** — benchmark every compression level 1-26.
- **`mcx bench --ratio-only`** — measure compression ratio only, skip timing (faster for ratio comparisons).
- **Integration tests** — coverage for `--all-levels` (verifies 26 levels) and `--ratio-only` (no speed columns).
- **L6 compress profiling** — 8.3 MB/s compress, 109 MB/s decompress on alice29. Bottleneck is HC chain walking in match finder, not entropy encode. Hash table already reduced to 256K entries.
- **Man page + completions update** — `--all-levels` documented across man page, Bash, Zsh, and Fish completions.

### Skipped (Batch 14)
- **16-bit CRC in LZ token stream** — requires format change (all 8 flag bits used), deferred to v3.0.
- **Compress `--comment` flag** — frame header has no reserved bytes for variable-length data; needs v3.0 format extension.

### Added (Batch 13)
- **`mcx bench --all-levels`** — benchmark every level 1-26 (not just the default subset of 8 representative levels).
- **`--preserve-mtime` decompress fix** — flag was only parsed in compress command; now also works for decompress/extract.
- **`--preserve-mtime` integration test** — verifies mtime preservation through full compress→decompress roundtrip.
- **Man page + completions update** — `--preserve-mtime`, `--memory`, and `--all-levels` documented across man page, Bash, Zsh, and Fish completions.

### Skipped (Batch 13)
- **3-byte LZ matches** — match confirmation uses `read32()` (4-byte compare) for speed in hot loop; 3-byte matches need masking/memcmp plus token format change. Deferred to v3.0.
- **Embed CRC32 in frame header** — all 8 flag bits used; needs flag extension (v3.0 format change).
- **Cache-friendly BWT inverse** — already optimized with merged LF-mapping, 2× unrolled with prefetch. LF-traversal is inherently random-access; no further optimization without algorithmic change.

### Added (Batch 12)
- **`mcx compress --preserve-mtime`** — set output file mtime to match input file's modification time.
- **`mcx bench --memory`** — report peak RSS memory usage alongside speed and ratio during benchmarks.
- **`--split` integration test** — compress with split, concatenate chunks, decompress, verify roundtrip correctness.
- **L1 decompress profiling** — 147 MB/s on enwik8, memory-bandwidth limited; wild-copy16 + match copy already optimal.
- **Man page + completions update** — all Batch 12 flags documented.

### Tested (Batch 12)
- **Linked-list MTF** — tested replacing array+memmove with linked list for large positions (pos>16). Only -4% difference on 1MB BWT-like data, within noise. 256-byte table fits in L1 cache, memmove is already optimal. Not adopted.

### Added (Batch 11)
- **CM-rANS early exit** — skip CM-rANS trial when single rANS is already smaller than multi-table (reduces entropy encode time at L12+).
- **`mcx bench --size`** — benchmark on a truncated prefix of input (e.g. `--size 64K` for quick tests).
- **`mcx compress --split N`** — split compressed output into N-byte chunks (e.g. `--split 10M` for large archives).
- **Unrolled MTF decode** — switch-based fast path for positions 0-3 (~85%+ of BWT+MTF output) with memmove fallback for pos≥4.
- **Man page + completions update** — all Batch 11 flags documented.

### Skipped (Batch 11)
- **Radix sort for BWT** — BWT already uses SA-IS (O(n) linear time) or libdivsufsort; radix sort wouldn't improve.

### Added (Batch 10)
- **`mcx bench --iterations N`** — run N compress/decompress iterations and average (default: 1, or 3 for --decode-only).
- **Lazy matching for L2-L3** — check next position's match before emitting current; L2/L3 2.26x vs L1 2.15x on alice29 (+5% ratio, -6% speed tradeoff).
- **`mcx compress --filter`** — force a specific preprocessing filter (delta, nibble, none) instead of auto-detect.
- **Integration tests** — coverage for --level-scan, --no-trials, --decode-only, --json bench, --iterations, --filter flags.
- **L12 compress profiling** — entropy encode is bottleneck (78%, multi-trial rANS); decompress bottleneck is BWT inverse (56%).
- **Man page + completions update** — all Batch 10 flags documented.

### Added (Batch 9)
- **`mcx bench --json`** — machine-readable JSON benchmark output for CI/scripting.
- **`mcx bench --decode-only`** — pre-compress once, then time decode only (3 iterations averaged).
- **2-byte context rANS** — `ctx2_hash(prev2, prev1) = (prev2*31)^prev1` trialed alongside 1-byte context; auto-pick best per block.
- **`--level-scan` includes L20** — scan now covers L1-L12 and L20 for maximum ratio discovery.
- **L1 accelerating skip** — LZ4-style skip on misses + lazy secondary hash update for faster fast-mode compression (ratio unchanged on real data).
- **Man page + completions update** — all Batch 9 flags documented.

### Added (Batch 8)
- **`mcx bench --warmup`** — run one warmup iteration before timing to reduce cold-cache variance.
- **`mcx compress --no-trials`** — skip multi-strategy trial at L20+ (use first strategy, faster).
- **8-bit context rANS** — context-mode table selection (prev byte → 256 contexts) trialed alongside group mode; decoder detects via header flag.
- **`mcx info --blocks`** — show per-block details (offset, size, strategy, flags) in tabular format.
- **Silesia integration test** — roundtrip 12 corpus files × 3 levels (L1/L6/L12) = 36 tests.
- **`mcx compress --level-scan`** — try L1-L12 and pick the best ratio automatically.

### Tested (Batch 8)
- **10-bit Huffman table** — no measurable decode speed difference vs current 9-bit (93.1 vs 92.9 MB/s on dickens L6); Huffman decode is not the bottleneck.

### Added (Batch 7)
- **Version bump to 2.2.0** — synced across CMakeLists.txt, maxcomp.h, and README badge.
- **`mcx upgrade --in-place/-i`** — explicit shortcut to overwrite file in-place during re-compression.
- **Pipe mode CI test** — roundtrip via `mcx pipe | mcx pipe -d` in ctest.
- **`mcx bench --csv`** — CSV output for automated benchmark collection and CI tracking.
- **`mcx decompress --verify`** — re-compress after decompression to verify integrity match.
- **Man page update** — added upgrade, pipe, --estimate, --csv, --verify, --strategy, --json documentation.
- **Shell completions update** — Bash, Zsh, and Fish updated with all new commands and flags.

### Added
- **`mcx upgrade` command** — re-compress an `.mcx` file at a different level (decompress+recompress in one step) with roundtrip verification. Alias: `mcx recompress`.
- **`mcx pipe` command** — compress/decompress stdin→stdout (`mcx pipe [-l LEVEL]` / `mcx pipe -d`), like gzip without arguments.
- **`--estimate` flag** — estimate compressed size by compressing a 128KB sample, faster than `--dry-run`. Reports predicted ratio, savings, and estimated compression time.
- **`--threads/-T` flag** — explicit OpenMP thread control for CLI commands.
- **`mcx ls --json`** — machine-readable JSON archive listing.
- **`mcx list` alias** — consistency with common CLI tools (`mcx list` = `mcx ls`).
- **`mcx compare` alias** — `mcx compare` = `mcx bench` for convenience.
- **`--delete` flag** — remove source file after successful compress/decompress.
- **`--verbose/-v` flag** — show peak memory usage during compress/decompress.
- **WASM build target** — Emscripten build for browser/Node.js usage.
- **Valgrind CI job** — memcheck on full test suite (Linux).
- **C example program** (`examples/simple.c`) — compress/decompress API usage.
- **`mcx bench --level`** — benchmark specific levels instead of all.
- **Memory usage reporting** — peak RSS tracking with `--verbose`.

### Changed
- **L6 compress speed +8%** — reduced hash table from 1M to 256K entries for L4-L6, improving cache locality (5.1→5.5 MB/s on alice29.txt). L7+ retains 1M table for max ratio.
- **L6 decompress speed +13%** — 85.6→96.8 MB/s on alice29.txt from reduced memory footprint.

### Performance
- **L20 vs L26 benchmark** — L20 matches or beats L26 on all Silesia files; LZRC (L24/L26) only worthwhile for specific binary patterns.
- **Bash/Zsh/Fish completions** — updated with all new commands and flags.

### Documentation
- **`mcx diff` hex dump** — byte-level visual differences when archives differ.
- Updated man page, README, and completions for all new commands.

## [2.1.2] — 2026-03-20

### Added
- **`--strategy/-s` flag** — force compression strategy: `lz`, `bwt`, `cm`, `smart`, `lzrc`.
- **`--block-size` flag** — override BWT block size at runtime (64K-256M, e.g. `--block-size 4M`).
- **`--dry-run/-n` flag** — analyze file and predict strategy/entropy without compressing.
- **`mcx bench --compare`** — run gzip/bzip2/xz alongside MCX with comparison table.
- **`--json` flag** for `mcx info` and `mcx stat` — machine-readable JSON output for scripting/CI.
- **`mcx checksum` command** — CRC32 of compressed file with header metadata for transfer verification.
- **`mcx version --build`** — extended build info (SIMD, optimization, C std, BWT engine).
- **CMake export targets** — `find_package(MaxCompression)` support for downstream projects.
- **Fuzz corpus seed generator** — `tests/generate_fuzz_corpus.sh` creates 31 diverse seed files.
- **GitHub Sponsors** — `.github/FUNDING.yml` funding configuration.
- **Rust bindings** — `version()`, `get_frame_info()`, `check_error()`, `MaxCompressionError` type.
- **Python bindings docs** — `verify()` and `diff()` API reference and usage examples.

### Changed
- **Version synced to 2.1.2** across CMakeLists.txt, maxcomp.h, and README badge.
- **Table-based Huffman decoder** — 9-bit lookup table replaces bit-by-bit tree walk; O(1) for codes ≤9 bits.
- **BWT inverse 2× unrolled** — reduced loop overhead in LF-mapping reconstruction.
- **Better CLI error messages** — decompress errors now include filename and specific error name.

### CI/CD
- **pkg-config integration test** — builds minimal C program via `pkg-config --cflags --libs maxcomp`.
- **CMake find_package test** — validates `find_package(MaxCompression)` downstream usage.
- **Doxygen API docs job** — generates docs and checks for undocumented public symbols.

### Documentation
- **CONTRIBUTING.md** — added benchmark guide and test pattern instructions.
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
