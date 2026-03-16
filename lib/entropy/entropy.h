/**
 * @file entropy.h
 * @brief Entropy coding module — Transforms symbols into bits.
 *
 * Stage 3 of the MaxCompression pipeline.
 * Currently provides Huffman coding as the initial encoder.
 * ANS (Asymmetric Numeral Systems) will be added as the primary encoder.
 */

#ifndef MCX_ENTROPY_H
#define MCX_ENTROPY_H

#include "../internal.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  Huffman Coding
 * ═══════════════════════════════════════════════════════════════════════ */

/** Maximum Huffman code length in bits. */
#define MCX_HUFFMAN_MAX_BITS  15

/**
 * Huffman encoding table (computed from frequency data).
 */
typedef struct {
    uint16_t code[256];       /* Huffman code for each symbol */
    uint8_t  bits[256];       /* Number of bits for each symbol */
    uint32_t freq[256];       /* Symbol frequencies (input) */
    int      num_symbols;     /* Number of symbols with freq > 0 */
} mcx_huffman_table_t;

/**
 * Build a Huffman table from frequency data.
 *
 * @param table  Huffman table (freq[] must be filled in)
 * @return       0 on success, error code otherwise
 */
int mcx_huffman_build(mcx_huffman_table_t* table);

/**
 * Compress data using Huffman coding.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output buffer capacity
 * @param src       Input data
 * @param src_size  Input data size
 * @param table     Pre-built Huffman table (or NULL to auto-build)
 * @return          Compressed size, or error
 */
size_t mcx_huffman_compress(uint8_t* dst, size_t dst_cap,
                            const uint8_t* src, size_t src_size,
                            mcx_huffman_table_t* table);

/**
 * Decompress Huffman-encoded data.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output buffer capacity
 * @param src       Compressed data
 * @param src_size  Compressed data size
 * @return          Decompressed size, or error
 */
size_t mcx_huffman_decompress(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size);

/* ═══════════════════════════════════════════════════════════════════════
 *  rANS (range Asymmetric Numeral Systems)
 *
 *  The revolution in entropy coding by Jarosław Duda (2014).
 *  Combines arithmetic coding's compression ratio with Huffman's speed.
 *
 *  Key insight: encode the entire message into a single integer.
 *  Each symbol modifies the integer based on its probability.
 *  Integer-only arithmetic — no floating point needed.
 * ═══════════════════════════════════════════════════════════════════════ */

/** Precision for frequency normalization (log2 of total).
 *  M = 2^RANS_PRECISION = 4096. All symbol frequencies must sum to M. */
#define MCX_RANS_PRECISION      14
#define MCX_RANS_SCALE          (1u << MCX_RANS_PRECISION)  /* 16384 */

/** State bounds for 32-bit rANS. */
#define MCX_RANS_STATE_LOWER    (MCX_RANS_SCALE)            /* L = M */
#define MCX_RANS_STATE_UPPER    (MCX_RANS_STATE_LOWER << 16) /* L * 2^16 */

/**
 * rANS frequency table — normalized so that all freqs sum to M (4096).
 */
typedef struct {
    uint16_t freq[256];      /* Normalized frequency (probability × M) */
    uint16_t cumfreq[256];   /* Cumulative frequency (CDF) */
    uint8_t  lookup[MCX_RANS_SCALE]; /* Symbol lookup table for decoding */
} mcx_rans_table_t;

/**
 * Build a normalized rANS table from raw symbol counts.
 * Normalizes frequencies so they sum to exactly MCX_RANS_SCALE (4096).
 * Ensures no active symbol has frequency 0.
 *
 * @param table     Output rANS table
 * @param raw_freq  Raw frequency counts for each of the 256 symbols
 * @return          0 on success
 */
int mcx_rans_build_table(mcx_rans_table_t* table, const uint32_t* raw_freq);

/**
 * Compress data using rANS entropy coding.
 *
 * Format of compressed output:
 *   [4 bytes: original size]
 *   [256 × 2 bytes: normalized frequency table]
 *   [variable: rANS encoded data, little-endian uint16 stream]
 *   [4 bytes: final rANS state]
 *
 * @param dst       Output buffer
 * @param dst_cap   Output buffer capacity
 * @param src       Input data
 * @param src_size  Input data size
 * @return          Compressed size, or error
 */
size_t mcx_rans_compress(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size);

/**
 * Decompress rANS-encoded data.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output buffer capacity
 * @param src       Compressed data (as produced by mcx_rans_compress)
 * @param src_size  Compressed data size
 * @return          Decompressed size, or error
 */
size_t mcx_rans_decompress(uint8_t* dst, size_t dst_cap,
                           const uint8_t* src, size_t src_size);

#endif /* MCX_ENTROPY_H */
