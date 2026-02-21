/**
 * @file preprocess.h
 * @brief Preprocessing module — Data transformations before compression.
 *
 * Stage 1 of the MaxCompression pipeline.
 * These transforms reorganize data to increase compressibility
 * without losing any information.
 */

#ifndef MCX_PREPROCESS_H
#define MCX_PREPROCESS_H

#include "../internal.h"

/* ─── Burrows-Wheeler Transform ──────────────────────────────────────── */

/**
 * Apply the Burrows-Wheeler Transform.
 *
 * @param dst         Output buffer (must be at least size bytes)
 * @param primary_idx Output: the index of the original string's position
 * @param src         Input data
 * @param size        Data size in bytes
 * @return            Number of bytes written, or error
 */
size_t mcx_bwt_forward(uint8_t* dst, size_t* primary_idx,
                       const uint8_t* src, size_t size);

/**
 * Reverse the Burrows-Wheeler Transform.
 */
size_t mcx_bwt_inverse(uint8_t* dst, size_t primary_idx,
                       const uint8_t* src, size_t size);

/* ─── Move-to-Front Transform ────────────────────────────────────────── */

/**
 * Apply Move-to-Front encoding in-place.
 * Transforms data so that recently seen bytes get small values.
 */
void mcx_mtf_encode(uint8_t* data, size_t size);

/**
 * Reverse Move-to-Front encoding in-place.
 */
void mcx_mtf_decode(uint8_t* data, size_t size);

/* ─── Delta Encoding ─────────────────────────────────────────────────── */

/**
 * Apply delta encoding in-place.
 * Each byte becomes the difference from the previous byte.
 */
void mcx_delta_encode(uint8_t* data, size_t size);

/**
 * Reverse delta encoding in-place.
 */
void mcx_delta_decode(uint8_t* data, size_t size);

/* ─── Run-Length Encoding ────────────────────────────────────────────── */

/**
 * Apply RLE compression.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output buffer capacity
 * @param src       Input data
 * @param src_size  Input data size
 * @return          Compressed size, or error
 */
size_t mcx_rle_encode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size);

/**
 * Decode RLE compressed data.
 */
size_t mcx_rle_decode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size);

#endif /* MCX_PREPROCESS_H */
