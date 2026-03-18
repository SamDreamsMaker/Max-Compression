# MCX Benchmark Results

**Version:** 1.9.3  
**Date:** 2026-03-18  
**Test environment:** Intel Xeon E-2386G @ 3.50 GHz, Linux, single-threaded

## Canterbury Corpus

### Level 20 (Smart Mode)

| File | Original | Compressed | Ratio | vs gzip | vs bzip2 | vs xz |
|------|----------|------------|-------|---------|----------|-------|
| alice29.txt | 152,089 | 43,144 | **3.53×** | +26% | **+0.3%** | +12% |
| asyoulik.txt | 125,179 | 39,799 | **3.15×** | +23% | ≈ | +12% |
| cp.html | 24,603 | 7,955 | **3.09×** | ≈ | -4% | -4% |
| fields.c | 11,150 | 3,293 | **3.39×** | -5% | -8% | -8% |
| grammar.lsp | 3,721 | 1,512 | **2.46×** | -18% | -15% | -15% |
| kennedy.xls | 1,029,744 | 20,551 | **50.12×** | +921% | +535% | +139% |
| lcet10.txt | 426,754 | 107,101 | **3.98×** | +35% | **+0.5%** | +12% |
| plrabn12.txt | 481,861 | 144,548 | **3.33×** | +34% | **+0.6%** | +14% |
| ptt5 | 513,216 | 50,370 | **10.19×** | +4% | -1% | -17% |
| sum | 38,240 | 13,466 | **2.84×** | -5% | -4% | -30% |
| xargs.1 | 4,227 | 1,977 | **2.14×** | -11% | -11% | -8% |

### Level 9 (Best LZ)

| File | Compressed | Ratio |
|------|------------|-------|
| alice29.txt | 65,018 | 2.34× |
| kennedy.xls | 113,918 | 9.04× |
| ptt5 | 59,323 | 8.65× |

## Silesia Corpus

### Level 20 (Smart Mode) — Definitive Results

| File | Original | Compressed | Ratio | vs gzip -9 | vs bzip2 -9 | vs xz -9 |
|------|----------|------------|-------|------------|-------------|----------|
| dickens | 10,192,446 | 2,503,136 | **4.07×** | +54% | +12% | +13% |
| xml | 5,345,280 | 415,596 | **12.86×** | +59% | +6% | +9% |
| ooffice | 6,152,192 | 2,427,050 | **2.53×** | +27% | +18% | -0.4% |
| reymont | 6,627,202 | 1,117,908 | **5.93×** | +63% | +11% | +18% |
| sao | 7,251,944 | 4,899,239 | **1.48×** | +9% | +1% | -10% |
| x-ray | 8,474,240 | 3,935,673 | **2.15×** | +54% | +3% | +14% |
| mr | 9,970,564 | 2,329,678 | **4.28×** | +58% | +5% | +18% |
| osdb | 10,085,684 | 2,497,824 | **4.04×** | +49% | +12% | +14% |
| nci | 33,553,445 | 1,308,201 | **25.65×** | +128% | +39% | +33% |
| samba | 21,606,400 | 4,293,052 | **5.03×** | +26% | +6% | -12% |
| webster | 41,458,703 | 7,138,948 | **5.81×** | +69% | +21% | +18% |
| mozilla | 51,220,480 | 17,466,729 | **2.93×** | +9% | — | -24% |
| **Total** | **211,938,580** | **50,333,034** | **4.21×** | | | |

### Summary

| Comparison | Files Won | Win Rate |
|------------|-----------|----------|
| MCX L20 vs gzip -9 | 12/12 | **100%** |
| MCX L20 vs bzip2 -9 | 11/11 | **100%** |
| MCX L20 vs xz -9 | 9/12 | **75%** |

### Notable Results

- **nci: 25.65×** — 33% better than xz, 39% better than bzip2
- **kennedy.xls: 50.12×** — 2.4× better than xz (stride-delta auto-detection)
- **ooffice: 2.53×** — E8/E9 x86 filter brings within 0.4% of xz
- **mozilla: 2.93×** — largest gap vs xz (-24%), addressed in v2.0 LZRC prototype (3.07×)

## Speed Benchmarks

### Compression Speed (MB/s)

| File | L3 | L6 | L9 | L12 | L20 |
|------|----|----|----|----|-----|
| alice29 | 5.1 | 3.8 | 2.0 | 0.3 | 0.3 |
| dickens | — | — | 2.1 | 0.3 | 0.3 |
| ooffice | — | — | 2.2 | 0.7 | 0.7 |
| mozilla | — | — | 2.7 | 0.3 | 0.3 |

### Decompression Speed (MB/s)

| File | L3 | L9 | L12 | L20 |
|------|----|----|-----|-----|
| alice29 | 18.7 | 23.5 | 5.5 | 5.5 |
| dickens | — | 3.8 | 3.9 | 3.9 |
| ooffice | — | 3.1 | — | 3.2 |
| samba | — | 6.3 | — | 6.3 |

## LZRC v2.0 Prototype

Preliminary results with binary tree match finder + range coder:

| File | L20 (BWT) | LZRC (16 MB window) | Change |
|------|-----------|---------------------|--------|
| mozilla | 2.93× | **3.07×** | **+5%** |
| samba | 5.03× | 4.90× | -3% |
| ooffice | 2.53× | 2.16× | -15% |
| dickens | 4.07× | 3.13× | -23% |

LZRC outperforms BWT on mozilla (binary archive). Multi-trial integration will select the best strategy per file.
