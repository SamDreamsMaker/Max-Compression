/**
 * @file ans.c
 * @brief rANS (range Asymmetric Numeral Systems) — The primary entropy coder.
 *
 * rANS by Jarosław Duda (2014) is the state-of-the-art in entropy coding.
 * Used by Facebook Zstandard, Apple LZFSE, Google Draco, JPEG XL.
 *
 * How it works:
 *   The entire message is encoded into a single integer (the "state").
 *   Each symbol transforms the state based on its probability.
 *   The state is periodically "renormalized" by emitting output bytes
 *   to keep it within a bounded range.
 *
 * Key properties:
 *   - Compression within ~0.01 bits/symbol of Shannon entropy
 *   - Integer-only arithmetic (fast — no floating point!)
 *   - LIFO encoding, FIFO decoding (encode backwards, decode forwards)
 *   - Streaming: constant memory, works on arbitrary-length data
 *
 * This implementation uses 32-bit state with 12-bit precision (M=4096).
 *
 * ┌─────────────────────────────────────────────────────┐
 * │  State x encodes information about symbol sequence   │
 * │                                                      │
 * │  Encode: x' = (x ÷ freq[s]) × M + cumfreq[s]       │
 * │               + (x mod freq[s])                      │
 * │                                                      │
 * │  Decode: s = lookup[x mod M]                         │
 * │          x' = freq[s] × (x ÷ M) + (x mod M)        │
 * │               - cumfreq[s]                           │
 * └─────────────────────────────────────────────────────┘
 */

#include "entropy.h"
#include "../internal.h"
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  Frequency Table Normalization
 *
 *  Raw byte counts must be normalized so they sum to exactly M = 4096.
 *  This is crucial: if the sum is off by even 1, decoding will fail.
 *  Any symbol that appears at least once must have freq >= 1.
 * ═══════════════════════════════════════════════════════════════════════ */

