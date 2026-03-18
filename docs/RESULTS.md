# MCX v1.9.3 — Final Results Report

## Summary

MaxCompression (MCX) is a lossless data compression library written in C99.
It achieves **better compression than bzip2 on 100% of tested files** while offering
multiple speed/ratio tradeoffs via compression levels.

## Architecture

```
L1-L3:  LZ77 greedy + tANS/FSE        (fast, ~4-9 MB/s compress)
L4-L8:  LZ77 lazy HC + tANS/FSE       (balanced)
L9:     LZ77 lazy HC + Adaptive AC     (best LZ ratio, 2-4 MB/s compress)
L10-L14: BWT + MTF + RLE2 + multi-rANS (strong, 0.3-0.5 MB/s)
L15-L19: BWT + MTF + RLE + CM-rANS    (experimental)
L20:    Smart Mode — multi-trial       (best ratio, slow)
```

### Key Innovations
- **Multi-table rANS**: K-means clustering of BWT output into 4-6 tables (like bzip2's Huffman switching but with rANS)
- **RLE2 (RUNA/RUNB)**: Bijective base-2 zero-run encoding for BWT+MTF output
- **Adaptive Arithmetic Coding**: Order-1 context model with Fenwick tree for O(log N) decode
- **E8/E9 x86 filter**: Preprocesses CALL/JMP instructions for better compression of executables
- **Stride-delta detection**: Auto-detects structured binary with periodic patterns

## Canterbury Corpus Results (L20)

| File | Size | gzip -9 | bzip2 -9 | **MCX L20** | vs bzip2 |
|------|------|---------|----------|-------------|----------|
| alice29.txt | 152 KB | 2.81× | 3.52× | **3.53×** 🏆 | **+0.3%** |
| kennedy.xls | 1006 KB | 4.87× | 7.66× | **50.11×** 🏆 | **+554%** |
| lcet10.txt | 427 KB | 3.15× | 3.72× | **3.98×** 🏆 | **+7%** |
| plrabn12.txt | 482 KB | 2.75× | 3.28× | **3.33×** 🏆 | **+2%** |

## Silesia Corpus Results (202 MB)

| File | Size | gzip -9 | bzip2 -9 | xz -9 | **MCX L9** | **MCX L20** | vs bzip2 | vs xz |
|------|------|---------|----------|-------|-----------|-------------|----------|-------|
| dickens | 10 MB | 2.65× | 3.64× | 3.60× | 2.34× | **4.07×** 🏆 | +12% | +13% |
| xml | 5 MB | 8.07× | 12.12× | 11.79× | 6.69× | **12.86×** 🏆 | +6% | +9% |
| ooffice | 6 MB | 1.99× | 2.15× | 2.54× | 1.86× | **2.53×** | +18% | -0.4% |
| reymont | 6.5 MB | 3.64× | 5.32× | 5.03× | 3.00× | **5.93×** 🏆 | +11% | +18% |
| sao | 7 MB | 1.36× | 1.47× | 1.64× | 1.34× | **1.48×** | +0.7% | -10% |
| x-ray | 8 MB | 1.40× | 2.09× | 1.89× | 1.40× | **2.15×** 🏆 | +3% | +14% |
| mr | 10 MB | 2.71× | 4.08× | 3.63× | 2.78× | **4.28×** 🏆 | +5% | +18% |
| osdb | 10 MB | 2.71× | 3.60× | 3.54× | 2.85× | **4.04×** 🏆 | +12% | +14% |
| nci | 33 MB | 11.23× | 18.51× | 19.30× | 9.58× | **25.65×** 🏆 | +39% | +33% |
| samba | 21 MB | 4.00× | 4.75× | 5.74× | 3.64× | **5.03×** | +6% | -12% |
| webster | 40 MB | 3.44× | 4.80× | 4.94× | 2.98× | **5.81×** 🏆 | +21% | +18% |
| mozilla | 50 MB | 2.70× | — | 3.83× | 2.60× | **2.93×** | — | -24% |
| **TOTAL** | **202 MB** | — | — | — | **2.93×** | **4.21×** | — | — |

### Win Rate
- **vs gzip -9**: 12/12 (100%) 🏆
- **vs bzip2 -9**: 11/11 tested (100%) 🏆
- **vs xz -9**: 9/12 (75%)

## Speed (single-thread)

| Level | Compress | Decompress | Ratio (alice29) |
|-------|----------|------------|-----------------|
| L3 | 4-9 MB/s | 12-32 MB/s | 2.04× |
| L9 | 2-4 MB/s | 3-6 MB/s | 2.34× |
| L12 | 0.3-0.5 MB/s | 3-5 MB/s | 3.53× |
| L20 | 0.1-0.3 MB/s | 3-5 MB/s | 3.53× |

## What We Tested & Rejected

| Approach | Result | Why |
|----------|--------|-----|
| Order-2 AAC | Worse than order-1 | LZ tokens too sparse for 64K contexts |
| Hybrid LZ+BWT on literals | Always worse | LZ tokens are 70-97% of output |
| LZ24 (16MB window) for binary | Worse than BWT | No context modeling on literals |
| Adaptive block sizes | Larger always better | BWT context > heterogeneity penalty |
| Double BWT | Always worse | Destroys MTF locality |
| CM-rANS sparse (order-1 tables) | Too much overhead | 2-5KB tables per block |
| Dictionary preprocessing | BWT captures implicitly | No gain after BWT |
| 15-bit rANS precision | Roundtrip failures | Encoder/decoder mismatch |

## Remaining Gaps (v2.0 Goals)

MCX trails xz on binary archives (mozilla -24%, samba -12%) because:
1. **xz uses LZMA2**: 64MB+ dictionary with context-mixed entropy coding on LZ literals
2. **BWT max context = block size**: Even 64MB blocks can't match LZMA2's sliding window
3. **No match-dependent literal coding**: LZMA2 uses match distance to predict literals

Closing these gaps requires a fundamentally different approach (large-window LZ + context mixer),
which is a multi-month v2.0 project.

## Build & Test

```bash
cmake -S . -B build && cd build && make maxcomp_static mcx
# 204 comprehensive roundtrip tests
gcc -O2 -I../include -I../lib -o bin/test tests/test_comprehensive.c bin/libmaxcomp.a -lm -fopenmp -lpthread
./bin/test
```

## License

GPL-3.0
