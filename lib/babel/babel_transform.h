/**
 * @file babel_transform.h
 * @brief Babel Transform — Context-predictive XOR preprocessing stage.
 *
 * The "Library of Babel" transform: for each byte, predict its value
 * from the preceding context and store the XOR residual. When the
 * prediction is correct the residual is 0x00, creating long runs of
 * zeros that downstream entropy coders (ANS, FSE) compress extremely well.
 *
 * Two-pass algorithm:
 *   Pass 1 — Build a compact prediction table: hash(context) → most_frequent_byte
 *   Pass 2 — XOR each byte with its prediction
 *
 * The prediction table is stored in the output so the decoder can reconstruct it.
 *
 * Lossless & bijective: XOR is its own inverse.
 */

#ifndef MCX_BABEL_TRANSFORM_H
#define MCX_BABEL_TRANSFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ─────────────────────────────────────────────── */

/** Context length in bytes (how many preceding bytes to hash). */
#define BABEL_CTX_LEN       3

/** Hash table size: 2^BABEL_HASH_BITS entries.
 *  16 bits = 64K entries × 1 byte = 64 KB overhead per block.
 *  Good trade-off for blocks up to 1 MB. */
#define BABEL_HASH_BITS     16
#define BABEL_HASH_SIZE     (1u << BABEL_HASH_BITS)
#define BABEL_HASH_MASK     (BABEL_HASH_SIZE - 1)

/** Header stored in the compressed stream (adaptive v2 — no table stored):
 *  [1 byte: ctx_len] [2 bytes: hash_bits LE]
 */
#define BABEL_HEADER_SIZE   3

/* ── API ───────────────────────────────────────────────────────── */

/**
 * Forward Babel transform (compress side).
 *
 * @param dst       Output buffer (must be at least BABEL_HEADER_SIZE + src_size).
 * @param dst_cap   Capacity of dst.
 * @param src       Input data.
 * @param src_size  Input size.
 * @return          Number of bytes written to dst, or 0 on error.
 */
size_t mcx_babel_forward(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size);

/**
 * Inverse Babel transform (decompress side).
 *
 * @param dst       Output buffer (must be at least the original data size).
 * @param dst_cap   Capacity of dst.
 * @param src       Babel-transformed data (header + XOR residuals).
 * @param src_size  Size of src.
 * @return          Number of bytes written to dst (= original size), or 0 on error.
 */
size_t mcx_babel_inverse(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size);

/**
 * Bound: maximum output size for a given input size.
 * Only 3 bytes overhead (header), data is same size.
 */
static inline size_t mcx_babel_bound(size_t src_size) {
    return BABEL_HEADER_SIZE + src_size;
}

#ifdef __cplusplus
}
#endif

#endif /* MCX_BABEL_TRANSFORM_H */
