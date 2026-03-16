/**
 * @file cmrans_sparse.c
 * @brief Order-1 rANS with sparse context tables.
 *
 * Instead of storing 256×256×2 = 128KB of frequency tables,
 * we only store active (context, symbol) pairs.
 *
 * Format:
 *   4 bytes: original size
 *   1 byte:  num_active_contexts (0 = 256)
 *   For each active context:
 *     1 byte:  context byte
 *     1 byte:  num_symbols (0 = 256)
 *     num_symbols × 3 bytes: (symbol, freq_hi, freq_lo)
 *   4 bytes: rANS state
 *   variable: encoded bitstream (uint16 array, reversed)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "entropy.h"

#define CMRS_SCALE MCX_RANS_SCALE  /* 16384 with 14-bit precision */

/* Normalize raw counts to sum = CMRS_SCALE for one context */
static void normalize_ctx(const uint32_t raw[256], uint32_t total,
                          uint16_t freq_out[256], uint16_t cumfreq_out[256])
{
    int i;
    uint32_t sum_norm = 0;
    int max_idx = 0;
    uint32_t max_raw = 0;
    
    if (total == 0) {
        /* Empty context — uniform (shouldn't happen if we filter) */
        memset(freq_out, 0, 256 * sizeof(uint16_t));
        memset(cumfreq_out, 0, 256 * sizeof(uint16_t));
        return;
    }
    
    for (i = 0; i < 256; i++) {
        if (raw[i] > 0) {
            freq_out[i] = (uint16_t)((uint64_t)raw[i] * CMRS_SCALE / total);
            if (freq_out[i] == 0) freq_out[i] = 1;
            sum_norm += freq_out[i];
            if (raw[i] > max_raw) { max_raw = raw[i]; max_idx = i; }
        } else {
            freq_out[i] = 0;
        }
    }
    
    /* Adjust to exactly CMRS_SCALE */
    if (sum_norm > CMRS_SCALE) {
        freq_out[max_idx] -= (uint16_t)(sum_norm - CMRS_SCALE);
    } else if (sum_norm < CMRS_SCALE) {
        freq_out[max_idx] += (uint16_t)(CMRS_SCALE - sum_norm);
    }
    
    /* Build cumulative */
    uint16_t cum = 0;
    for (i = 0; i < 256; i++) {
        cumfreq_out[i] = cum;
        cum += freq_out[i];
    }
}

