# MCX Benchmark Results

**Version:** 2.1.1  
**Date:** 2026-03-19  
**Platform:** Linux x64, GCC 11, single-threaded

## Silesia Corpus — Level 20 (Best)

| File | Size | MCX L20 | Ratio | vs bzip2 | vs xz |
|------|------|---------|-------|----------|-------|
| dickens | 10.2 MB | 2,503 KB | **4.07×** | +18% | -9% |
| mozilla | 51.2 MB | 15,893 KB | **3.22×** | +15% | +10% |
| mr | 10.0 MB | 2,330 KB | **4.28×** | +33% | +10% |
| nci | 33.6 MB | 1,308 KB | **25.65×** | +60% | +3% |
| ooffice | 6.2 MB | 2,407 KB | **2.56×** | +24% | +1% |
| osdb | 10.1 MB | 2,498 KB | **4.04×** | +42% | +8% |
| reymont | 6.6 MB | 1,118 KB | **5.93×** | +29% | +3% |
| samba | 21.6 MB | 4,274 KB | **5.06×** | +29% | -2% |
| sao | 7.3 MB | 4,899 KB | **1.48×** | +1% | -10% |
| webster | 41.5 MB | 7,139 KB | **5.81×** | +21% | -5% |
| xml | 5.3 MB | 416 KB | **12.86×** | +65% | +3% |
| x-ray | 8.5 MB | 3,936 KB | **2.15×** | +16% | -8% |
| **TOTAL** | **211.9 MB** | **48,719 KB** | **4.35×** | **+29%** | **+0.3%** |

**MCX L20 beats bzip2 on 12/12 Silesia files (100%).**  
**MCX L20 beats xz on 7/12 Silesia files (58%).**  
**MCX L20 total is competitive with xz (4.35× vs ~4.34×).**

## Silesia Corpus — Level 9 (Best LZ)

| File | Size | MCX L9 | Ratio |
|------|------|--------|-------|
| dickens | 10.2 MB | 4,192 KB | **2.37×** |
| mozilla | 51.2 MB | 19,140 KB | **2.61×** |
| mr | 10.0 MB | 3,477 KB | **2.80×** |
| nci | 33.6 MB | 3,382 KB | **9.69×** |
| ooffice | 6.2 MB | 3,221 KB | **1.87×** |
| osdb | 10.1 MB | 3,444 KB | **2.86×** |
| reymont | 6.6 MB | 2,083 KB | **3.11×** |
| samba | 21.6 MB | 5,743 KB | **3.67×** |
| sao | 7.3 MB | 5,240 KB | **1.35×** |
| webster | 41.5 MB | 13,350 KB | **3.03×** |
| xml | 5.3 MB | 767 KB | **6.80×** |
| x-ray | 8.5 MB | 5,851 KB | **1.41×** |
| **TOTAL** | **211.9 MB** | **69,889 KB** | **2.96×** |

## Canterbury Corpus — All Levels

| File | Size | L6 | L6× | L9 | L9× | L20 | L20× |
|------|------|-----|-----|-----|-----|------|------|
| alice29.txt | 152 KB | 62 KB | 2.38× | 61 KB | 2.42× | 42 KB | **3.53×** |
| asyoulik.txt | 122 KB | 56 KB | 2.18× | 55 KB | 2.20× | 39 KB | **3.15×** |
| cp.html | 24 KB | 10 KB | 2.48× | 10 KB | 2.52× | 8 KB | **3.15×** |
| fields.c | 11 KB | 4 KB | 2.55× | 4 KB | 2.70× | 3 KB | **3.39×** |
| grammar.lsp | 4 KB | 2 KB | 2.03× | 2 KB | 2.21× | 1 KB | **2.46×** |
| kennedy.xls | 1,006 KB | 209 KB | 4.80× | 109 KB | 9.25× | 20 KB | **50.11×** |
| lcet10.txt | 417 KB | 162 KB | 2.57× | 160 KB | 2.60× | 105 KB | **3.98×** |
| plrabn12.txt | 471 KB | 221 KB | 2.13× | 218 KB | 2.16× | 141 KB | **3.33×** |
| ptt5 | 501 KB | 61 KB | 8.18× | 58 KB | 8.70× | 49 KB | **10.19×** |
| sum | 37 KB | 14 KB | 2.60× | 14 KB | 2.64× | 11 KB | **3.33×** |
| xargs.1 | 4 KB | 2 KB | 1.68× | 2 KB | 1.84× | 2 KB | **2.14×** |

## Decompression Speed — Silesia Corpus

| File | Size | L12 Ratio | L12 Decomp | L20 Ratio | L20 Decomp |
|------|------|-----------|------------|-----------|------------|
| dickens | 9.7 MB | 4.07× | 4.7 MB/s | 4.07× | 4.7 MB/s |
| mozilla | 48.8 MB | 2.93× | 5.0 MB/s | 3.22× | 16.6 MB/s |
| mr | 9.5 MB | 4.28× | 5.5 MB/s | 4.28× | 5.5 MB/s |
| nci | 32.0 MB | 25.65× | 5.1 MB/s | 25.65× | 5.1 MB/s |
| ooffice | 5.9 MB | 2.18× | 5.3 MB/s | 2.56× | 12.7 MB/s |
| osdb | 9.6 MB | 4.04× | 5.3 MB/s | 4.04× | 5.3 MB/s |
| reymont | 6.3 MB | 5.93× | 5.6 MB/s | 5.93× | 5.6 MB/s |
| samba | 20.6 MB | 5.03× | 5.9 MB/s | 5.05× | 26.0 MB/s |
| sao | 6.9 MB | 1.47× | 4.7 MB/s | 1.48× | 4.5 MB/s |
| webster | 39.5 MB | 5.81× | 4.5 MB/s | 5.81× | 4.5 MB/s |
| xml | 5.1 MB | 12.86× | 6.2 MB/s | 12.86× | 6.3 MB/s |
| x-ray | 8.1 MB | 2.15× | 5.1 MB/s | 2.15× | 5.1 MB/s |
| **TOTAL** | **202.1 MB** | **4.17×** | **5.0 MB/s** | **4.35×** | **6.8 MB/s** |

