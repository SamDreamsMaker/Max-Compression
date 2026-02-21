/**
 * @file internal.h
 * @brief Internal shared definitions for all MaxCompression modules.
 *
 * This header is NOT part of the public API. Do not include it
 * from external projects.
 */

#ifndef MCX_INTERNAL_H
#define MCX_INTERNAL_H

#include "maxcomp/maxcomp.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  Error codes
 * ═══════════════════════════════════════════════════════════════════════ */

/** Marker bit for error values (top bit of size_t). */
#define MCX_ERROR_BIT  ((size_t)1 << (sizeof(size_t) * 8 - 1))

#define MCX_ERROR(code)       (MCX_ERROR_BIT | (size_t)(code))
#define MCX_IS_ERROR(val)     (((val) & MCX_ERROR_BIT) != 0)
#define MCX_ERROR_CODE(val)   ((val) & ~MCX_ERROR_BIT)

typedef enum {
    MCX_ERR_NONE             = 0,
    MCX_ERR_GENERIC          = 1,
    MCX_ERR_DST_TOO_SMALL    = 2,
    MCX_ERR_SRC_CORRUPTED    = 3,
    MCX_ERR_INVALID_MAGIC    = 4,
    MCX_ERR_INVALID_LEVEL    = 5,
    MCX_ERR_ALLOC_FAILED     = 6,
    MCX_ERR_UNKNOWN_BLOCK    = 7,
    MCX_ERR_MAX
} mcx_error_code_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  Frame header structure
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  Byte offset | Size | Description
 *  -----------+------+-------------------------------------------
 *   0          | 4    | Magic number (MCX_MAGIC = 0x0158434D)
 *   4          | 1    | Version (currently 1)
 *   5          | 1    | Flags (bit 0: has original size)
 *   6          | 1    | Compression level used
 *   7          | 1    | Block strategy ID
 *   8          | 8    | Original (uncompressed) size (if flag set)
 *  16          | 4    | Header checksum (XXH32 of bytes 0-15)
 *  -----------+------+-------------------------------------------
 *  Total: 20 bytes
 */

#define MCX_FRAME_HEADER_SIZE    20
#define MCX_FRAME_VERSION        1
#define MCX_FLAG_HAS_ORIG_SIZE   0x01
#define MCX_FLAG_STREAMING       0x02

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint8_t  level;
    uint8_t  strategy;
    uint64_t original_size;
    uint32_t header_checksum;
} mcx_frame_header_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  Compression strategy IDs
 * ═══════════════════════════════════════════════════════════════════════ */

typedef enum {
    MCX_STRATEGY_STORE     = 0,   /* No compression */
    MCX_STRATEGY_FAST      = 1,   /* RLE + Huffman (legacy) */
    MCX_STRATEGY_DEFAULT   = 2,   /* BWT + MTF + ANS (v0.2) */
    MCX_STRATEGY_BEST      = 3,   /* Context mixing + ANS (v0.2) */
    MCX_STRATEGY_LZ_FAST   = 4,   /* LZ77 greedy + Huffman (v1.0 L1-3) */
    MCX_STRATEGY_LZ_HC     = 5,   /* LZ77 lazy + Huffman (v1.0 L4-9) */
} mcx_strategy_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  Streaming Contexts
 * ═══════════════════════════════════════════════════════════════════════ */

typedef enum {
    MCX_CSTATE_INIT,
    MCX_CSTATE_FLUSH_HEADER,
    MCX_CSTATE_ACCUMULATE,
    MCX_CSTATE_FLUSH_BLOCK,
    MCX_CSTATE_FLUSH_EOF
} mcx_cstate_t;

struct mcx_cctx_s {
    mcx_cstate_t state;
    int          level;
    uint8_t*     in_buf;
    size_t       in_pos;
    uint8_t*     out_buf;
    size_t       out_pos;
    size_t       out_size;
    uint8_t      header[MCX_FRAME_HEADER_SIZE];
};

typedef enum {
    MCX_DSTATE_INIT_HEADER,
    MCX_DSTATE_READ_BLOCK_HEADER,
    MCX_DSTATE_READ_BLOCK_PAYLOAD,
    MCX_DSTATE_FLUSH_DECODED,
    MCX_DSTATE_FINISHED
} mcx_dstate_t;

struct mcx_dctx_s {
    mcx_dstate_t state;
    mcx_strategy_t strategy;
    uint8_t*     in_buf;
    size_t       in_pos;
    size_t       in_needed;
    uint8_t*     out_buf;
    size_t       out_pos;
    size_t       out_size;
    uint32_t     current_block_comp_size;
};

/* ═══════════════════════════════════════════════════════════════════════
 *  Data type detection (analyzer output)
 * ═══════════════════════════════════════════════════════════════════════ */

typedef enum {
    MCX_DTYPE_UNKNOWN      = 0,
    MCX_DTYPE_TEXT_UTF8     = 1,
    MCX_DTYPE_TEXT_ASCII    = 2,
    MCX_DTYPE_BINARY       = 3,
    MCX_DTYPE_STRUCTURED   = 4,  /* JSON, XML, CSV */
    MCX_DTYPE_EXECUTABLE   = 5,
    MCX_DTYPE_NUMERIC      = 6,  /* arrays of numbers */
    MCX_DTYPE_HIGH_ENTROPY  = 7,  /* already compressed / encrypted */
} mcx_data_type_t;

typedef struct {
    mcx_data_type_t type;
    double          entropy;          /* Shannon entropy (bits/byte) */
    double          self_similarity;  /* 0.0 = none, 1.0 = perfect fractal */
    size_t          block_size;       /* recommended block size */
} mcx_analysis_t;


/* ═══════════════════════════════════════════════════════════════════════
 *  Utility macros
 * ═══════════════════════════════════════════════════════════════════════ */

#define MCX_MIN(a, b)  ((a) < (b) ? (a) : (b))
#define MCX_MAX(a, b)  ((a) > (b) ? (a) : (b))

/** Default block size for block-based compression (64 KB). */
#define MCX_DEFAULT_BLOCK_SIZE  (64 * 1024)

/** Maximum block size (1 MB). */
#define MCX_MAX_BLOCK_SIZE      (1024 * 1024)

#endif /* MCX_INTERNAL_H */
