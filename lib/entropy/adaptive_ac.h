/**
 * @file adaptive_ac.h
 * @brief Adaptive Arithmetic Coder with order-1 context modeling.
 *
 * No stored frequency tables — both encoder and decoder build identical
 * adaptive models on the fly. Zero overhead beyond the bit stream itself.
 *
 * This replaces CM-rANS for scenarios where table storage overhead hurts.
 */

#ifndef MCX_ADAPTIVE_AC_H
#define MCX_ADAPTIVE_AC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Adaptive arithmetic compression with order-1 context model.
 * @return Compressed size, or 0 on error.
 */
size_t mcx_adaptive_ac_compress(uint8_t* dst, size_t dst_cap,
                                 const uint8_t* src, size_t src_size);

/**
 * Adaptive arithmetic decompression.
 * @return Decompressed size, or 0 on error.
 */
size_t mcx_adaptive_ac_decompress(uint8_t* dst, size_t dst_cap,
                                   const uint8_t* src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif /* MCX_ADAPTIVE_AC_H */
