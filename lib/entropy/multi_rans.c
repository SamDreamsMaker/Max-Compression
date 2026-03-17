/**
 * @file multi_rans.c
 * @brief Multi-table rANS encoder — switches between N frequency tables.
 *
 * Inspired by bzip2's multi-table Huffman, but using rANS.
 * The data is divided into groups of GROUP_SIZE bytes.
 * Each group uses one of N_TABLES pre-computed frequency tables.
 * A selector array (one byte per group, rANS-coded) tells the
 * decoder which table to use for each group.
 *
 * Algorithm:
 * 1. Split data into groups of GROUP_SIZE bytes
 * 2. K-means clustering to find N_TABLES optimal frequency distributions
 * 3. Assign each group to its best-fit table
 * 4. Encode: sparse tables + selector array (rANS) + data per group (rANS with selected table)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "entropy.h"

#define MT_GROUP_SIZE  50    /* bzip2 uses 50 — optimal for most files */
#define MT_MAX_TABLES  6     /* Max tables to try (adaptive picks best of 4-5) */
#define MT_PRECISION   MCX_RANS_PRECISION   /* 14-bit — matches rANS */
#define MT_SCALE       MCX_RANS_SCALE
#define MT_STATE_LOWER MCX_RANS_STATE_LOWER

/* Normalize raw counts to sum = MT_SCALE */
static void mt_normalize(const uint32_t raw[256], uint32_t total, uint16_t freq[256]) {
    if (total == 0) { memset(freq, 0, 512); return; }
    uint32_t sum = 0;
    int max_i = 0; uint32_t max_v = 0;
    for (int i = 0; i < 256; i++) {
        if (raw[i] > 0) {
            freq[i] = (uint16_t)((uint64_t)raw[i] * MT_SCALE / total);
            if (freq[i] == 0) freq[i] = 1;
            sum += freq[i];
            if (raw[i] > max_v) { max_v = raw[i]; max_i = i; }
        } else {
            freq[i] = 0;
        }
    }
    if (sum > MT_SCALE) freq[max_i] -= (uint16_t)(sum - MT_SCALE);
    else if (sum < MT_SCALE) freq[max_i] += (uint16_t)(MT_SCALE - sum);
}

/* Compute cost of encoding a group with a given frequency table (in bits) */
static double group_cost(const uint8_t* data, size_t len, const uint16_t freq[256]) {
    double cost = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t f = freq[data[i]];
        if (f == 0) return 1e30; /* Can't encode this symbol */
        cost += log2((double)MT_SCALE / f);
    }
    return cost;
}

/* Internal: compress with a specific number of tables */
static size_t mt_compress_ntables(uint8_t* dst, size_t dst_cap,
                                   const uint8_t* src, size_t src_size,
                                   int max_tables);

size_t mcx_multi_rans_compress(uint8_t* dst, size_t dst_cap,
                                const uint8_t* src, size_t src_size)
{
    if (!dst || !src || src_size == 0) return MCX_ERROR(MCX_ERR_GENERIC);
    
    /* For very small data, fall back to regular rANS */
    if (src_size < MT_GROUP_SIZE * 4) {
        return mcx_rans_compress(dst, dst_cap, src, src_size);
    }
    
    /* Adaptive: try 4 and 5 tables, keep the smaller result.
     * For large data (>200KB), also try 6 tables — but only if 5 beat 4
     * (trend suggests more tables help, so 6 might help too). */
    size_t sz4 = mt_compress_ntables(dst, dst_cap, src, src_size, 4);
    size_t best = sz4;
    int five_won = 0;
    
    uint8_t* alt = (uint8_t*)malloc(dst_cap);
    if (alt) {
        size_t sz5 = mt_compress_ntables(alt, dst_cap, src, src_size, 5);
        if (!mcx_is_error(sz5) && (mcx_is_error(best) || sz5 < best)) {
            memcpy(dst, alt, sz5);
            best = sz5;
            five_won = 1;
        }
        /* Only try 6 tables if 5 was better than 4 AND data is large enough */
        if (five_won && src_size > 200000) {
            size_t sz6 = mt_compress_ntables(alt, dst_cap, src, src_size, 6);
            if (!mcx_is_error(sz6) && (mcx_is_error(best) || sz6 < best)) {
                memcpy(dst, alt, sz6);
                best = sz6;
            }
        }
        free(alt);
    }
    return best;
}

