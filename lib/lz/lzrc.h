/**
 * @file lzrc.h
 * @brief LZ + Range Coder compressor (v2.0)
 *
 * Combines binary tree match finder with adaptive range coder
 * for high-ratio LZ compression. Best for binary/mixed data.
 */

#ifndef MCX_LZRC_H
#define MCX_LZRC_H

#include <stdint.h>
#include <stddef.h>

/**
 * Compress with LZ+RC (v2.0 engine).
 * @param dst Output buffer
 * @param dst_cap Output buffer capacity
 * @param src Input data
 * @param src_size Input size
 * @param window_log Window size as log2 (20=1MB, 24=16MB)
 * @param bt_depth Max binary tree depth (32-128)
 * @return Compressed size, or 0 on error
 */
size_t mcx_lzrc_compress(uint8_t* dst, size_t dst_cap,
                          const uint8_t* src, size_t src_size,
                          int window_log, int bt_depth);

/**
 * Decompress LZ+RC data.
 * @param dst Output buffer
 * @param dst_cap Output buffer capacity  
 * @param src Compressed data
 * @param src_size Compressed size
 * @return Decompressed size, or 0 on error
 */
size_t mcx_lzrc_decompress(uint8_t* dst, size_t dst_cap,
                            const uint8_t* src, size_t src_size);

#endif /* MCX_LZRC_H */
