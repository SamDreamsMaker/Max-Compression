/**
 * @file babel_transform.c
 * @brief Babel Transform v2 — Adaptive context-predictive XOR.
 *
 * Zero-overhead prediction: both encoder and decoder build the same
 * prediction table on-the-fly, byte by byte. After each byte is
 * processed, the table is updated with the actual (original) value.
 *
 * This means NO prediction table is stored in the output — the
 * transform has zero overhead beyond the 3-byte header.
 *
 * Algorithm (encode):
 *   for each byte at position i:
 *     h = hash(context[i-3..i-1])
 *     output[i] = input[i] XOR prediction[h]
 *     prediction[h] = input[i]  // update with actual value
 *
 * Algorithm (decode):
 *   for each byte at position i:
 *     h = hash(decoded[i-3..i-1])  // use decoded context
 *     decoded[i] = input[i] XOR prediction[h]
 *     prediction[h] = decoded[i]  // same update as encoder
 *
 * Both sides stay in perfect sync → lossless, zero overhead.
 */

#include "babel_transform.h"
#include <string.h>
#include <stdlib.h>

/* ── Hash function ─────────────────────────────────────────────── */

static inline uint32_t babel_hash(const uint8_t* ctx)
{
    uint32_t h = (uint32_t)ctx[0] * 2654435761u
               ^ (uint32_t)ctx[1] * 2246822519u
               ^ (uint32_t)ctx[2] * 3266489917u;
    return (h >> (32 - BABEL_HASH_BITS)) & BABEL_HASH_MASK;
}

/* ── Forward transform (adaptive, zero overhead) ───────────────── */

size_t mcx_babel_forward(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size)
{
    if (dst == NULL || src == NULL || src_size <= BABEL_CTX_LEN) return 0;
    
    /* Output: [1: ctx_len] [2: hash_bits LE] [src_size bytes: XOR residuals] */
    size_t out_size = 3 + src_size;
    if (dst_cap < out_size) return 0;
    
    /* Write minimal header */
    dst[0] = BABEL_CTX_LEN;
    dst[1] = (uint8_t)(BABEL_HASH_BITS & 0xFF);
    dst[2] = (uint8_t)((BABEL_HASH_BITS >> 8) & 0xFF);
    
    /* Allocate prediction table (initialized to 0) */
    uint8_t* pred = (uint8_t*)calloc(BABEL_HASH_SIZE, 1);
    if (!pred) return 0;
    
    uint8_t* out = dst + 3;
    
    /* Copy context prefix unchanged (we need these for the first hash) */
    memcpy(out, src, BABEL_CTX_LEN);
    /* Update prediction table with context bytes */
    /* For the first 3 bytes we can't predict, but we can start updating */
    
    /* Adaptive XOR transform */
    for (size_t i = BABEL_CTX_LEN; i < src_size; i++) {
        uint32_t h = babel_hash(src + i - BABEL_CTX_LEN);
        uint8_t prediction = pred[h];
        out[i] = src[i] ^ prediction;
        pred[h] = src[i]; /* Update with actual value */
    }
    
    free(pred);
    return out_size;
}

/* ── Inverse transform (adaptive, mirrors forward exactly) ─────── */

size_t mcx_babel_inverse(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size)
{
    if (dst == NULL || src == NULL || src_size < 3) return 0;
    
    /* Read header */
    uint8_t ctx_len = src[0];
    uint16_t hash_bits = (uint16_t)src[1] | ((uint16_t)src[2] << 8);
    
    if (ctx_len != BABEL_CTX_LEN || hash_bits != BABEL_HASH_BITS) return 0;
    
    const uint8_t* xor_data = src + 3;
    size_t orig_size = src_size - 3;
    
    if (dst_cap < orig_size) return 0;
    if (orig_size <= BABEL_CTX_LEN) {
        memcpy(dst, xor_data, orig_size);
        return orig_size;
    }
    
    /* Allocate prediction table (same initial state as encoder) */
    uint8_t* pred = (uint8_t*)calloc(BABEL_HASH_SIZE, 1);
    if (!pred) return 0;
    
    /* Copy context prefix unchanged */
    memcpy(dst, xor_data, BABEL_CTX_LEN);
    
    /* Adaptive inverse XOR */
    for (size_t i = BABEL_CTX_LEN; i < orig_size; i++) {
        uint32_t h = babel_hash(dst + i - BABEL_CTX_LEN);
        uint8_t prediction = pred[h];
        dst[i] = xor_data[i] ^ prediction; /* Recover original byte */
        pred[h] = dst[i]; /* Update with decoded value (= original) */
    }
    
    free(pred);
    return orig_size;
}
