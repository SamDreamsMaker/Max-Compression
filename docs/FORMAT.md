# MaxCompression File Format Specification (v1)

## Overview

MCX uses a simple frame-based format with per-block compression.

## Frame Header (20 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | `magic` | `0x0158434D` ("MCX\x01") |
| 4 | 1 | `version` | Format version (currently 1) |
| 5 | 1 | `flags` | Bit flags (see below) |
| 6 | 1 | `level` | Compression level used (1-25) |
| 7 | 1 | `strategy` | Strategy used (see below) |
| 8 | 8 | `original_size` | Original uncompressed size (LE uint64) |
| 16 | 4 | `header_checksum` | CRC32 of header bytes 0-15 |

### Flags (byte 5)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `HAS_ORIG_SIZE` | Original size field is valid |
| 1 | `STREAMING` | Stream mode (no original size) |
| 2 | `E8E9` | E8/E9 x86 filter was applied (decode after decompress) |
| 3-7 | Reserved | Must be 0 |

### Strategies (byte 7)

| Value | Name | Description |
|-------|------|-------------|
| 0 | `STORE` | No compression |
| 1 | `DEFAULT` | BWT pipeline (auto-tuned) |
| 2 | `LZ_FAST` | LZ77 greedy + tANS |
| 3 | `LZ_HC` | LZ77 lazy + hash chains + tANS |
| 4 | `BWT` | BWT + MTF + RLE + entropy coder |
| 6 | `BABEL` | Smart mode (L20+) |
| 7 | `STRIDE` | Stride-delta preprocessing |
| 8 | `LZ24` | LZ77 with 24-bit offsets |

## Block Format

After the frame header, the file contains one or more compressed blocks:

| Field | Size | Description |
|-------|------|-------------|
| `num_blocks` | 4 bytes (LE uint32) | Number of blocks |
| Block 0 size | 4 bytes (LE uint32) | Compressed size of block 0 |
| Block 0 data | variable | Compressed block data |
| Block 1 size | 4 bytes (LE uint32) | Compressed size of block 1 |
| Block 1 data | variable | ... |

Maximum block size: 16 MB (16,777,216 bytes).

## Block Types

The first byte of each block's data determines the compression method:

| Byte | Type | Pipeline |
|------|------|----------|
| `0x00` | STORE | Uncompressed block |
| `0xAA` | LZ16+FSE | LZ77 (16-bit offsets) + FSE entropy coding |
| `0xAB` | LZ16+RAW | LZ77 (16-bit offsets) + raw (FSE didn't help) |
| `0xAC` | LZ24+FSE | LZ77 (24-bit offsets) + FSE entropy coding |
| `0xAD` | LZ24+RAW | LZ77 (24-bit offsets) + raw |
| Other | Genome | BWT pipeline — byte encodes genome (see below) |

## Genome Byte

For BWT-pipeline blocks, the first byte encodes the processing pipeline:

| Bits | Field | Values |
|------|-------|--------|
| 0 | `use_bwt` | 0=skip BWT, 1=apply BWT |
| 1 | `use_mtf_rle` | 0=skip MTF+RLE, 1=apply MTF+RLE |
| 2 | `use_delta` | 0=skip delta, 1=apply delta coding |
| 3-4 | `entropy_coder` | 0=Huffman, 1=rANS, 2=CM-rANS |
| 5-7 | `cm_learning` | 6=multi-table rANS, 7=RLE2 flag |

### BWT Block Layout

After the genome byte:

1. **Primary index** (4 bytes LE uint32) — BWT primary index
2. **Compressed data** — entropy-coded BWT+MTF+RLE2 output

### Multi-table rANS Format (cm_learning=6)

| Field | Size | Description |
|-------|------|-------------|
| `orig_size` | 4 bytes | Original (pre-entropy) size |
| `n_tables` | 1 byte | Number of frequency tables (4-6) |
| `n_groups` | 4 bytes | Number of 50-byte groups |
| Per-table: bitmap | 32 bytes | Which symbols are active |
| Per-table: freqs | variable | Varint-encoded 14-bit frequencies |
| `sel_comp_sz` | 4 bytes | Compressed selector size |
| Selectors | variable | rANS-compressed table selectors |
| Body | variable | Interleaved 2-state rANS coded data |

## Entropy Coders

- **rANS**: Order-0, 14-bit precision (M=16384), sparse frequency table
- **Multi-table rANS**: 4-6 tables with K-means clustering, group size 50
- **tANS/FSE**: Used in LZ pipeline for literal/match encoding
- **CM-rANS**: Order-1 context-mixed rANS (deprecated — overhead too high)

## Preprocessing Filters

### E8/E9 x86 Filter
Applied at file level before block compression. Converts relative x86 CALL (0xE8) and JMP (0xE9) addresses to absolute. Signaled by `MCX_FLAG_E8E9` in frame header. Decoder applies inverse after block decompression.

### Stride-Delta
Auto-detected for binary data with fixed-width records. Applies delta coding at detected stride width, producing many zeros that compress efficiently.

### RLE2 (RUNA/RUNB)
Bijective base-2 encoding of zero runs in MTF output. Only active when `cm_learning=7` in the genome byte.
