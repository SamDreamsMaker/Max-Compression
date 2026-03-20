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

/* ─── Nibble-Split Preprocessing ─────────────────────────────────────── */

/**
 * Split bytes into high/low nibble streams (size-preserving).
 * Groups similar value ranges together to improve BWT on binary data.
 *
 * @param dst   Output buffer (must be at least size bytes)
 * @param src   Input data
 * @param size  Data size in bytes
 */
void mcx_nibble_split_encode(uint8_t* dst, const uint8_t* src, size_t size);

/**
 * Reverse nibble-split: recombine high/low nibble streams.
 */
void mcx_nibble_split_decode(uint8_t* dst, const uint8_t* src, size_t size);

/* ─── LZP (LZ-Prediction) Preprocessing ──────────────────────────────── */

/**
 * Apply LZP preprocessing: detect and remove repeated blocks using
 * context-based prediction (like LZ77 but offset-free).
 *
 * Best used before BWT on mixed code/text files with repeated sections.
 * The decoder rebuilds the same hash table from decoded output.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output buffer capacity
 * @param src       Input data
 * @param src_size  Input data size
 * @return          Compressed size, or 0 if LZP didn't reduce size
 */
size_t mcx_lzp_encode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size);

/**
 * Decode LZP preprocessed data.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output buffer capacity
 * @param src       LZP-encoded data
 * @param src_size  Encoded data size
 * @return          Decoded size, or 0 on error
 */
size_t mcx_lzp_decode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size);

/* ─── Sorted Integer Delta ───────────────────────────────────────────── */

/**
 * Detect if data looks like a sorted integer sequence.
 *
 * @param data  Input data
 * @param size  Data size
 * @return      Detected integer width (2 or 4), or 0 if not sorted ints
 */
int mcx_sorted_int_detect(const uint8_t* data, size_t size);

/**
 * Apply integer-width delta encoding in-place.
 * For width=2: treats data as uint16_t LE, stores deltas.
 * For width=4: treats data as uint32_t LE, stores deltas.
 *
 * @param data   Data buffer (modified in-place)
 * @param size   Data size in bytes
 * @param width  Integer width (2 or 4)
 */
void mcx_int_delta_encode(uint8_t* data, size_t size, int width);

/**
 * Reverse integer-width delta encoding in-place.
 */
void mcx_int_delta_decode(uint8_t* data, size_t size, int width);

#endif /* MCX_PREPROCESS_H */
