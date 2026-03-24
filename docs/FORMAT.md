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
| 6 | 1 | `level` | Compression level used (1–26) |
| 7 | 1 | `strategy` | Compression strategy (see below) |
| 8 | 8 | `original_size` | Uncompressed size in bytes (LE uint64) |
| 16 | 4 | `header_crc32` | CRC32 of bytes 0–15 |

### Flags

| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 0 | `0x01` | `HAS_ORIG_SIZE` | Original size field is valid |
| 1 | `0x02` | `STREAMING` | Stream mode (original size unknown) |
| 2 | `0x04` | `E8E9` | E8/E9 x86 filter applied at frame level |
| 3 | `0x08` | `ADAPTIVE_BLOCKS` | Variable-sized blocks (original block sizes stored in frame) |
| 4 | `0x10` | `INT_DELTA` | Sorted integer delta preprocessing applied |
| 5 | `0x20` | `INT_DELTA_W4` | Int-delta width: 0=16-bit, 1=32-bit (only valid with `INT_DELTA`) |
| 6 | `0x40` | `LZP` | LZP preprocessing applied (repeated block removal before compression) |
| 7 | `0x80` | `NIBBLE_SPLIT` | Nibble-split preprocessing (high/low nibble streams before BWT) |

**Note:** All 8 flag bits are now allocated. Future preprocessing flags require a format extension (v2+ header).

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
| 9 | `LZRC` | LZ + Range Coder (v2.0, binary tree match finder) |
| 10 | `CM` | Context Mixing (L28, bit-level adaptive compression) |

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
| `0xA8` | LZ16+rANS | LZ77 (16-bit offsets) + rANS entropy coding |
| `0xA9` | LZ24+rANS | LZ77 (24-bit offsets) + rANS entropy coding |
| `0xAA` | LZ16+FSE | LZ77 (16-bit offsets) + FSE entropy coding |
| `0xAB` | LZ16+RAW | LZ77 (16-bit offsets), no entropy coding |
| `0xAC` | LZ24+FSE | LZ77 (24-bit offsets) + FSE entropy coding |
| `0xAD` | LZ24+RAW | LZ77 (24-bit offsets), no entropy coding |
| `0xAE` | LZ16+AAC | LZ77 (16-bit offsets) + adaptive arithmetic coding |
| `0xAF` | LZ24+AAC | LZ77 (24-bit offsets) + adaptive arithmetic coding |
| `0xB0` | LZRC | LZ + Range Coder (v2.0, adaptive models) |
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

## LZRC Format (Block Type `0xB0`)

v2.0 LZ + Range Coder. Uses a binary tree match finder with adaptive range-coded models.

```
[original_size  : uint32 LE]    — uncompressed block size
[window_log     : uint8]        — window size as log2 (20=1MB, 24=16MB)
[rc_data        : ...]          — range-coded token stream
```

**Token types (range-coded):**
- **is_match** (1 bit, context-dependent): 0 = literal, 1 = match
- **Literal**: 8-bit byte coded via bit-tree (16 contexts based on previous byte)
- **Match**:
  - **is_rep** (1 bit): 0 = new distance, 1 = repeat distance
  - **rep_index** (if is_rep=1): binary tree encoding of rep0–rep3
  - **length**: 3-tier model (short 4–11, medium 12–19, extra 20–275)
  - **distance** (if is_rep=0): 6-bit slot tree + extra bits + alignment

**Distance slot encoding (64 slots):**
- Slots 0–3: no extra bits (distances 0–3)
- Slots 4–17: 1–6 context-coded extra bits
- Slots 18+: direct bits (fixed 50/50) + 4 alignment bits

All models are adaptive — no tables stored in the stream. Encoder and decoder maintain identical state.

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

## Preprocessing Flags

### Adaptive Blocks (`ADAPTIVE_BLOCKS`, 0x08)

When set, block sizes vary based on data entropy. High-entropy regions use smaller blocks (≤4 MB) and low-entropy regions use larger blocks (≤64 MB). An additional table of original block sizes is stored after the block count. Only used on BWT strategies for files >64 MB.

### Integer Delta (`INT_DELTA`, 0x10 + `INT_DELTA_W4`, 0x20)

Auto-detected on sorted integer sequences. The encoder detects runs of monotonically increasing 16-bit or 32-bit integers and replaces them with their deltas (differences between consecutive values). Width is indicated by bit 5: 0 = 16-bit integers, 1 = 32-bit integers. Achieves up to 9× improvement on sorted uint16 arrays.

### LZP Preprocessing (`LZP`, 0x40)

Lempel-Ziv Prediction removes repeated blocks before the main compression pipeline. The outer frame stores the LZP-compressed data and original size; decompression reverses LZP first, then passes the result through the standard decoder.

### Nibble Split (`NIBBLE_SPLIT`, 0x80)

Splits each byte into high and low nibbles, grouping all high nibbles together followed by all low nibbles. Improves BWT compression on structured binary data where nibble-level patterns exist. Applied as a trial — only used when it reduces output size.

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
