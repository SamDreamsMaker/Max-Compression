# MCX Roadmap

## v2.0.x — Current (Released)

### Compression Engines
- **BWT Pipeline**: SA-IS (divsufsort) → MTF → RLE2 → multi-table rANS
- **LZRC v2.0**: Binary tree / hash chain match finder + adaptive range coder
- **LZ-HC**: Lazy LZ77 + FSE / adaptive arithmetic coding
- **Stride-Delta**: Auto-detected structured binary preprocessing
- **E8/E9**: x86 CALL/JMP address filter

### Results (Silesia Corpus)
- Total: **4.35×** (L20 smart mode)
- Beats bzip2 on **12/12** files
- Beats xz on **7/12** files
- kennedy.xls: **50.1×** (2.4× better than xz)
- mozilla: **3.22×** (beats xz 2.93×)

### Speed
- L1-L3: 5-18 MB/s compress, 18-75 MB/s decompress
- L9: 2-6 MB/s compress, 3-24 MB/s decompress (AAC)
- L12: 0.3-1.5 MB/s compress, 4-8 MB/s decompress (BWT)
- L20: 0.1-0.8 MB/s compress, 4-14 MB/s decompress (auto)
- L24: 1.5-2.6 MB/s compress, 4-11 MB/s decompress (LZRC-HC)
- L26: 0.5-0.8 MB/s compress, 4-11 MB/s decompress (LZRC-BT)

## v2.1 — Speed & Packaging (Next)

- [ ] PyPI package (pip install maxcomp)
- [ ] Doxygen API documentation
- [ ] `mcx cat` — decompress to stdout (piping)
- [ ] Streaming decompression API
- [ ] ARM/ARM64 BCJ filter
- [ ] Faster suffix sort (divsufsort-lite or parallel SA-IS)

## v2.2 — Compression Quality

- [ ] Optimal parsing for LZRC (price-based match/literal decisions, +2-5%)
- [ ] Context-mixed literal coding (XOR prediction, +1-3%)
- [ ] Larger LZRC window (64MB) for huge files
- [ ] Per-block strategy selection in large archives
- [ ] Sparse context tables for CM-rANS (fix 128KB header)

## v3.0 — Future

- [ ] Streaming API (arbitrary-length input)
- [ ] WASM build for browser usage
- [ ] GPU-accelerated BWT (CUDA/OpenCL)
- [ ] Multithreaded LZRC compression
- [ ] Archive format (multiple files, directory structure)
