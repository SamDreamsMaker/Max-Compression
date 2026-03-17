# MaxCompression Roadmap

## Current Status (v1.9.0)

MCX beats bzip2 on 10/12 Silesia files and xz on 9/12. Dominates on text and structured data. Competitive on executables (E8/E9 filter). Weak on binary archives (mozilla -23% vs xz).

## What the Pros Do (and How We Compare)

### ✅ We Do This Well
| Technique | MCX | bzip2 | xz/LZMA2 | zstd |
|-----------|-----|-------|-----------|------|
| BWT + MTF | ✅ (16MB blocks) | ✅ (900KB blocks) | ❌ | ❌ |
| Multi-table entropy | ✅ (4-6 table rANS, K-means) | ✅ (6 table Huffman) | ❌ | ❌ |
| Stride detection | ✅ **Unique to MCX** | ❌ | ❌ | ❌ |
| E8/E9 x86 filter | ✅ (auto-detect) | ❌ | ✅ (BCJ filter) | ❌ |
| RLE2 (RUNA/RUNB) | ✅ | ✅ | ❌ | ❌ |
| Multi-trial | ✅ (BWT + LZ-HC + E8/E9) | ❌ | ❌ | ❌ |

### ❌ We Don't Have Yet (Ordered by Impact)
| Technique | Used by | Expected gain | Difficulty |
|-----------|---------|---------------|------------|
| **Context mixer** | LZMA2, zpaq, paq | +10-20% on binary | Very High |
| **Large-window LZ** | LZMA2 (64MB), zstd (128MB) | +15-25% on archives | High |
| **Match finder (BT4)** | LZMA2 (binary tree + 4-byte hash) | +5-10% vs hash table | Medium |
| **Range coder** | LZMA2, zpaq | +1-2% vs rANS | Low |
| **ARM/ARM64 BCJ filter** | xz, 7-Zip | +5-10% on ARM binaries | Low |
| **Delta filter** | xz (preprocessor) | +5% on PCM audio | Low |
| **Dictionary preseeding** | zstd (trained dicts) | +50% on small similar files | Medium |
| **Parallel compression** | pigz, pzstd | Nx speedup (no ratio gain) | Medium |
| **SIMD optimization** | zstd, lz4 | 2-4x speed improvement | Medium |

## Short-term Goals (v2.0)
1. [ ] Improve LZ-HC with hash chains (better match finding)
2. [ ] ARM/ARM64 BCJ filter (broadens platform support)
3. [ ] Per-block strategy selection for archives
4. [ ] SIMD-optimized BWT inverse (faster decompression)
5. [ ] Streaming API improvements (match core API quality)

## Medium-term Goals (v3.0)
1. [ ] Context mixer for binary data
2. [ ] Large-window LZ (64MB+) with binary tree match finder
3. [ ] Parallel block compression (OpenMP)
4. [ ] Dictionary training mode
5. [ ] Professional benchmarking suite with automated regression tests

## Long-term Vision
MCX should be the **best general-purpose compressor** — not just for text. This requires closing the gap with LZMA2 on binary data while maintaining our lead on text and structured data.

## Architecture Decision Record
- **BWT-first architecture**: Chosen because BWT excels on text/structured data (70%+ of real-world data). Binary is handled via multi-trial fallback to LZ-HC.
- **rANS over Huffman**: Better precision (14-bit), proven superior on BWT output (-322 bytes vs Huffman on alice29).
- **Multi-trial over single-path**: Guarantees L20 ≥ max(L9, L12) on every file. Extra compression time is acceptable for maximum ratio.
- **Auto-detection over manual flags**: Stride detection, E8/E9 detection, data type analysis — user just picks a level.