**Notes:**
- Best of 3 runs, single-threaded, sd-132105 (Linux x86_64)
- L20 is faster on mozilla/ooffice/samba because these use LZRC (range coder),
  which decodes faster than BWT+rANS for binary data
- BWT-compressed files (text) decompress at ~4.5–6 MB/s consistently
- LZRC-compressed files decompress at ~12–26 MB/s

## Silesia Corpus — Level 24 (LZRC Fast)

LZRC with hash chain match finder — ~3× faster than L26, 2-5% larger.

| File | Size | MCX L24 | Ratio |
|------|------|---------|-------|
| dickens | 9.7 MB | 3,548 KB | 2.81× |
| mozilla | 48.8 MB | 15,833 KB | 3.16× |
| mr | 9.5 MB | 3,197 KB | 3.05× |
| nci | 32.0 MB | 2,497 KB | 13.13× |
| ooffice | 5.9 MB | 2,734 KB | 2.20× |
| osdb | 9.6 MB | 3,322 KB | 2.96× |
| reymont | 6.3 MB | 1,829 KB | 3.54× |
| samba | 20.6 MB | 4,490 KB | 4.70× |
| sao | 6.9 MB | 4,992 KB | 1.42× |
| webster | 39.5 MB | 10,911 KB | 3.71× |
| xml | 5.1 MB | 561 KB | 9.30× |
| x-ray | 8.1 MB | 5,090 KB | 1.63× |
| **TOTAL** | **202.1 MB** | **59,004 KB** | **3.43×** |

L24 is ideal for binary data where L26's full binary tree is too slow.
For text, BWT (L12/L20) still dominates.

## LZRC v2.0 Engine — Level 26

Direct LZRC (LZ + Range Coder, no BWT fallback):

| File | LZRC | Ratio | Speed |
|------|------|-------|-------|
| alice29 | 50 KB | **2.99×** | 0.9 MB/s |
| ooffice | 2,671 KB | **2.25×** | 0.7 MB/s |
| dickens | 2,990 KB | **3.33×** | 0.5 MB/s |
| samba | 4,175 KB | **5.06×** | 0.4 MB/s |
| mozilla | 15,507 KB | **3.23×** | 0.3 MB/s |

## enwik8 (100 MB Wikipedia)

The [enwik8](https://mattmahoney.net/dc/textdata.html) dataset is 100 MB of English Wikipedia XML,
a standard benchmark for text compression.

| Compressor | Size | Ratio | vs MCX L20 |
|------------|------|-------|------------|
| gzip -9 | 36,445 KB | 2.74× | MCX +53% ✅ |
| bzip2 -9 | 29,009 KB | 3.45× | MCX +21% ✅ |
| xz -9 | 24,865 KB | 4.02× | MCX +4% ✅ |
| **MCX L3** | 43,733 KB | 2.23× | — |
| **MCX L6** | 40,441 KB | 2.41× | — |
| **MCX L9** | 38,970 KB | 2.51× | — |
| **MCX L12** | 23,351 KB | 4.18× | — |
| **MCX L20** | **23,351 KB** | **4.18×** | — |
| MCX L24 | 32,252 KB | 3.03× | — |
| MCX L26 | 27,718 KB | 3.52× | — |

**MCX L20 beats xz -9 by 4% on enwik8** — BWT + multi-table rANS excels on large text.
All roundtrips verified.

## Compression Levels

| Level | Strategy | Silesia Total | Speed |
|-------|----------|---------------|-------|
| L1 | LZ greedy + rANS | ~2.1× | 30-50 MB/s |
| L3 | LZ greedy + rANS | ~2.1× | 30-50 MB/s |
| L6 | LZ-HC + rANS | ~2.4× | 5-15 MB/s |
| L7-L8 | LZ-HC + AAC | ~2.4× | 3-10 MB/s |
| L9 | LZ-HC (d=64) + AAC | ~2.4× | 2-5 MB/s |
| L12 | BWT+genetic | ~4.16× | 0.3-12 MB/s |
| L20 | Smart (BWT+LZRC+multi) | **4.35×** | 0.1-0.5 MB/s |
| L24 | LZRC-HC (fast) | ~3.2× | 1-3 MB/s |
| L26 | LZRC-BT (best) | ~3.5× | 0.3-1 MB/s |

## Key Improvements in v2.0–v2.1

| Feature | Impact | Version |
|---------|--------|---------|
| LZRC engine (BT + Range Coder) | mozilla +10% vs BWT | v2.0 |
| Lazy evaluation | Universal +1-5% | v2.0 |
| 4 rep distances | +0.3% on large files | v2.0 |
| LZMA-style matched literals | +0.3% universal | v2.0 |
| Binary→LZRC routing at L20 | mozilla 2.93×→3.22× | v2.0 |
| E8/E9 x86 filter | ooffice +16% | v2.0 |
| Multi-trial (BWT/LZ/LZRC) | Guarantees best per-file | v2.0 |
| Embedded libdivsufsort | BWT 2× faster | v2.1 |
| LZRC fast mode (L24, HC) | 3× faster, 2-5% larger | v2.1 |
| L26 window 64MB | mozilla -0.11% (16.7KB smaller) | v2.1 |
| RC branch hints | ~2% faster decompress | v2.1 |
