/**
 * @file cm.h
 * @brief Context Mixing compression engine for MaxCompression L28
 *
 * PAQ-style context mixing with:
 * - 29 context models (order-0 through order-8, word, sparse, indirect, etc.)
 * - StateMap (count-based adaptive probabilities)
 * - 5 logistic mixers with different context granularities
 * - Interpolated SSE (Secondary Symbol Estimation)
 * - SSE-mixer blending for final probability
 * - LZMA-style range coder
 *
 * Results: ~3.94× on alice29.txt (beats bzip2 by 10.7%, matches PAQ8)
 */

#ifndef MCX_CM_H
#define MCX_CM_H

#include <stdint.h>
#include <stddef.h>

/**
 * Compress data using context mixing (MCX L28).
 * @param dst     Output buffer
 * @param dst_cap Output buffer capacity
 * @param src     Input data
 * @param src_size Input data size
 * @return Compressed size, or 0 on error
 */
size_t mcx_cm_compress(uint8_t *dst, size_t dst_cap,
                       const uint8_t *src, size_t src_size);

/**
 * Decompress CM-compressed data.
 * @param dst     Output buffer
 * @param dst_cap Output buffer capacity
 * @param src     Compressed data
 * @param src_size Compressed data size
 * @return Decompressed (original) size, or 0 on error
 */
size_t mcx_cm_decompress(uint8_t *dst, size_t dst_cap,
                         const uint8_t *src, size_t src_size);

/**
 * Initialize CM lookup tables. Must be called once before compress/decompress.
 * Thread-safe after initialization.
 */
void mcx_cm_init(void);

#endif /* MCX_CM_H */
