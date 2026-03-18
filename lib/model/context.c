/**
 * @file context.c
 * @brief Context modeling + CM-rANS encoder/decoder.
 *
 * Implements order-1 predictive context modeling combined with rANS
 * entropy coding. Each byte is encoded using a probability table
 * conditioned on the previous byte.
 */

#include "model.h"
#include "../entropy/entropy.h"
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  Frequency Model
 * ═══════════════════════════════════════════════════════════════════════ */

void mcx_freq_model_init(mcx_freq_model_t* model)
{
    int i;
    if (model == NULL) return;
    for (i = 0; i < 256; i++) model->freq[i] = 1;
    model->total = 256;
}

void mcx_freq_model_update(mcx_freq_model_t* model, uint8_t byte)
{
    int i;
    if (model == NULL) return;
    model->freq[byte]++;
    model->total++;
    if (model->total > 0x00FFFFFF) {
        model->total = 0;
        for (i = 0; i < 256; i++) {
            model->freq[i] = (model->freq[i] >> 1) | 1;
            model->total += model->freq[i];
        }
    }
}

uint32_t mcx_freq_model_norm_prob(const mcx_freq_model_t* model,
                                  uint8_t byte, uint32_t denom)
{
    uint32_t scaled;
    if (model == NULL || model->total == 0) return denom / 256;
    scaled = (uint32_t)(
        ((uint64_t)model->freq[byte] * denom + model->total / 2) / model->total
    );
    return (scaled == 0) ? 1 : scaled;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Order-1 Context Model
 * ═══════════════════════════════════════════════════════════════════════ */

void mcx_context1_init(mcx_context1_model_t* model)
{
    int i;
    if (model == NULL) return;
    for (i = 0; i < 256; i++) mcx_freq_model_init(&model->contexts[i]);
}

void mcx_context1_update(mcx_context1_model_t* model, uint8_t ctx, uint8_t byte)
{
    if (model == NULL) return;
    mcx_freq_model_update(&model->contexts[ctx], byte);
}

void mcx_context1_get_freqs(const mcx_context1_model_t* model,
                             uint8_t ctx, uint32_t* out_freq, uint32_t denom)
{
    int i; uint32_t sum; int max_sym; uint32_t max_cnt; int diff;
    if (!model || !out_freq) return;
    sum = 0; max_sym = 0; max_cnt = 0;
    for (i = 0; i < 256; i++) {
        uint32_t f = mcx_freq_model_norm_prob(&model->contexts[ctx], (uint8_t)i, denom);
        out_freq[i] = f;
        sum += f;
        if (model->contexts[ctx].freq[i] > max_cnt) {
            max_cnt = model->contexts[ctx].freq[i]; max_sym = i;
        }
    }
    diff = (int)denom - (int)sum;
    if (diff > 0) out_freq[max_sym] += (uint32_t)diff;
    else if (diff < 0 && out_freq[max_sym] > (uint32_t)(-diff) + 1)
        out_freq[max_sym] += (uint32_t)diff;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Internal types for CM-rANS tables (heap allocated to avoid stack overflow)
 *
 *  256 contexts × 256 symbols × 2 bytes (uint16) = 131,072 bytes
 *  256 contexts × 4096 symbols (uint8 lookup) = 1,048,576 bytes
 *
 *  Total = ~1.2MB — must be on heap, NOT stack.
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint16_t freq[256][256];    /* Normalized frequencies per context */
    uint16_t cumfreq[256][256]; /* CDF per context */
} cmrans_tables_t;

typedef struct {
    uint8_t lookup[256][MCX_RANS_SCALE]; /* O(1) decode lookup per context */
} cmrans_lookup_t;

/* Normalize a single context's raw counts to sum = MCX_RANS_SCALE */
static void normalize_context(const uint32_t* raw, uint32_t tot,
                               uint16_t* out_freq, uint16_t* out_cumfreq)
{
    int      i;
    uint32_t scaled_sum = 0;
    int      max_sym    = 0;
    uint32_t max_cnt    = 0;
    int      diff;
    uint16_t cum        = 0;
    uint32_t f[256];

    if (tot == 0) {
        /* Never seen — uniform distribution */
        uint32_t base    = MCX_RANS_SCALE / 256;
        uint32_t remain  = MCX_RANS_SCALE % 256;
        for (i = 0; i < 256; i++) {
            f[i] = base + (i == 0 ? remain : 0);
        }
    } else {
        for (i = 0; i < 256; i++) {
            if (raw[i] == 0) {
                f[i] = 1;
            } else {
                f[i] = (uint32_t)(
                    ((uint64_t)raw[i] * MCX_RANS_SCALE + tot / 2) / tot
                );
                if (f[i] == 0) f[i] = 1;
            }
            scaled_sum += f[i];
            if (raw[i] > max_cnt) { max_cnt = raw[i]; max_sym = i; }
        }
        diff = (int)MCX_RANS_SCALE - (int)scaled_sum;
        if (diff > 0) {
            f[max_sym] += (uint32_t)diff;
        } else if (diff < 0) {
            int to_remove = -diff;
            while (to_remove > 0) {
                /* Find the largest frequency to decrement */
                int m_sym = 0;
                uint32_t m_val = 0;
                int j;
                for (j = 0; j < 256; j++) {
                    if (f[j] > m_val) {
                        m_val = f[j];
                        m_sym = j;
                    }
                }
                /* Safety check: don't reduce below 1 */
                if (m_val <= 1) break;
                f[m_sym]--;
                to_remove--;
            }
        }
    }

    for (i = 0; i < 256; i++) {
        out_freq[i]   = (uint16_t)f[i];
        out_cumfreq[i] = cum;
        cum += out_freq[i];
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  CM-rANS Compression
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_cmrans_compress(uint8_t* dst, size_t dst_cap,
                           const uint8_t* src, size_t src_size)
{
    /* All large arrays on heap */
    cmrans_tables_t* tables;
    uint16_t*        out16;
    size_t           out16_cap;
    size_t           out16_pos;
    uint32_t         state;
    /* size_t header_size; — unused */
    size_t           total_out;
    size_t           i;
    size_t           j;
    uint32_t         orig_size32;
    uint32_t         raw[256][256]; /* 256KB — on heap via VLA or local */
    int              c;

    if (!dst || !src || src_size == 0) return MCX_ERROR(MCX_ERR_GENERIC);

    tables = (cmrans_tables_t*)malloc(sizeof(cmrans_tables_t));
    if (!tables) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);

    /* Step 1: Count context frequencies */
    memset(raw, 0, sizeof(raw));
    raw[0][src[0]]++;
    for (i = 1; i < src_size; i++) {
        raw[src[i-1]][src[i]]++;
    }

    /* Step 2: Normalize each context table */
    for (c = 0; c < 256; c++) {
        uint32_t tot = 0;
        int k;
        for (k = 0; k < 256; k++) tot += raw[c][k];
        normalize_context(raw[c], tot, tables->freq[c], tables->cumfreq[c]);

        // Sanity check: ensure normalized frequencies sum to MCX_RANS_SCALE
        uint32_t sanity_sum = 0;
        for (k = 0; k < 256; k++) sanity_sum += tables->freq[c][k];
        if (sanity_sum != MCX_RANS_SCALE) {
            free(tables);
            return MCX_ERROR(MCX_ERR_GENERIC);
        }
    }

    /* Step 3: Encode backward */
    out16_cap = src_size + 1024;
    out16 = (uint16_t*)malloc(out16_cap * sizeof(uint16_t));
    if (!out16) { free(tables); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }

    out16_pos = 0;
    state     = MCX_RANS_STATE_LOWER;

    for (i = src_size; i > 0; i--) {
        uint8_t  sym  = src[i - 1];
        uint8_t  ctx  = (i >= 2) ? src[i - 2] : (uint8_t)0;
        uint16_t freq = tables->freq[ctx][sym];
        uint16_t cumf = tables->cumfreq[ctx][sym];

        /* Renormalize */
        while (state >= ((uint32_t)freq << 16)) {
            if (out16_pos >= out16_cap) {
                fprintf(stderr, "CMRANS ENCODE DST_TOO_SMALL: out16_pos %zu >= out16_cap %zu\n", out16_pos, out16_cap);
                free(out16); free(tables);
                return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
            }
            out16[out16_pos++] = (uint16_t)(state & 0xFFFF);
            state >>= 16;
        }

        state = ((state / freq) << MCX_RANS_PRECISION)
              + cumf
              + (state % freq);
    }

    /* Step 4: Write output (Compressing the 131KB context tables) */
    {
        uint32_t comp_hdr_size32;
        size_t   comp_hdr_size;
        size_t   state_offset;

        if (dst_cap < 8) {
            fprintf(stderr, "CMRANS HDR PRE DST_TOO_SMALL: dst_cap %zu < 8\n", dst_cap);
            free(out16); free(tables);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }

        /* Compress the frequency tables using the DEFAULT strategy (Level 10) */
        comp_hdr_size = mcx_compress(dst + 8, dst_cap - 8,
                                     tables->freq, 256 * 256 * 2, 10);
        if (MCX_IS_ERROR(comp_hdr_size)) {
            free(out16); free(tables);
            return comp_hdr_size;
        }
        
        if (dst_cap < 8 + comp_hdr_size + 4 + out16_pos * 2) {
            fprintf(stderr, "CMRANS HDR POST DST_TOO_SMALL: dst_cap %zu < 8 + %zu + 4 + %zu\n", dst_cap, comp_hdr_size, out16_pos * 2);
            free(out16); free(tables);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }

        orig_size32 = (uint32_t)src_size;
        memcpy(dst, &orig_size32, 4);

        comp_hdr_size32 = (uint32_t)comp_hdr_size;
        memcpy(dst + 4, &comp_hdr_size32, 4);

        state_offset = 8 + comp_hdr_size;
        memcpy(dst + state_offset, &state, 4);

        for (j = 0; j < out16_pos; j++) {
            uint16_t val = out16[out16_pos - 1 - j];
            memcpy(dst + state_offset + 4 + j * 2, &val, 2);
        }

        total_out = state_offset + 4 + out16_pos * 2;
    }

    free(out16);
    free(tables);
    return total_out;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  CM-rANS Decompression
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_cmrans_decompress(uint8_t* dst, size_t dst_cap,
                             const uint8_t* src, size_t src_size)
{
    cmrans_tables_t* tables;
    cmrans_lookup_t* lut;
    /* size_t header_size; — unused */
    uint32_t         orig_size32;
    size_t           orig_size;
    uint32_t         state;
    size_t           stream_pos;
    size_t           i;
    int              c;
    uint16_t         val;

    uint32_t         comp_hdr_size32;
    size_t           comp_hdr_size;
    size_t           dec_hdr_size;

    if (!dst || !src) return MCX_ERROR(MCX_ERR_GENERIC);

    if (src_size < 12) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);

    /* Read header */
    memcpy(&orig_size32, src, 4);
    memcpy(&comp_hdr_size32, src + 4, 4);
    orig_size = (size_t)orig_size32;
    comp_hdr_size = (size_t)comp_hdr_size32;

    if (orig_size > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    if (8 + comp_hdr_size + 4 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);

    /* Allocate tables on heap */
    tables = (cmrans_tables_t*)malloc(sizeof(cmrans_tables_t));
    lut    = (cmrans_lookup_t*)malloc(sizeof(cmrans_lookup_t));
    if (!tables || !lut) {
        free(tables); free(lut);
        return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    }

    /* Decompress frequency tables */
    dec_hdr_size = mcx_decompress(tables->freq, 256 * 256 * 2,
                                  src + 8, comp_hdr_size);
    if (MCX_IS_ERROR(dec_hdr_size) || dec_hdr_size != 256 * 256 * 2) {
        free(tables); free(lut);
        return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    }

    /* Rebuild cumfreq and lookup for each context */
    for (c = 0; c < 256; c++) {
        uint16_t cum = 0;
        uint16_t f, base;
        int      k;
        for (k = 0; k < 256; k++) {
            tables->cumfreq[c][k] = cum;
            cum += tables->freq[c][k];
        }
        /* O(1) lookup table */
        base = 0;
        for (k = 0; k < 256; k++) {
            f = tables->freq[c][k];
            if (base + f > MCX_RANS_SCALE) {
                free(tables); free(lut);
                return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            }
            {
                uint16_t p;
                for (p = 0; p < f; p++) {
                    lut->lookup[c][base + p] = (uint8_t)k;
                }
            }
            base += f;
        }
        if (base != MCX_RANS_SCALE) {
            free(tables); free(lut);
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }
    }

    /* Read initial rANS state */
    memcpy(&state, src + 8 + comp_hdr_size, 4);
    stream_pos = 8 + comp_hdr_size + 4;

    /* Decode forward */
    for (i = 0; i < orig_size; i++) {
        uint8_t  ctx  = (i == 0) ? (uint8_t)0 : dst[i - 1];
        uint32_t mask = MCX_RANS_SCALE - 1;
        uint32_t slot = state & mask;
        uint8_t  sym  = lut->lookup[ctx][slot];
        uint16_t freq = tables->freq[ctx][sym];
        uint16_t cumf = tables->cumfreq[ctx][sym];

        dst[i] = sym;

        state = (uint32_t)freq * (state >> MCX_RANS_PRECISION)
              + (state & mask)
              - cumf;

        while (state < MCX_RANS_STATE_LOWER && stream_pos + 1 < src_size) {
            memcpy(&val, src + stream_pos, 2);
            state = (state << 16) | val;
            stream_pos += 2;
        }
    }

    free(tables);
    free(lut);
    return orig_size;
}
