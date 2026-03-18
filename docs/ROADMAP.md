# MCX Roadmap

## v2.1.0 — Released (2026-03-18)

### New Features
- **Embedded libdivsufsort** — BWT forward transform 2× faster
- **LZRC fast mode (L24)** — hash chain match finder, 3× faster than L26
- **`mcx_get_frame_info()` API** — read metadata without decompressing
- **`mcx test`** — built-in self-test command
- **`-q/--quiet`** — silent mode for scripts
- **rANS for LZ blocks** — 0.5-2% smaller than FSE at L1-L9
- **AAC at L7+** (was L9+) — better ratio at moderate speed
- **Fuzz + malformed input tests** — 10 test suites, all passing
- **Comparison benchmark script** (`benchmarks/compare.sh`)
- **GitHub Actions CI** — Linux/macOS/Windows, all green

### Performance
- L1 compress: +3.7% ratio (better hash populating)
- L6 compress: +2.9% ratio (larger HC hash + rANS)
- L6 decompress: +65% speed (hash scaling for cache)
- BWT inverse: +5% speed on large files (prefetch)
- rANS decoder: unrolled 2× per iteration

### Results (Silesia Corpus)
- Total: **4.35×** (L20 smart mode)
- Beats bzip2 on **12/12** files (100%)
- Beats xz on **7/12** files (58%)
- kennedy.xls: **50.1×** (2.4× better than xz)
- mozilla: **3.22×** (LZRC engine)

## v2.2 — Compression Quality (Next)

- [ ] Optimal parsing for LZRC (price-based match/literal decisions, +2-5%)
- [ ] Split-stream entropy coding for LZ (separate literal/token/offset streams)
  - Requires compact table format (current FSE overhead too high per stream)
- [ ] Sparse context tables for CM-rANS (fix 128KB header)
- [ ] Per-block strategy selection in large archives
- [ ] ARM/ARM64 BCJ filter

## v2.3 — Speed & Packaging

- [ ] PyPI package (pip install maxcomp)
- [ ] Doxygen API documentation
- [ ] Streaming decompression API (incremental)
- [ ] Interleaved rANS decoder (2-4× throughput)
- [ ] SIMD-accelerated MTF decode (AVX2/NEON)

## v3.0 — Future

- [ ] Zstandard-style sequence coding (Huffman literals + FSE sequences)
- [ ] WASM build for browser usage
- [ ] GPU-accelerated BWT (CUDA/OpenCL)
- [ ] Multithreaded LZRC compression
- [ ] Archive format (multiple files, directory structure)
- [ ] Context mixing (PAQ-inspired, for maximum ratio mode)

## Rejected Experiments (Research Notes)

These approaches were tested and found to be unhelpful:

| Experiment | Result | Why |
|-----------|--------|-----|
| 4-state LZRC machine | No gain | Model dilution on small files |
| Delta/XOR literal coding | +20-30% entropy | LZ residuals are random |
| Position-aligned literals | 0.1% saving | Not enough correlation |
| 15-bit rANS precision | +29 bytes | Table overhead > precision gain |
| 16 literal groups | Worse | Model dilution |
| Hybrid LZ+BWT | Always worse | BWT subsumes LZ matches |
| RC for BWT output | Undecodable | Self-contextual chicken-and-egg |
| Split-stream LZ+FSE | -8.4% worse | FSE table overhead per stream |
| Lazy threshold ≠ +1 | +0 and +2 worse | +1 is sweet spot |
| Match-dist literal ctx | Mixed | Sometimes helps, sometimes hurts |
| 8 dist context bits | No gain | 6 bits sufficient |
| Rep-aware lazy | Net negative | Helps one file, hurts others |
