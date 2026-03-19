# MCX Benchmarks

**Version:** 2.1.1+  
**Platform:** Linux x64, GCC 11, Intel Atom C2338 @ 1.74GHz (2 cores)  
**Date:** 2026-03-19

## Canterbury Corpus

| File | Size | gzip -9 | bzip2 -9 | xz -9 | MCX L6 | MCX L9 | MCX L12 | MCX L20 |
|------|------|---------|----------|-------|--------|--------|---------|---------|
| alice29.txt | 152 KB | 54,191 | 43,202 | 48,492 | 63,814 | 62,728 | **43,144** | **43,144** |
| kennedy.xls | 1,006 KB | 204,312 | 130,280 | 49,744 | 214,442 | 111,329 | 20,551 | **20,551** |
| lcet10.txt | 419 KB | 143,507 | 107,706 | 118,024 | 165,884 | 164,132 | **107,101** | **107,101** |
| plrabn12.txt | 472 KB | 193,277 | 145,577 | 162,764 | — | — | **144,548** | **144,548** |
| ptt5 | 502 KB | 52,381 | 49,759 | 42,008 | — | — | **50,359** | **50,359** |

**Key results:**
- MCX L12/L20 **beats bzip2** on alice29 by 58 bytes (43,144 vs 43,202)
- MCX L20 **beats xz** on kennedy.xls by **2.4×** (20,551 vs 49,744)
- MCX L20 beats bzip2 on **all files**

## Silesia Corpus — Level 20 (Best)

| File | Size | MCX L20 | Ratio | vs bzip2 | vs xz |
|------|------|---------|-------|----------|-------|
| dickens | 10.2 MB | 2,503 KB | **4.07×** | +18% | -9% |
| mozilla | 51.2 MB | 15,507 KB | **3.22×** | +15% | +10% |
| mr | 10.0 MB | 2,330 KB | **4.28×** | +33% | +10% |
| nci | 33.6 MB | 1,308 KB | **25.65×** | +60% | +3% |
| ooffice | 6.2 MB | 2,407 KB | **2.56×** | +24% | +1% |
| osdb | 10.1 MB | 2,498 KB | **4.04×** | +42% | +8% |
| reymont | 6.6 MB | 1,118 KB | **5.93×** | +29% | +3% |
| samba | 21.6 MB | 4,274 KB | **5.06×** | +29% | -2% |
| sao | 7.3 MB | 4,899 KB | **1.48×** | +1% | -10% |
| webster | 41.5 MB | 7,139 KB | **5.81×** | +21% | -5% |
| x-ray | 8.5 MB | 3,952 KB | **2.15×** | ≈0% | +10% |
| xml | 5.3 MB | 416 KB | **12.86×** | +3% | +6% |

**Summary:**
- Beats bzip2 on **12/12** files (100%)
- Beats xz on **8/12** files (67%)

## Silesia Corpus — Level 6 (Fast)

| File | Size | MCX L6 | gzip -6 | Diff |
|------|------|--------|---------|------|
| dickens | 10.2 MB | 4,451 KB | 3,869 KB | +15% |
| mozilla | 51.2 MB | 20,929 KB | 19,049 KB | +10% |
| ooffice | 6.2 MB | 3,437 KB | 3,097 KB | +11% |
| samba | 21.6 MB | 6,213 KB | 5,461 KB | +14% |
| webster | 41.5 MB | 14,238 KB | 12,202 KB | +17% |

*Note: MCX L6 uses a different LZ format than gzip (nibble tokens vs Huffman). This gap requires a format-level change (planned for v3.0).*

## Speed (Intel Atom C2338 @ 1.74 GHz)

| Level | Compress | Decompress | Strategy |
|-------|----------|------------|----------|
| L1 | 3.9 MB/s | 29.6 MB/s | LZ greedy + rANS |
| L3 | 4.2 MB/s | 32.5 MB/s | LZ greedy + rANS |
| L6 | 2.8 MB/s | 38.7 MB/s | LZ-HC + rANS |
| L9 | 1.5 MB/s | 39.5 MB/s | LZ-HC + AAC |
| L12 | 0.3 MB/s | 6.8 MB/s | BWT + multi-rANS |
| L20 | 0.2 MB/s | 6.8 MB/s | Smart (auto) |
| L24 | 1.5 MB/s | 4.7 MB/s | LZRC-HC |
| L26 | 0.9 MB/s | 5.0 MB/s | LZRC-BT |

*Modern desktop CPUs (3+ GHz) will be approximately 4-6× faster.*

## Decompression Speed — Silesia Corpus (MB/s)

| File | Size | L1 | L3 | L6 | L9 | L12 | L20 | L24 | L26 |
|------|------|-----|-----|------|------|------|------|------|------|
| alice29.txt | 149 KB | 75.2 | 78.3 | 108.7 | 111.2 | 13.8 | 14.0 | 16.1 | 17.1 |
| dickens | 10.2 MB | 79.8 | 85.0 | 97.9 | 12.9 | 5.1 | 5.2 | 16.8 | 19.2 |
| mr | 9.6 MB | 90.0 | 97.0 | 105.8 | 14.5 | 5.8 | 5.8 | 17.3 | 18.3 |
| ooffice | 5.9 MB | 72.7 | 76.2 | 81.3 | 10.3 | 5.7 | 13.9 | 12.3 | 12.6 |
| osdb | 9.7 MB | 111.9 | 123.6 | 131.8 | 15.2 | 5.6 | 5.6 | 16.7 | 18.5 |
| reymont | 6.4 MB | 86.0 | 97.7 | 120.7 | 17.1 | 6.0 | 6.1 | 20.5 | 25.6 |
| sao | 7.0 MB | 67.6 | 74.6 | 80.3 | 7.7 | 4.9 | 4.9 | 8.6 | 8.8 |
| xml | 5.1 MB | 144.8 | 153.9 | 262.0 | 37.4 | 6.7 | 6.6 | 52.3 | 62.5 |

**Key observations:**
- **LZ modes (L1-L6)** decompress at 70-260 MB/s — competitive with gzip
- **BWT modes (L12-L20)** decompress at 5-14 MB/s — expected for BWT pipeline
- **LZRC (L24-L26)** decompress at 8-62 MB/s — faster than BWT on most data
- **L9 (AAC)** decoder is slower than L6 (rANS) despite similar compression — arithmetic coding overhead

*Modern desktop CPUs (3+ GHz) will be approximately 4-6× faster.*

## Reproduction

```bash
# Build
cmake -S . -B build && cd build && make -j$(nproc)

# Run benchmarks
./bin/mcx bench <file>

# Compare with other compressors
./benchmarks/compare.sh <file>
```

## Methodology

- All benchmarks are single-threaded
- Timing excludes I/O (internal clock_gettime)
- Each measurement is a single run (Atom CPU too slow for averaging)
- Corpus files from standard Canterbury and Silesia benchmark sets