size_t mcx_cmrans_sparse_compress(uint8_t* dst, size_t dst_cap,
                                   const uint8_t* src, size_t src_size)
{
    uint32_t raw[256][256];
    uint16_t freq[256][256];
    uint16_t cumfreq[256][256];
    uint8_t  lookup[256][CMRS_SCALE]; /* For decoding — not needed here but keep symmetric */
    
    uint16_t* out16;
    size_t out16_cap, out16_pos;
    uint32_t state;
    size_t i;
    
    if (!dst || !src || src_size == 0) return MCX_ERROR(MCX_ERR_GENERIC);
    
    /* Step 1: Count context frequencies */
    memset(raw, 0, sizeof(raw));
    raw[0][src[0]]++;
    for (i = 1; i < src_size; i++) {
        raw[(unsigned)src[i-1]][(unsigned)src[i]]++;
    }
    
    /* Step 2: Find active contexts and normalize */
    uint8_t active_ctx[256];
    int num_ctx = 0;
    
    for (int c = 0; c < 256; c++) {
        uint32_t tot = 0;
        for (int s = 0; s < 256; s++) tot += raw[c][s];
        if (tot > 0) {
            active_ctx[num_ctx++] = (uint8_t)c;
            normalize_ctx(raw[c], tot, freq[c], cumfreq[c]);
        } else {
            memset(freq[c], 0, sizeof(freq[c]));
            memset(cumfreq[c], 0, sizeof(cumfreq[c]));
        }
    }
    
    /* Step 3: Write sparse header */
    size_t hdr_pos = 0;
    
    /* orig_size (4) + num_ctx (1) */
    if (dst_cap < 5) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    uint32_t orig32 = (uint32_t)src_size;
    memcpy(dst, &orig32, 4); hdr_pos = 4;
    dst[hdr_pos++] = (uint8_t)(num_ctx == 256 ? 0 : num_ctx);
    
    for (int ci = 0; ci < num_ctx; ci++) {
        int c = active_ctx[ci];
        /* Count active symbols in this context */
        int num_sym = 0;
        for (int s = 0; s < 256; s++) {
            if (freq[c][s] > 0) num_sym++;
        }
        
        if (hdr_pos + 2 + num_sym * 3 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        
        dst[hdr_pos++] = (uint8_t)c;
        dst[hdr_pos++] = (uint8_t)(num_sym == 256 ? 0 : num_sym);
        
        for (int s = 0; s < 256; s++) {
            if (freq[c][s] > 0) {
                dst[hdr_pos++] = (uint8_t)s;
                dst[hdr_pos++] = (uint8_t)(freq[c][s] >> 8);
                dst[hdr_pos++] = (uint8_t)(freq[c][s] & 0xFF);
            }
        }
    }
    
    /* Step 4: Encode backward */
    out16_cap = src_size + 1024;
    out16 = (uint16_t*)malloc(out16_cap * sizeof(uint16_t));
    if (!out16) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    
    out16_pos = 0;
    state = MCX_RANS_STATE_LOWER;
    
    for (i = src_size; i > 0; i--) {
        uint8_t sym = src[i - 1];
        uint8_t ctx = (i >= 2) ? src[i - 2] : 0;
        uint16_t f = freq[ctx][sym];
        uint16_t cf = cumfreq[ctx][sym];
        
        if (f == 0) {
            /* Should not happen — symbol was in data so freq > 0 */
            free(out16);
            return MCX_ERROR(MCX_ERR_GENERIC);
        }
        
        /* Renormalize */
        while (state >= ((uint32_t)f << 16)) {
            if (out16_pos >= out16_cap) { free(out16); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
            out16[out16_pos++] = (uint16_t)(state & 0xFFFF);
            state >>= 16;
        }
        
        state = ((state / f) << MCX_RANS_PRECISION) + cf + (state % f);
    }
    
    /* Step 5: Write state + bitstream */
    size_t needed = hdr_pos + 4 + out16_pos * 2;
    if (needed > dst_cap) { free(out16); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
    
    memcpy(dst + hdr_pos, &state, 4);
    hdr_pos += 4;
    
    for (size_t j = 0; j < out16_pos; j++) {
        uint16_t val = out16[out16_pos - 1 - j];
        memcpy(dst + hdr_pos + j * 2, &val, 2);
    }
    
    size_t total = hdr_pos + out16_pos * 2;
    free(out16);
    return total;
}

size_t mcx_cmrans_sparse_decompress(uint8_t* dst, size_t dst_cap,
                                     const uint8_t* src, size_t src_size)
{
    uint16_t freq[256][256];
    uint16_t cumfreq[256][256];
    uint8_t  lookup[256][CMRS_SCALE];
    
    if (!dst || !src || src_size < 5) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    
    /* Read orig_size */
    uint32_t orig32;
    memcpy(&orig32, src, 4);
    size_t orig_size = orig32;
    if (orig_size > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    if (orig_size == 0) return 0;
    
    /* Initialize all freqs to 0 */
    memset(freq, 0, sizeof(freq));
    memset(cumfreq, 0, sizeof(cumfreq));
    
    /* Read sparse header */
    size_t pos = 4;
    int num_ctx = src[pos++];
    if (num_ctx == 0) num_ctx = 256;
    
    for (int ci = 0; ci < num_ctx; ci++) {
        if (pos + 2 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        uint8_t c = src[pos++];
        int num_sym = src[pos++];
        if (num_sym == 0) num_sym = 256;
        
        if (pos + num_sym * 3 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        
        for (int si = 0; si < num_sym; si++) {
            uint8_t s = src[pos++];
            freq[c][s] = ((uint16_t)src[pos] << 8) | src[pos + 1];
            pos += 2;
        }
        
        /* Build cumfreq */
        uint16_t cum = 0;
        for (int s = 0; s < 256; s++) {
            cumfreq[c][s] = cum;
            cum += freq[c][s];
        }
        
        /* Build lookup for this context */
        for (int s = 0; s < 256; s++) {
            if (freq[c][s] == 0) continue;
            uint16_t cf = cumfreq[c][s];
            for (uint16_t j = 0; j < freq[c][s]; j++) {
                lookup[c][cf + j] = (uint8_t)s;
            }
        }
    }
    
    /* Read state */
    if (pos + 4 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    uint32_t state;
    memcpy(&state, src + pos, 4);
    pos += 4;
    
    /* Decode forward */
    const uint16_t* bits = (const uint16_t*)(src + pos);
    size_t bits_pos = 0;
    size_t max_bits = (src_size - pos) / 2;
    
    uint8_t ctx = 0;
    for (size_t i = 0; i < orig_size; i++) {
        uint16_t slot = (uint16_t)(state & (CMRS_SCALE - 1));
        uint8_t sym = lookup[ctx][slot];
        uint16_t f = freq[ctx][sym];
        uint16_t cf = cumfreq[ctx][sym];
        
        /* Advance state */
        state = f * (state >> MCX_RANS_PRECISION) + slot - cf;
        
        /* Renormalize */
        while (state < MCX_RANS_STATE_LOWER && bits_pos < max_bits) {
            state = (state << 16) | bits[bits_pos++];
        }
        
        dst[i] = sym;
        ctx = sym;
    }
    
    return orig_size;
}
