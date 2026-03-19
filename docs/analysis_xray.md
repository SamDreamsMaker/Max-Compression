# X-Ray Compression Analysis

**Date:** 2026-03-19
**File:** `corpora/silesia/x-ray` — Medical X-ray image (raw 16-bit grayscale)
**Size:** 8,474,240 bytes (8.08 MB)

## Summary

MCX **already beats** all standard compressors on x-ray. The task's original
xz comparison (2.24x) was incorrect — actual xz -9 achieves only 1.89x.
MCX's BWT path at 2.15x is the clear winner.

## File Properties

- **16-bit pixels**: 4,237,120 pixels (little-endian uint16)
- **Effective bit depth**: ~12-bit (only 38 values > 4095, 3527 unique values)
- **Pixel range**: 0–46,600 (mean: 2033.5)
- **H0 entropy**: 6.51 bits/byte
- **Best detected stride**: 2 (16-bit pixel boundary, gain: 1.70 bits/byte)

## Compression Results

| Compressor      | Size       | Ratio  |
|-----------------|------------|--------|
| MCX L6 (LZ-HC)  | 6,474,760  | 1.31x  |
| MCX L9 (LZ-HC)  | 5,991,245  | 1.41x  |
| **MCX L12 (BWT)** | **3,935,673** | **2.15x** |
| MCX L20 (best)  | 3,935,673  | 2.15x  |
| MCX L24 (LZRC)  | 5,212,717  | 1.63x  |
| MCX L26 (LZRC)  | 5,157,315  | 1.64x  |
| gzip -9         | 6,037,713  | 1.40x  |
| bzip2 -9        | 4,051,112  | 2.09x  |
| xz -9           | 4,489,868  | 1.89x  |

MCX L12/L20 beats:
- xz by **14%** (554 KB smaller)
- bzip2 by **3%** (115 KB smaller)
- gzip by **53%** (2.1 MB smaller)

## Stride-Delta Analysis

Stride=2 is correctly detected (H0: 6.51 → 4.80, gain: 1.70 bits/byte), confirming
16-bit pixel structure. However, **BWT without stride-delta outperforms stride+BWT**:

L20's multi-trial correctly selects L12 (plain BWT) over the STRIDE path.

**Why BWT wins without stride**: BWT's suffix sorting naturally groups similar 2-byte
contexts. The pixel data has strong local correlation that BWT captures through
context-sorted byte sequences. Stride-delta disrupts these patterns by converting
slowly-varying values into pseudo-random residuals.

Deflate-level experiments confirm BWT's advantage:
- Raw deflate: 1.40x
- Stride-2 delta + deflate: 1.68x
- 16-bit delta + split H/L + deflate: 1.93x
- All inferior to MCX BWT's 2.15x

## Conclusion

**No improvement needed.** MCX already has the best ratio on x-ray among all
tested compressors. BWT + multi-table rANS is the optimal strategy for 16-bit
medical image data. The multi-trial system correctly rejects stride-delta in
favor of plain BWT.
