# MCX File Format Specification

**Version:** 1  
**Status:** Stable  
**Last updated:** 2026-03-18

## Overview

MCX is a frame-based lossless compression format. Each `.mcx` file contains a single frame with a fixed header followed by one or more independently-decompressible blocks.

## Frame Header (20 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Magic (0x0158434D = "MCX\x01")                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Version     |    Flags      |    Level      |   Strategy    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Original Size (uint64 LE)                  |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Header CRC32                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | `magic` | `0x0158434D` (`"MCX\x01"` in little-endian) |
| 4 | 1 | `version` | Format version (currently `1`) |
| 5 | 1 | `flags` | Bitfield (see below) |
| 6 | 1 | `level` | Compression level used (1–25) |
| 7 | 1 | `strategy` | Compression strategy (see below) |
| 8 | 8 | `original_size` | Uncompressed size in bytes (LE uint64) |
| 16 | 4 | `header_crc32` | CRC32 of bytes 0–15 |

### Flags

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `HAS_ORIG_SIZE` | Original size field is valid |
| 1 | `STREAMING` | Stream mode (original size unknown) |
| 2 | `E8E9` | E8/E9 x86 filter applied at frame level |
| 3–7 | Reserved | Must be zero |

### Strategies

| Value | Name | Description |
|-------|------|-------------|
| 0 | `STORE` | Uncompressed |
| 1 | `DEFAULT` | BWT pipeline (genome-optimized) |
| 2 | `LZ_FAST` | LZ77 greedy + tANS |
| 3 | `LZ_HC` | LZ77 lazy + hash chains + tANS/FSE/AAC |
| 4 | `BWT` | Forced BWT + MTF + RLE + entropy |
| 6 | `BABEL` | Smart Mode (L20+, auto-detect) |
| 7 | `STRIDE` | Stride-delta preprocessing |
| 8 | `LZ24` | LZ77 with 24-bit offsets (16 MB window) |

## Block Layout

After the frame header:

```
[num_blocks : uint32 LE]
[block_0_compressed_size : uint32 LE] [block_0_data : ...]
[block_1_compressed_size : uint32 LE] [block_1_data : ...]
...
```

**Maximum block size:** 64 MB (67,108,864 bytes).

Blocks are independently decompressible, enabling parallel decoding.

## Block Types

The first byte of each block's data identifies the compression method:

| Byte | Type | Description |
|------|------|-------------|
| `0x00` | STORE | Uncompressed (block data follows directly) |
| `0xAA` | LZ16+FSE | LZ77 (16-bit offsets) + FSE entropy coding |
| `0xAB` | LZ16+RAW | LZ77 (16-bit offsets), no entropy coding |
| `0xAC` | LZ24+FSE | LZ77 (24-bit offsets) + FSE entropy coding |
| `0xAD` | LZ24+RAW | LZ77 (24-bit offsets), no entropy coding |
| `0xAE` | LZ16+AAC | LZ77 (16-bit offsets) + adaptive arithmetic coding |
| `0xAF` | LZ24+AAC | LZ77 (24-bit offsets) + adaptive arithmetic coding |
| Other | BWT Genome | BWT pipeline — byte encodes configuration (see below) |

## BWT Genome Byte

For BWT-pipeline blocks, the first byte encodes the processing pipeline configuration:

| Bits | Field | Values |
|------|-------|--------|
| 0 | `use_bwt` | 0 = skip, 1 = apply BWT |
| 1 | `use_mtf_rle` | 0 = skip, 1 = apply MTF + RLE |
| 2 | `use_delta` | 0 = skip, 1 = apply delta coding |
| 3–4 | `entropy_coder` | 0 = Huffman, 1 = rANS, 2 = CM-rANS |
| 5–7 | `cm_learning` | 6 = multi-table rANS, 7 = RLE2 active |

### BWT Block Data

```
[genome_byte : 1 byte]
[primary_index : uint32 LE]    — BWT primary index for inverse transform
[compressed_data : ...]         — entropy-coded output
```

## Multi-table rANS Format

Used when `cm_learning = 6` in the genome byte.

```
[original_size  : uint32 LE]    — pre-entropy size
[n_tables       : uint8]        — number of frequency tables (4–6)
[n_groups       : uint32 LE]    — number of 50-byte groups

For each table (n_tables times):
  [bitmap        : 32 bytes]    — bitfield of active symbols (bit i = symbol i present)
  [freq_0        : varint]      — 14-bit frequency for first active symbol
  [freq_1        : varint]      — ...
  ...

[sel_comp_size  : uint32 LE]    — compressed selector stream size
[sel_data       : ...]          — rANS-compressed table selectors (MTF'd)

[body           : ...]          — interleaved 2-state rANS coded data
```

**Varint encoding:** Values 0–127 use 1 byte. Values 128–16383 use 2 bytes (high bit set in first byte).

## Adaptive Arithmetic Coding Format

Used for LZ block types `0xAE` and `0xAF`. Order-1 adaptive model with 256 contexts.

```
[original_size  : uint32 LE]    — uncompressed LZ token stream size
[ac_data        : ...]          — arithmetic-coded bitstream
```

The decoder reconstructs the model adaptively — no explicit frequency tables are stored.

## E8/E9 x86 Filter

When the `E8E9` flag (bit 2) is set in the frame header:

1. **Encoding:** Before compression, scan for `0xE8` (CALL) and `0xE9` (JMP) opcodes. Convert the following 4-byte relative address to absolute (add current position).
2. **Decoding:** After decompression, apply the inverse transform (subtract position from addresses).

Auto-detected when ≥ 0.5% of bytes are `0xE8` or `0xE9`.

## Stride-Delta Preprocessing

For structured binary data (detected by Smart Mode):

1. Detect optimal stride width (1–512 bytes) via autocorrelation
2. Apply delta coding at the detected stride: `out[i] = in[i] - in[i - stride]`
3. Result contains many zeros → compresses efficiently with RLE2 + rANS

The stride value is encoded in the block's genome/metadata.

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| Magic | `0x0158434D` | Frame identifier |
| Max block size | 64 MB | Maximum uncompressed block |
| rANS precision | 14 bits | Frequency table resolution (M = 16384) |
| Multi-rANS tables | 4–6 | K-means clustered tables |
| Multi-rANS group size | 50 bytes | Symbols per group for table selection |
| Max match length | 258 (LZ) / 273 (LZRC) | Maximum match length |
| LZ16 max offset | 65535 | 16-bit offset limit |
| LZ24 max offset | 16777216 | 24-bit offset (16 MB window) |