static size_t mt_compress_ntables(uint8_t* dst, size_t dst_cap,
                                   const uint8_t* src, size_t src_size,
                                   int max_tables)
{
    
    int num_groups = (int)((src_size + MT_GROUP_SIZE - 1) / MT_GROUP_SIZE);
    
    /* Step 1: Compute per-group frequency distributions */
    uint32_t (*grp_freq)[256] = calloc(num_groups, sizeof(uint32_t[256]));
    if (!grp_freq) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    
    for (int g = 0; g < num_groups; g++) {
        size_t off = (size_t)g * MT_GROUP_SIZE;
        size_t len = (off + MT_GROUP_SIZE <= src_size) ? MT_GROUP_SIZE : (src_size - off);
        for (size_t i = 0; i < len; i++) grp_freq[g][src[off + i]]++;
    }
    
    /* Step 2: Determine optimal number of tables via iterative refinement.
     * Start with the global distribution, then split. */
    int n_tables = max_tables;
    if (num_groups < n_tables) n_tables = num_groups;
    
    /* Initialize tables: divide groups into n_tables equal chunks */
    uint16_t tables[MT_MAX_TABLES][256];
    uint32_t table_raw[MT_MAX_TABLES][256];
    int* assign = calloc(num_groups, sizeof(int));
    
    /* Initial assignment: sequential split (each table gets a contiguous region).
     * This gives better initial centroids than round-robin because
     * contiguous regions share local distribution patterns. */
    for (int g = 0; g < num_groups; g++) assign[g] = g * n_tables / num_groups;
    
    /* K-means iterations */
    for (int iter = 0; iter < 15; iter++) {
        /* Recompute table frequencies from assignments */
        memset(table_raw, 0, sizeof(table_raw));
        for (int g = 0; g < num_groups; g++) {
            int t = assign[g];
            for (int s = 0; s < 256; s++) table_raw[t][s] += grp_freq[g][s];
        }
        
        /* Normalize */
        for (int t = 0; t < n_tables; t++) {
            uint32_t total = 0;
            for (int s = 0; s < 256; s++) total += table_raw[t][s];
            mt_normalize(table_raw[t], total, tables[t]);
        }
        
        /* Reassign groups to closest table */
        int changed = 0;
        for (int g = 0; g < num_groups; g++) {
            size_t off = (size_t)g * MT_GROUP_SIZE;
            size_t len = (off + MT_GROUP_SIZE <= src_size) ? MT_GROUP_SIZE : (src_size - off);
            
            double best_cost = 1e30;
            int best_t = 0;
            for (int t = 0; t < n_tables; t++) {
                double c = group_cost(src + off, len, tables[t]);
                if (c < best_cost) { best_cost = c; best_t = t; }
            }
            if (best_t != assign[g]) { assign[g] = best_t; changed++; }
        }
        
        if (changed == 0) break;
    }
    
    /* Final normalization */
    memset(table_raw, 0, sizeof(table_raw));
    for (int g = 0; g < num_groups; g++) {
        int t = assign[g];
        for (int s = 0; s < 256; s++) table_raw[t][s] += grp_freq[g][s];
    }
    for (int t = 0; t < n_tables; t++) {
        uint32_t total = 0;
        for (int s = 0; s < 256; s++) total += table_raw[t][s];
        mt_normalize(table_raw[t], total, tables[t]);
    }
    
    /* Step 3: Encode.
     * Header: orig_size(4) + n_tables(1) + n_groups(4) + tables(sparse) + selectors(rANS)
     * Body: rANS-encoded data using per-group table selection */
    
    size_t pos = 0;
    
    /* Header */
    if (dst_cap < 16) { free(grp_freq); free(assign); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
    
    uint32_t orig32 = (uint32_t)src_size;
    memcpy(dst + pos, &orig32, 4); pos += 4;
    dst[pos++] = (uint8_t)n_tables;
    uint32_t ng32 = (uint32_t)num_groups;
    memcpy(dst + pos, &ng32, 4); pos += 4;
    
    /* Write tables using per-table bitmap + varint freq format:
     * Per table: 32-byte bitmap + varint per active symbol.
     * Varint: if freq < 128, 1 byte; if >= 128, 2 bytes (0x80|high, low).
     * Tables are already normalized to MT_SCALE, stored losslessly. */
    for (int t = 0; t < n_tables; t++) {
        if (pos + 32 > dst_cap) {
            free(grp_freq); free(assign);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }
        
        uint8_t bitmap[32];
        memset(bitmap, 0, 32);
        int n_active = 0;
        for (int s = 0; s < 256; s++) {
            if (tables[t][s] > 0) {
                bitmap[s >> 3] |= (1 << (s & 7));
                n_active++;
            }
        }
        memcpy(dst + pos, bitmap, 32); pos += 32;
        
        if (pos + n_active * 2 > dst_cap) {
            free(grp_freq); free(assign);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }
        for (int s = 0; s < 256; s++) {
            if (tables[t][s] > 0) {
                uint16_t f = tables[t][s];
                if (f < 128) {
                    dst[pos++] = (uint8_t)f;
                } else {
                    dst[pos++] = (uint8_t)(0x80 | (f >> 8));
                    dst[pos++] = (uint8_t)(f & 0xFF);
                }
            }
        }
    }
    
    /* Write selector array — MTF encode then rANS compress */
    {
        uint8_t* sel_raw = malloc(num_groups);
        if (!sel_raw) { free(grp_freq); free(assign); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }
        
        /* MTF encode the selectors (bzip2 does this too) */
        uint8_t sel_mtf[MT_MAX_TABLES];
        for (int i = 0; i < n_tables; i++) sel_mtf[i] = (uint8_t)i;
        for (int g = 0; g < num_groups; g++) {
            uint8_t t = (uint8_t)assign[g];
            int rank;
            for (rank = 0; rank < n_tables; rank++) {
                if (sel_mtf[rank] == t) break;
            }
            sel_raw[g] = (uint8_t)rank;
            if (rank > 0) {
                memmove(sel_mtf + 1, sel_mtf, rank);
                sel_mtf[0] = t;
            }
        }
        
        /* rANS compress the MTF'd selectors */
        size_t sel_cap = num_groups + 256;
        uint8_t* sel_comp = malloc(sel_cap);
        if (!sel_comp) { free(sel_raw); free(grp_freq); free(assign); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }
        
        size_t sel_comp_sz = mcx_rans_compress(sel_comp, sel_cap, sel_raw, num_groups);
        free(sel_raw);
        
        if (mcx_is_error(sel_comp_sz) || pos + 4 + sel_comp_sz > dst_cap) {
            free(sel_comp); free(grp_freq); free(assign);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }
        
        /* Write compressed selector size + data */
        uint32_t sel_sz32 = (uint32_t)sel_comp_sz;
        memcpy(dst + pos, &sel_sz32, 4); pos += 4;
        memcpy(dst + pos, sel_comp, sel_comp_sz); pos += sel_comp_sz;
        free(sel_comp);
    }
    
    /* Step 4: Encode data — single rANS pass but switch freq table per group */
    size_t out16_cap = src_size + 4096;
    uint16_t* out16 = malloc(out16_cap * sizeof(uint16_t));
    if (!out16) { free(grp_freq); free(assign); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }
    
    size_t out16_pos = 0;
    uint32_t state1 = MT_STATE_LOWER;
    uint32_t state2 = MT_STATE_LOWER;
    
    /* Build cumfreq for all tables */
    uint16_t cumfreq[MT_MAX_TABLES][256];
    for (int t = 0; t < n_tables; t++) {
        uint16_t cum = 0;
        for (int s = 0; s < 256; s++) {
            cumfreq[t][s] = cum;
            cum += tables[t][s];
        }
    }
    
    /* Encode backwards */
    for (size_t i = src_size; i > 0; i--) {
        uint8_t sym = src[i - 1];
        int g = (int)((i - 1) / MT_GROUP_SIZE);
        int t = assign[g];
        uint16_t f = tables[t][sym];
        uint16_t cf = cumfreq[t][sym];
        
        /* Interleaved dual-state rANS */
        uint32_t* st = ((src_size - i) & 1) ? &state2 : &state1;
        
        while (*st >= ((uint32_t)f << 16)) {
            if (out16_pos >= out16_cap) { free(out16); free(grp_freq); free(assign); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
            out16[out16_pos++] = (uint16_t)(*st & 0xFFFF);
            *st >>= 16;
        }
        *st = ((*st / f) << MT_PRECISION) + cf + (*st % f);
    }
    
    /* Write states + bitstream */
    size_t needed = pos + 8 + out16_pos * 2;
    if (needed > dst_cap) { free(out16); free(grp_freq); free(assign); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
    
    memcpy(dst + pos, &state1, 4); pos += 4;
    memcpy(dst + pos, &state2, 4); pos += 4;
    
    for (size_t j = 0; j < out16_pos; j++) {
        uint16_t val = out16[out16_pos - 1 - j];
        memcpy(dst + pos + j * 2, &val, 2);
    }
    pos += out16_pos * 2;
    
    free(out16);
    free(grp_freq);
    free(assign);
    return pos;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Multi-table rANS Decompression
 * ═══════════════════════════════════════════════════════════════════════ */

extern size_t mcx_rans_decompress(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_size);

size_t mcx_multi_rans_decompress(uint8_t* dst, size_t dst_cap,
                                  const uint8_t* src, size_t src_size)
{
    if (!dst || !src || src_size < 9) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    
    size_t pos = 0;
    
    /* Read header */
    uint32_t orig32;
    memcpy(&orig32, src + pos, 4); pos += 4;
    size_t orig_size = orig32;
    
    if (orig_size > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    
    /* For very small data, fall back to regular rANS */
    if (orig_size < MT_GROUP_SIZE * 4) {
        return mcx_rans_decompress(dst, dst_cap, src, src_size);
    }
    
    int n_tables = src[pos++];
    if (n_tables == 0 || n_tables > MT_MAX_TABLES) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    
    uint32_t ng32;
    memcpy(&ng32, src + pos, 4); pos += 4;
    int num_groups = (int)ng32;
    
    /* Read sparse tables */
    uint16_t tables[MT_MAX_TABLES][256];
    uint16_t cum[MT_MAX_TABLES][256];
    uint8_t  lookup[MT_MAX_TABLES][MT_SCALE];
    
    memset(tables, 0, sizeof(tables));
    
    for (int t = 0; t < n_tables; t++) {
        if (pos + 32 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        
        uint8_t bitmap[32];
        memcpy(bitmap, src + pos, 32); pos += 32;
        
        /* Read varint freq: if high bit set, 2 bytes (0x80|high, low); else 1 byte */
        for (int s = 0; s < 256; s++) {
            if (bitmap[s >> 3] & (1 << (s & 7))) {
                if (pos >= src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
                uint8_t first = src[pos++];
                if (first & 0x80) {
                    if (pos >= src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
                    tables[t][s] = ((uint16_t)(first & 0x7F) << 8) | src[pos++];
                } else {
                    tables[t][s] = first;
                }
            }
        }
    }
    
    /* Re-normalize each table to MT_SCALE */
    for (int t = 0; t < n_tables; t++) {
        {
            uint32_t sum = 0;
            int max_i = -1; uint32_t max_v = 0;
            for (int s = 0; s < 256; s++) {
                sum += tables[t][s];
                if (tables[t][s] > max_v) { max_v = tables[t][s]; max_i = s; }
            }
            if (sum > 0 && sum != MT_SCALE) {
                uint32_t new_sum = 0;
                for (int s = 0; s < 256; s++) {
                    if (tables[t][s] > 0) {
                        tables[t][s] = (uint16_t)((uint64_t)tables[t][s] * MT_SCALE / sum);
                        if (tables[t][s] == 0) tables[t][s] = 1;
                        new_sum += tables[t][s];
                    }
                }
                if (new_sum > MT_SCALE && max_i >= 0) tables[t][max_i] -= (uint16_t)(new_sum - MT_SCALE);
                else if (new_sum < MT_SCALE && max_i >= 0) tables[t][max_i] += (uint16_t)(MT_SCALE - new_sum);
            }
        }
        
        /* Build cumfreq + lookup */
        uint16_t c = 0;
        for (int s = 0; s < 256; s++) {
            cum[t][s] = c;
            for (uint16_t j = 0; j < tables[t][s]; j++) {
                lookup[t][c + j] = (uint8_t)s;
            }
            c += tables[t][s];
        }
    }
    
    /* Read compressed selectors */
    if (pos + 4 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    uint32_t sel_comp_sz;
    memcpy(&sel_comp_sz, src + pos, 4); pos += 4;
    
    if (pos + sel_comp_sz > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    
    uint8_t* sel_raw = malloc(num_groups + 256);
    if (!sel_raw) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    
    size_t sel_dec = mcx_rans_decompress(sel_raw, num_groups + 256, src + pos, sel_comp_sz);
    pos += sel_comp_sz;
    
    if (mcx_is_error(sel_dec) || sel_dec != (size_t)num_groups) {
        free(sel_raw);
        return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    }
    
    /* MTF decode selectors */
    int* assign = malloc(num_groups * sizeof(int));
    if (!assign) { free(sel_raw); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }
    
    {
        uint8_t sel_mtf[MT_MAX_TABLES];
        for (int i = 0; i < n_tables; i++) sel_mtf[i] = (uint8_t)i;
        for (int g = 0; g < num_groups; g++) {
            int rank = sel_raw[g];
            if (rank >= n_tables) { free(sel_raw); free(assign); return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); }
            assign[g] = sel_mtf[rank];
            if (rank > 0) {
                uint8_t tmp = sel_mtf[rank];
                memmove(sel_mtf + 1, sel_mtf, rank);
                sel_mtf[0] = tmp;
            }
        }
    }
    free(sel_raw);
    
    /* Read rANS states */
    if (pos + 8 > src_size) { free(assign); return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); }
    uint32_t state1, state2;
    memcpy(&state1, src + pos, 4); pos += 4;
    memcpy(&state2, src + pos, 4); pos += 4;
    
    /* Decode forward */
    const uint16_t* bits = (const uint16_t*)(src + pos);
    size_t bits_pos = 0;
    size_t max_bits = (src_size - pos) / 2;
    
    for (size_t i = 0; i < orig_size; i++) {
        int g = (int)(i / MT_GROUP_SIZE);
        int t = assign[g];
        
        /* Select state: must mirror encoder's ((src_size - i) & 1) pattern.
         * Encoder at position i uses state ((src_size - i) & 1).
         * For decode position i, the matching encoder position was also i,
         * so use ((orig_size - 1 - i) & 1) to get the same state. */
        uint32_t* st = ((orig_size - 1 - i) & 1) ? &state2 : &state1;
        
        uint16_t slot = (uint16_t)(*st & (MT_SCALE - 1));
        uint8_t sym = lookup[t][slot];
        uint16_t f = tables[t][sym];
        uint16_t cf = cum[t][sym];
        
        /* Advance state */
        *st = f * (*st >> MT_PRECISION) + slot - cf;
        
        /* Renormalize */
        while (*st < MT_STATE_LOWER && bits_pos < max_bits) {
            *st = (*st << 16) | bits[bits_pos++];
        }
        
        dst[i] = sym;
    }
    
    free(assign);
    return orig_size;
}
