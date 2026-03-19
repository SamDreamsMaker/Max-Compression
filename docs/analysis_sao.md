# SAO Compression Analysis

**Date:** 2026-03-19
**File:** `corpora/silesia/sao` — SAO Star Catalog (binary astronomical data)
**Size:** 7,251,944 bytes (6.92 MB)

## Summary

`sao` is MCX's worst-performing Silesia file: **1.48x** (L20) vs xz's **1.64x**.
The gap is 484 KB (11% more bytes in MCX output). This document explains why.

## File Structure

- **258,998 records × 28 bytes** — perfect alignment, no remainder
- Binary format: mix of IEEE 754 doubles, integers, and flag bytes
- Records represent star entries (coordinates, magnitudes, identifiers)
- Overall entropy: **7.53 bits/byte** (near-maximum; theoretical minimum: 1.06x)

## Compression Results

| Compressor     | Size       | Ratio  |
|----------------|------------|--------|
| MCX L6         | 5,650,682  | 1.28x  |
| MCX L9         | 5,366,037  | 1.35x  |
| MCX L12 (BWT)  | 4,943,509  | 1.47x  |
| MCX L20 (best) | 4,899,239  | 1.48x  |
| MCX L24 (LZRC) | 5,111,864  | 1.42x  |
| MCX L26 (LZRC) | 4,975,122  | 1.46x  |
| gzip -9        | 5,327,041  | 1.36x  |
| bzip2 -9       | 4,940,524  | 1.47x  |
| **xz -9**      | **4,415,072** | **1.64x** |

MCX L20 uses multi-trial (tries L9, L12, L26) and picks the smallest.
L12 BWT path wins at L20, beating pure LZRC (L26) by 76 KB.

## Root Cause Analysis

### Per-Column Entropy (28-byte records)

| Columns          | Count | Entropy    | Description                     |
|------------------|-------|------------|---------------------------------|
| 0-5, 8-13       | 12    | ~8.0 bits  | Essentially random (FP mantissa)|
| 14               | 1     | 6.1 bits   | Mostly random                   |
| 6, 18, 20-22, 24-26 | 8 | 4.5-5.8 bits | Moderate structure           |
| 7, 15, 19       | 3     | 0.6-1.0 bits | Highly structured (flags/type)|
| 16-17            | 2     | 2.5-2.6 bits | Structured                    |

**46% of bytes (13 columns) have near-maximum entropy and are fundamentally incompressible.**
These are IEEE 754 double-precision mantissa bits — effectively random.

### Delta Coding Analysis

- Columns 5-7, 14-15 benefit from delta coding (sorted/sequential data)
- Columns 16-27 get **worse** with delta (unsorted fields)
- Overall delta-columnar entropy: 1.43x — worse than raw columnar (1.45x)

### Columnar Transposition

Stride-28 transposition + deflate gives only 1.02x improvement over raw deflate.
The high-entropy columns dominate and negate any benefit from separating the structured columns.

## Why xz Wins

xz (LZMA2) gets 1.64x vs MCX's 1.48x — a 0.16x gap. LZMA2 advantages:

1. **Position-dependent literal coding**: LZMA2 uses `pos % alignment` as part of its
   context for literal bytes, which helps with fixed-record binary data
2. **Rep match modeling**: LZMA2 caches recent match distances and efficiently codes
   repeated distances — valuable for fixed-stride records
3. **Sophisticated probability estimation**: More context bits, adaptive probability
   tables with exponential decay
4. **Larger context for range coding**: LZMA2's literal context uses the previous byte
   AND the byte at the matched position, giving ~2x more context information

## Can MCX Close the Gap?

### What WON'T help much:
- **Columnar transposition**: Only 2% gain even with known stride — not worth overhead
- **Delta coding**: Hurts more columns than it helps
- **Larger LZ windows**: Already using 16MB at L26, matches are the bottleneck
- **Better BWT**: Already close to bzip2, BWT is ~optimal for this data type

### What COULD help (future work):
1. **Position-aware LZRC**: Add `pos % stride` context to literal coding in the range
   coder. This is the single biggest gain opportunity — it's what gives LZMA2 its edge.
   Estimated improvement: +0.05x to +0.10x.
2. **Rep distance cache in LZRC**: Cache 4 recent match distances, code reps with fewer
   bits. Structured binary data reuses distances heavily.
   Estimated improvement: +0.02x to +0.05x.
3. **Hybrid columnar-LZ**: Detect fixed-record structure, transpose columns, then LZ
   compress each group. Complex to implement, marginal gain on sao specifically.

### Realistic target:
With (1) + (2), MCX could reach ~1.55-1.58x on sao. Fully matching xz's 1.64x would
require a complete LZMA2-class context model rewrite.

## Conclusion

sao's poor compression is **structural, not a bug**. Nearly half its bytes are
high-entropy floating-point mantissa data that no lossless compressor handles well.
The gap with xz comes from LZMA2's position-dependent context modeling, which is a
fundamental architectural advantage for structured binary data.

MCX's multi-trial at L20 is working correctly — it tries BWT, LZ-HC, and LZRC, and
picks the BWT result as the best. The 1.48x ratio is competitive with bzip2 (1.47x)
and well ahead of gzip (1.36x).

**Priority for improvement: Medium-low.** The absolute gap is 484 KB, and closing it
requires significant LZRC architectural changes. Better ROI from improving other files.