int mcx_rans_build_table(mcx_rans_table_t* table, const uint32_t* raw_freq)
{
    uint32_t total;
    int      num_active;
    int      i;
    uint32_t sum;
    int      max_sym;
    uint32_t max_freq;
    int      diff;
    uint16_t cum;

    if (table == NULL || raw_freq == NULL) return -1;

    /* Count total and number of active symbols */
    total = 0;
    num_active = 0;
    for (i = 0; i < 256; i++) {
        total += raw_freq[i];
        if (raw_freq[i] > 0) num_active++;
    }

    if (total == 0 || num_active == 0) return -1;

    /* Normalize: scale frequencies so they sum to MCX_RANS_SCALE (4096) */
    sum = 0;
    max_sym = 0;
    max_freq = 0;

    for (i = 0; i < 256; i++) {
        if (raw_freq[i] == 0) {
            table->freq[i] = 0;
        } else {
            /* Scale proportionally, ensure minimum of 1 */
            uint32_t scaled = (uint32_t)(
                ((uint64_t)raw_freq[i] * MCX_RANS_SCALE + total / 2) / total
            );
            if (scaled == 0) scaled = 1;
            table->freq[i] = (uint16_t)scaled;
            sum += scaled;

            /* Track the most frequent symbol for adjustment */
            if (raw_freq[i] > max_freq) {
                max_freq = raw_freq[i];
                max_sym = i;
            }
        }
    }

    /* Adjust the most frequent symbol to make the sum exactly M.
     * This ensures perfect normalization without distributing
     * rounding error across all symbols. */
    diff = (int)MCX_RANS_SCALE - (int)sum;
    table->freq[max_sym] = (uint16_t)((int)table->freq[max_sym] + diff);

    /* Build cumulative frequency table (CDF) */
    cum = 0;
    for (i = 0; i < 256; i++) {
        table->cumfreq[i] = cum;
        cum += table->freq[i];
    }

    /* Build decode lookup table:
     * For each position p in [0, M), store which symbol owns that position.
     * This gives O(1) symbol lookup during decoding. */
    for (i = 0; i < 256; i++) {
        uint16_t f;
        uint16_t c;
        uint16_t j;
        if (table->freq[i] == 0) continue;
        f = table->freq[i];
        c = table->cumfreq[i];
        for (j = 0; j < f; j++) {
            table->lookup[c + j] = (uint8_t)i;
        }
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  rANS Encoder
 *
 *  Encoding is done BACKWARDS (last symbol first).
 *  This is critical: rANS is LIFO, so encoding in reverse means
 *  the decoder can read symbols in forward order.
 *
 *  State renormalization:
 *    Before encoding a symbol, if the state would overflow,
 *    emit the bottom 16 bits to the output stream.
 *    This keeps the state within [L, L*2^16).
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_rans_compress(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size)
{
    uint32_t raw_freq[256];
    mcx_rans_table_t table;
    uint16_t* out16;
    size_t    out16_cap;
    size_t    out16_pos;
    uint32_t  state;
    size_t    header_size;
    size_t    i;
    size_t    total_size;
    uint32_t  orig_size32;
    size_t    j;
    size_t    byte_count;

    if (dst == NULL || src == NULL || src_size == 0) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }

    /* Step 1: Count byte frequencies */
    memset(raw_freq, 0, sizeof(raw_freq));
    for (i = 0; i < src_size; i++) {
        raw_freq[src[i]]++;
    }

    /* Step 2: Build normalized frequency table */
    if (mcx_rans_build_table(&table, raw_freq) != 0) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }

    /* Step 3: Encode backwards into a temporary uint16 buffer.
     * We encode into a reverse buffer, then copy forward at the end. */
    out16_cap = src_size + 256; /* generous allocation */
    out16 = (uint16_t*)malloc(out16_cap * sizeof(uint16_t));
    if (out16 == NULL) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);

    out16_pos = 0;
    
    /* Dual states for interleaved rANS */
    uint32_t state1 = MCX_RANS_STATE_LOWER;
    uint32_t state2 = MCX_RANS_STATE_LOWER;

    /* Encode symbols in REVERSE order.
     * state1 handles even indices: src[0], src[2], src[4]...
     * state2 handles odd indices:  src[1], src[3], src[5]...
     */
    for (i = src_size; i > 0; i--) {
        size_t   idx = i - 1;
        uint8_t  sym = src[idx];
        uint16_t freq = table.freq[sym];
        uint16_t cumf = table.cumfreq[sym];
        uint32_t* state_ptr = (idx & 1) ? &state2 : &state1;

        /* Renormalize the active state */
        while (*state_ptr >= ((uint32_t)freq << 16)) {
            if (out16_pos >= out16_cap) {
                fprintf(stderr, "ANS ENCODER FAIL: out16_pos %zu >= %zu\n", out16_pos, out16_cap);
                free(out16);
                return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
            }
            out16[out16_pos++] = (uint16_t)(*state_ptr & 0xFFFF);
            *state_ptr >>= 16;
        }

        /* Core rANS encode step */
        *state_ptr = ((*state_ptr / freq) << MCX_RANS_PRECISION)
                   + cumf
                   + (*state_ptr % freq);
    }

    /* Step 4: Write output.
     *
     * Format:
     *   [4 bytes]       Original size (uint32)
     *   [512 bytes]     Normalized freq table (256 × uint16)
     *   [8 bytes]       Final rANS state1 and state2 (2 × uint32)
     *   [variable]      Encoded uint16 stream (reversed)
     */
    header_size = 4 + 256 * 2 + 8;
    byte_count = out16_pos * 2;
    total_size = header_size + byte_count;

    if (total_size > dst_cap) {
        fprintf(stderr, "ANS ENCODER FAIL: total_size %zu > %zu\n", total_size, dst_cap);
        free(out16);
        return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    }

    /* Write original size */
    orig_size32 = (uint32_t)src_size;
    memcpy(dst, &orig_size32, 4);

    /* Write normalized frequency table */
    memcpy(dst + 4, table.freq, 256 * 2);

    /* Write final states */
    memcpy(dst + 4 + 512, &state1, 4);
    memcpy(dst + 4 + 512 + 4, &state2, 4);

    /* Write encoded stream in REVERSE order */
    for (j = 0; j < out16_pos; j++) {
        uint16_t val = out16[out16_pos - 1 - j];
        memcpy(dst + header_size + j * 2, &val, 2);
    }

    free(out16);
    return total_size;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  rANS Decoder
 *
 *  Decoding is done FORWARDS (first symbol first).
 *  Uses the O(1) lookup table to find each symbol.
 *
 *  After decoding a symbol, the state shrinks.
 *  When it drops below L, read 16 bits from the stream to refill.
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_rans_decompress(uint8_t* dst, size_t dst_cap,
                           const uint8_t* src, size_t src_size)
{
    uint32_t orig_size32;
    size_t   orig_size;
    mcx_rans_table_t table;
    uint32_t raw_freq_as_u32[256];
    uint32_t state;
    size_t   stream_pos;
    size_t   stream_end;
    size_t   header_size;
    size_t   i;
    uint16_t val;

    if (dst == NULL || src == NULL) return MCX_ERROR(MCX_ERR_GENERIC);

    header_size = 4 + 256 * 2 + 8;  /* orig_size + freq_table + state1/2 */
    if (src_size < header_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);

    /* Read original size */
    memcpy(&orig_size32, src, 4);
    orig_size = (size_t)orig_size32;

    if (orig_size > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);

    /* Read normalized frequency table directly into the rANS table */
    memcpy(table.freq, src + 4, 256 * 2);

    /* Rebuild cumfreq and lookup from the freq table */
    {
        uint16_t cum = 0;
        for (i = 0; i < 256; i++) {
            table.cumfreq[i] = cum;
            cum += table.freq[i];
        }
        /* Verify normalization */
        if (cum != MCX_RANS_SCALE) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    }

    /* Build decode lookup table */
    for (i = 0; i < 256; i++) {
        uint16_t f, c;
        uint16_t j;
        if (table.freq[i] == 0) continue;
        f = table.freq[i];
        c = table.cumfreq[i];
        for (j = 0; j < f; j++) {
            table.lookup[c + j] = (uint8_t)i;
        }
    }

    /* Read initial states */
    uint32_t state1, state2;
    memcpy(&state1, src + 4 + 512, 4);
    memcpy(&state2, src + 4 + 512 + 4, 4);

    /* Encoded stream starts after header, read as uint16s */
    stream_pos = header_size;
    stream_end = src_size;

    /* Decode symbols forward.
     * state1 handles even indices, state2 handles odd indices. */
    for (i = 0; i < orig_size; i++) {
        uint32_t mask = MCX_RANS_SCALE - 1;
        uint32_t* state_ptr = (i & 1) ? &state2 : &state1;
        
        uint32_t slot = *state_ptr & mask;
        uint8_t  sym  = table.lookup[slot];
        uint16_t freq = table.freq[sym];
        uint16_t cumf = table.cumfreq[sym];

        dst[i] = sym;

        /* Core rANS decode step */
        *state_ptr = (uint32_t)freq * (*state_ptr >> MCX_RANS_PRECISION)
                   + (*state_ptr & mask)
                   - cumf;

        /* Renormalize */
        while (*state_ptr < MCX_RANS_STATE_LOWER && stream_pos + 1 < stream_end) {
            memcpy(&val, src + stream_pos, 2);
            *state_ptr = (*state_ptr << 16) | val;
            stream_pos += 2;
        }
    }

    return orig_size;
}
