/**
 * @file nibble_split.c
 * @brief Nibble-split preprocessing for structured binary data.
 *
 * Splits each byte into high and low nibbles, packing them into
 * separate halves of the output. This groups similar value ranges
 * together, improving BWT effectiveness on structured binary data
 * (databases, spreadsheets, binary records).
 *
 * Size-preserving: output is exactly the same size as input.
 *
 * Layout for N bytes (N must be even):
 *   First N/2 bytes:  packed high nibbles (2 per byte)
 *   Last N/2 bytes:   packed low nibbles (2 per byte)
 *
 * For odd N: last byte's nibbles go at positions [N/2-1] and [N-1].
 */

#include "../internal.h"
#include <string.h>

void mcx_nibble_split_encode(uint8_t* dst, const uint8_t* src, size_t size)
{
    if (size < 2) {
        if (size == 1) dst[0] = src[0];
        return;
    }
    
    size_t half = size / 2;
    size_t pairs = size / 2;
    
    for (size_t i = 0; i < pairs; i++) {
        /* Pack two high nibbles into one byte */
        dst[i] = (src[2 * i] & 0xF0) | (src[2 * i + 1] >> 4);
        /* Pack two low nibbles into one byte */
        dst[half + i] = ((src[2 * i] & 0x0F) << 4) | (src[2 * i + 1] & 0x0F);
    }
    
    /* Handle odd last byte */
    if (size & 1) {
        dst[size - 1] = src[size - 1];
    }
}

void mcx_nibble_split_decode(uint8_t* dst, const uint8_t* src, size_t size)
{
    if (size < 2) {
        if (size == 1) dst[0] = src[0];
        return;
    }
    
    size_t half = size / 2;
    size_t pairs = size / 2;
    
    for (size_t i = 0; i < pairs; i++) {
        /* Reconstruct original bytes from high and low nibble streams */
        dst[2 * i]     = (src[i] & 0xF0) | (src[half + i] >> 4);
        dst[2 * i + 1] = ((src[i] & 0x0F) << 4) | (src[half + i] & 0x0F);
    }
    
    /* Handle odd last byte */
    if (size & 1) {
        dst[size - 1] = src[size - 1];
    }
}
