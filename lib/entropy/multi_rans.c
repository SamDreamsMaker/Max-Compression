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
/* Precomputed -log2(f/MT_SCALE) × 65536 for fast integer cost.
 * Higher precision (32-bit) avoids rounding errors that affect
 * group assignments vs float log2.
 * Index 0 = can't encode (huge cost). */
static uint32_t g_cost_lut[MT_SCALE + 1];
static int g_cost_lut_ready = 0;

static void init_cost_lut(void) {
    if (g_cost_lut_ready) return;
    g_cost_lut[0] = 0xFFFFFFFF; /* can't encode */
    for (unsigned f = 1; f <= MT_SCALE; f++) {
        double bits = log2((double)MT_SCALE / f);
        g_cost_lut[f] = (uint32_t)(bits * 65536 + 0.5);
    }
    g_cost_lut_ready = 1;
}

/* Fast group cost using precomputed LUT — avoids per-byte log2() calls.
 * Uses per-symbol frequency histogram instead of scanning raw bytes.
 * n_active + active_syms[] allow skipping zero-freq symbols (50 bytes
 * in a group usually have ~20-40 unique symbols out of 256). */
/* Kept for reference; group_cost_sparse is faster */
#if 0
static uint64_t group_cost_fast(const uint16_t sym_freq[256], const uint16_t freq[256]) {
    uint64_t cost = 0;
    for (int s = 0; s < 256; s++) {
        if (sym_freq[s] == 0) continue;
        uint16_t f = freq[s];
        if (f == 0) return UINT64_MAX; /* Can't encode this symbol */
        cost += (uint64_t)sym_freq[s] * g_cost_lut[f];
    }
    return cost;
}
#endif

/* Even faster: only iterate over active symbols (passed as a list).
 * For BWT+MTF output, groups typically have 20-40 active symbols. */
static uint64_t group_cost_sparse(const uint16_t sym_freq[256], 
                                   const uint8_t* active, int n_active,
                                   const uint16_t freq[256]) {
    uint64_t cost = 0;
    for (int i = 0; i < n_active; i++) {
        int s = active[i];
        uint16_t f = freq[s];
        if (f == 0) return UINT64_MAX;
        cost += (uint64_t)sym_freq[s] * g_cost_lut[f];
    }
    return cost;
}

/* Internal: compress with a specific number of tables */
static size_t mt_compress_ntables(uint8_t* dst, size_t dst_cap,
                                   const uint8_t* src, size_t src_size,
                                   int max_tables);

/* Internal: context-based table selection (8-bit context → table) */
static size_t mt_compress_ctx(uint8_t* dst, size_t dst_cap,
                               const uint8_t* src, size_t src_size,
                               int max_tables);

/* Internal: 2-byte context hash table selection */
static size_t mt_compress_ctx2(uint8_t* dst, size_t dst_cap,
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
    
    /* Adaptive table count based on data size:
     * Each table adds ~80-130 bytes overhead (32B bitmap + varint freqs).
     * Fewer tables = less overhead but less specialization.
     *
     *   < 8KB:    try 2, 3 tables (overhead dominates at small sizes)
     *   8-64KB:   try 3, 4 tables
     *   64-200KB: try 4, 5 tables
     *   > 200KB:  try 4, 5, maybe 6 tables
     */
    int lo_tables, hi_tables;
    if (src_size < 8192) {
        lo_tables = 2; hi_tables = 3;
    } else if (src_size < 65536) {
        lo_tables = 3; hi_tables = 4;
    } else if (src_size < 200000) {
        lo_tables = 4; hi_tables = 5;
    } else {
        lo_tables = 4; hi_tables = 5;
    }
    
    size_t best_lo = mt_compress_ntables(dst, dst_cap, src, src_size, lo_tables);
    size_t best = best_lo;
    int hi_won = 0;
    
    uint8_t* alt = (uint8_t*)malloc(dst_cap);
    if (alt) {
        size_t sz_hi = mt_compress_ntables(alt, dst_cap, src, src_size, hi_tables);
        if (!mcx_is_error(sz_hi) && (mcx_is_error(best) || sz_hi < best)) {
            memcpy(dst, alt, sz_hi);
            best = sz_hi;
            hi_won = 1;
        }
        /* Try 6 tables if hi_tables won and data is large (>200KB) */
        if (hi_won && src_size > 200000) {
            size_t sz6 = mt_compress_ntables(alt, dst_cap, src, src_size, 6);
            if (!mcx_is_error(sz6) && (mcx_is_error(best) || sz6 < best)) {
                memcpy(dst, alt, sz6);
                best = sz6;
            }
        }
        /* Try 1-byte context-based table selection */
        if (src_size >= 256) {
            size_t ctx_sz = mt_compress_ctx(alt, dst_cap, src, src_size, lo_tables);
            if (!mcx_is_error(ctx_sz) && (mcx_is_error(best) || ctx_sz < best)) {
                memcpy(dst, alt, ctx_sz);
                best = ctx_sz;
            }
            /* Also try context mode with hi_tables */
            if (hi_tables != lo_tables) {
                ctx_sz = mt_compress_ctx(alt, dst_cap, src, src_size, hi_tables);
                if (!mcx_is_error(ctx_sz) && (mcx_is_error(best) || ctx_sz < best)) {
                    memcpy(dst, alt, ctx_sz);
                    best = ctx_sz;
                }
            }
        }
        /* Try 2-byte context hash table selection */
        if (src_size >= 512) {
            size_t ctx2_sz = mt_compress_ctx2(alt, dst_cap, src, src_size, lo_tables);
            if (!mcx_is_error(ctx2_sz) && (mcx_is_error(best) || ctx2_sz < best)) {
                memcpy(dst, alt, ctx2_sz);
                best = ctx2_sz;
            }
            if (hi_tables != lo_tables) {
                ctx2_sz = mt_compress_ctx2(alt, dst_cap, src, src_size, hi_tables);
                if (!mcx_is_error(ctx2_sz) && (mcx_is_error(best) || ctx2_sz < best)) {
                    memcpy(dst, alt, ctx2_sz);
                    best = ctx2_sz;
                }
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
    
    init_cost_lut();
    
    int num_groups = (int)((src_size + MT_GROUP_SIZE - 1) / MT_GROUP_SIZE);
    
    /* Step 1: Compute per-group frequency distributions.
     * Use uint16_t since max group size = 50 (fits in 16 bits).
     * This halves memory: 76K groups × 512B = 39MB vs 78MB with uint32_t. */
    uint16_t (*grp_freq)[256] = calloc(num_groups, sizeof(uint16_t[256]));
    if (!grp_freq) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    
    for (int g = 0; g < num_groups; g++) {
        size_t off = (size_t)g * MT_GROUP_SIZE;
        size_t len = (off + MT_GROUP_SIZE <= src_size) ? MT_GROUP_SIZE : (src_size - off);
        for (size_t i = 0; i < len; i++) grp_freq[g][src[off + i]]++;
    }
    
    /* Build per-group active symbol lists for sparse cost function */
    uint8_t (*grp_active)[256] = malloc(num_groups * sizeof(uint8_t[256]));
    uint8_t *grp_n_active = malloc(num_groups);
    if (!grp_active || !grp_n_active) {
        free(grp_freq); free(grp_active); free(grp_n_active);
        return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    }
    for (int g = 0; g < num_groups; g++) {
        int n = 0;
        for (int s = 0; s < 256; s++) {
            if (grp_freq[g][s]) grp_active[g][n++] = (uint8_t)s;
        }
        grp_n_active[g] = (uint8_t)n;
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
    
    /* K-means iterations: more iters help small blocks converge,
     * but large blocks (many groups) converge faster naturally. */
    int max_iters = (num_groups > 50000) ? 10 : 15;
    for (int iter = 0; iter < max_iters; iter++) {
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
        
        /* Reassign groups to closest table (sparse: only active symbols) */
        int changed = 0;
        for (int g = 0; g < num_groups; g++) {
            uint64_t best_cost = UINT64_MAX;
            int best_t = 0;
            for (int t = 0; t < n_tables; t++) {
                uint64_t c = group_cost_sparse(grp_freq[g], grp_active[g], grp_n_active[g], tables[t]);
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
    if (dst_cap < 16) { free(grp_freq); free(grp_active); free(grp_n_active); free(assign); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
    
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
            free(grp_freq); free(grp_active); free(grp_n_active); free(assign);
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
            free(grp_freq); free(grp_active); free(grp_n_active); free(assign);
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
        if (!sel_raw) { free(grp_freq); free(grp_active); free(grp_n_active); free(assign); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }
        
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
        if (!sel_comp) { free(sel_raw); free(grp_freq); free(grp_active); free(grp_n_active); free(assign); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }
        
        size_t sel_comp_sz = mcx_rans_compress(sel_comp, sel_cap, sel_raw, num_groups);
        free(sel_raw);
        
        if (mcx_is_error(sel_comp_sz) || pos + 4 + sel_comp_sz > dst_cap) {
            free(sel_comp); free(grp_freq); free(grp_active); free(grp_n_active); free(assign);
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
    if (!out16) { free(grp_freq); free(grp_active); free(grp_n_active); free(assign); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }
    
    size_t out16_pos = 0;
    uint32_t state1 = MT_STATE_LOWER;
    uint32_t state2 = MT_STATE_LOWER;
    uint32_t state3 = MT_STATE_LOWER;
    uint32_t state4 = MT_STATE_LOWER;
    
    /* Build cumfreq for all tables */
    uint16_t cumfreq[MT_MAX_TABLES][256];
    for (int t = 0; t < n_tables; t++) {
        uint16_t cum = 0;
        for (int s = 0; s < 256; s++) {
            cumfreq[t][s] = cum;
            cum += tables[t][s];
        }
    }
    
    /* Encode backwards — 4-way interleaved rANS */
    for (size_t i = src_size; i > 0; i--) {
        uint8_t sym = src[i - 1];
        int g = (int)((i - 1) / MT_GROUP_SIZE);
        int t = assign[g];
        uint16_t f = tables[t][sym];
        uint16_t cf = cumfreq[t][sym];
        
        /* 4-way interleaved rANS: pick state by position mod 4 */
        uint32_t* st;
        switch ((i - 1) & 3) {
            case 0: st = &state1; break;
            case 1: st = &state2; break;
            case 2: st = &state3; break;
            default: st = &state4; break;
        }
        
        while (*st >= ((uint32_t)f << 16)) {
            if (out16_pos >= out16_cap) { free(out16); free(grp_freq); free(grp_active); free(grp_n_active); free(assign); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
            out16[out16_pos++] = (uint16_t)(*st & 0xFFFF);
            *st >>= 16;
        }
        *st = ((*st / f) << MT_PRECISION) + cf + (*st % f);
    }
    
    /* Write 4 states + bitstream */
    size_t needed = pos + 16 + out16_pos * 2;
    if (needed > dst_cap) { free(out16); free(grp_freq); free(grp_active); free(grp_n_active); free(assign); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
    
    memcpy(dst + pos, &state1, 4); pos += 4;
    memcpy(dst + pos, &state2, 4); pos += 4;
    memcpy(dst + pos, &state3, 4); pos += 4;
    memcpy(dst + pos, &state4, 4); pos += 4;
    
    for (size_t j = 0; j < out16_pos; j++) {
        uint16_t val = out16[out16_pos - 1 - j];
        memcpy(dst + pos + j * 2, &val, 2);
    }
    pos += out16_pos * 2;
    
    free(out16);
    free(grp_freq); free(grp_active); free(grp_n_active);
    free(assign);
    return pos;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Context-based multi-table rANS (8-bit context → table selection)
 *
 *  Instead of fixed 50-byte groups with selector array, use previous byte
 *  as context to select the frequency table. 256 contexts get clustered
 *  into N tables via K-means. Header stores 256-byte context→table map.
 *
 *  Format: orig_size(4) + (n_tables|0x80)(1) + ctx_map(256) + tables + rANS data
 *  Flag: n_tables high bit (0x80) signals context mode.
 * ═══════════════════════════════════════════════════════════════════════ */

static size_t mt_compress_ctx(uint8_t* dst, size_t dst_cap,
                               const uint8_t* src, size_t src_size,
                               int max_tables)
{
    if (src_size < 256) return MCX_ERROR(MCX_ERR_GENERIC); /* too small for context mode */
    
    init_cost_lut();
    
    /* Step 1: Build per-context frequency distributions */
    uint32_t ctx_freq[256][256];
    uint32_t ctx_total[256];
    memset(ctx_freq, 0, sizeof(ctx_freq));
    memset(ctx_total, 0, sizeof(ctx_total));
    
    uint8_t prev = 0;
    for (size_t i = 0; i < src_size; i++) {
        ctx_freq[prev][src[i]]++;
        ctx_total[prev]++;
        prev = src[i];
    }
    
    /* Normalize context frequencies to MT_SCALE for cost computation */
    uint16_t ctx_norm[256][256];
    for (int c = 0; c < 256; c++) {
        uint32_t raw[256];
        for (int s = 0; s < 256; s++) raw[s] = ctx_freq[c][s];
        mt_normalize(raw, ctx_total[c], ctx_norm[c]);
    }
    
    /* Build per-context active symbol lists */
    uint8_t ctx_active[256][256];
    uint8_t ctx_n_active[256];
    for (int c = 0; c < 256; c++) {
        int n = 0;
        for (int s = 0; s < 256; s++) {
            if (ctx_freq[c][s] > 0) ctx_active[c][n++] = (uint8_t)s;
        }
        ctx_n_active[c] = (uint8_t)n;
    }
    
    /* Step 2: K-means clustering of 256 contexts into max_tables tables */
    int n_tables = max_tables;
    uint16_t tables[MT_MAX_TABLES][256];
    uint32_t table_raw[MT_MAX_TABLES][256];
    uint8_t ctx_map[256]; /* context → table assignment */
    
    /* Initial assignment: distribute contexts evenly */
    for (int c = 0; c < 256; c++) ctx_map[c] = (uint8_t)(c * n_tables / 256);
    
    for (int iter = 0; iter < 15; iter++) {
        /* Recompute table frequencies from assignments */
        memset(table_raw, 0, sizeof(table_raw));
        for (int c = 0; c < 256; c++) {
            int t = ctx_map[c];
            for (int s = 0; s < 256; s++) table_raw[t][s] += ctx_freq[c][s];
        }
        
        /* Normalize */
        for (int t = 0; t < n_tables; t++) {
            uint32_t total = 0;
            for (int s = 0; s < 256; s++) total += table_raw[t][s];
            mt_normalize(table_raw[t], total, tables[t]);
        }
        
        /* Reassign contexts to closest table */
        int changed = 0;
        for (int c = 0; c < 256; c++) {
            if (ctx_total[c] == 0) continue; /* unused context */
            uint64_t best_cost = UINT64_MAX;
            int best_t = 0;
            for (int t = 0; t < n_tables; t++) {
                uint64_t cost = group_cost_sparse(ctx_norm[c], ctx_active[c], ctx_n_active[c], tables[t]);
                if (cost < best_cost) { best_cost = cost; best_t = t; }
            }
            if (best_t != ctx_map[c]) { ctx_map[c] = (uint8_t)best_t; changed++; }
        }
        if (changed == 0) break;
    }
    
    /* Final table computation */
    memset(table_raw, 0, sizeof(table_raw));
    for (int c = 0; c < 256; c++) {
        int t = ctx_map[c];
        for (int s = 0; s < 256; s++) table_raw[t][s] += ctx_freq[c][s];
    }
    for (int t = 0; t < n_tables; t++) {
        uint32_t total = 0;
        for (int s = 0; s < 256; s++) total += table_raw[t][s];
        mt_normalize(table_raw[t], total, tables[t]);
    }
    
    /* Step 3: Encode header */
    size_t pos = 0;
    if (dst_cap < 261 + n_tables * 300) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    
    uint32_t orig32 = (uint32_t)src_size;
    memcpy(dst + pos, &orig32, 4); pos += 4;
    dst[pos++] = (uint8_t)(n_tables | 0x80); /* high bit = context mode */
    
    /* Write 256-byte context-to-table mapping */
    memcpy(dst + pos, ctx_map, 256); pos += 256;
    
    /* Write tables (same bitmap+varint format as group mode) */
    for (int t = 0; t < n_tables; t++) {
        if (pos + 32 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        uint8_t bitmap[32];
        memset(bitmap, 0, 32);
        int n_active = 0;
        for (int s = 0; s < 256; s++) {
            if (tables[t][s] > 0) { bitmap[s >> 3] |= (1 << (s & 7)); n_active++; }
        }
        memcpy(dst + pos, bitmap, 32); pos += 32;
        if (pos + n_active * 2 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        for (int s = 0; s < 256; s++) {
            if (tables[t][s] > 0) {
                uint16_t f = tables[t][s];
                if (f < 128) { dst[pos++] = (uint8_t)f; }
                else { dst[pos++] = (uint8_t)(0x80 | (f >> 8)); dst[pos++] = (uint8_t)(f & 0xFF); }
            }
        }
    }
    
    /* Step 4: Encode data with 4-way interleaved rANS, context-based table selection */
    uint16_t cumfreq[MT_MAX_TABLES][256];
    for (int t = 0; t < n_tables; t++) {
        uint16_t cum = 0;
        for (int s = 0; s < 256; s++) { cumfreq[t][s] = cum; cum += tables[t][s]; }
    }
    
    size_t out16_cap = src_size + 4096;
    uint16_t* out16 = malloc(out16_cap * sizeof(uint16_t));
    if (!out16) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    
    size_t out16_pos = 0;
    uint32_t state1 = MT_STATE_LOWER, state2 = MT_STATE_LOWER;
    uint32_t state3 = MT_STATE_LOWER, state4 = MT_STATE_LOWER;
    
    /* Encode backwards */
    for (size_t i = src_size; i > 0; i--) {
        uint8_t sym = src[i - 1];
        uint8_t ctx = (i >= 2) ? src[i - 2] : 0;
        int t = ctx_map[ctx];
        uint16_t f = tables[t][sym];
        uint16_t cf = cumfreq[t][sym];
        
        uint32_t* st;
        switch ((i - 1) & 3) {
            case 0: st = &state1; break;
            case 1: st = &state2; break;
            case 2: st = &state3; break;
            default: st = &state4; break;
        }
        
        while (*st >= ((uint32_t)f << 16)) {
            if (out16_pos >= out16_cap) { free(out16); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
            out16[out16_pos++] = (uint16_t)(*st & 0xFFFF);
            *st >>= 16;
        }
        *st = ((*st / f) << MT_PRECISION) + cf + (*st % f);
    }
    
    /* Write states + bitstream */
    size_t needed = pos + 16 + out16_pos * 2;
    if (needed > dst_cap) { free(out16); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
    
    memcpy(dst + pos, &state1, 4); pos += 4;
    memcpy(dst + pos, &state2, 4); pos += 4;
    memcpy(dst + pos, &state3, 4); pos += 4;
    memcpy(dst + pos, &state4, 4); pos += 4;
    
    for (size_t j = 0; j < out16_pos; j++) {
        uint16_t val = out16[out16_pos - 1 - j];
        memcpy(dst + pos + j * 2, &val, 2);
    }
    pos += out16_pos * 2;
    
    free(out16);
    return pos;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  2-byte context multi-table rANS (prev 2 bytes hashed to 8 bits)
 *
 *  Same as 1-byte context mode but uses hash(prev2, prev1) as context.
 *  May capture more structure in data with 2-byte patterns.
 *
 *  Format: orig_size(4) + (n_tables|0xC0)(1) + ctx_map(256) + tables + rANS data
 *  Flag: 0xC0 = both bits 7+6 set signals 2-byte context mode.
 * ═══════════════════════════════════════════════════════════════════════ */

/* Hash 2 bytes into 8-bit context index */
static inline uint8_t ctx2_hash(uint8_t b0, uint8_t b1) {
    return (uint8_t)((b0 * 31) ^ b1);
}

static size_t mt_compress_ctx2(uint8_t* dst, size_t dst_cap,
                                const uint8_t* src, size_t src_size,
                                int max_tables)
{
    if (src_size < 512) return MCX_ERROR(MCX_ERR_GENERIC); /* need enough data for 2-byte ctx */
    
    init_cost_lut();
    
    /* Step 1: Build per-context frequency distributions (2-byte hash) */
    uint32_t ctx_freq[256][256];
    uint32_t ctx_total[256];
    memset(ctx_freq, 0, sizeof(ctx_freq));
    memset(ctx_total, 0, sizeof(ctx_total));
    
    uint8_t prev2 = 0, prev1 = 0;
    for (size_t i = 0; i < src_size; i++) {
        uint8_t ctx = ctx2_hash(prev2, prev1);
        ctx_freq[ctx][src[i]]++;
        ctx_total[ctx]++;
        prev2 = prev1;
        prev1 = src[i];
    }
    
    /* Normalize context frequencies */
    uint16_t ctx_norm[256][256];
    for (int c = 0; c < 256; c++) {
        uint32_t raw[256];
        for (int s = 0; s < 256; s++) raw[s] = ctx_freq[c][s];
        mt_normalize(raw, ctx_total[c], ctx_norm[c]);
    }
    
    /* Build per-context active symbol lists */
    uint8_t ctx_active[256][256];
    uint8_t ctx_n_active[256];
    for (int c = 0; c < 256; c++) {
        int n = 0;
        for (int s = 0; s < 256; s++) {
            if (ctx_freq[c][s] > 0) ctx_active[c][n++] = (uint8_t)s;
        }
        ctx_n_active[c] = (uint8_t)n;
    }
    
    /* Step 2: K-means clustering */
    int n_tables = max_tables;
    uint16_t tables[MT_MAX_TABLES][256];
    uint32_t table_raw[MT_MAX_TABLES][256];
    uint8_t ctx_map[256];
    
    for (int c = 0; c < 256; c++) ctx_map[c] = (uint8_t)(c * n_tables / 256);
    
    for (int iter = 0; iter < 15; iter++) {
        memset(table_raw, 0, sizeof(table_raw));
        for (int c = 0; c < 256; c++) {
            int t = ctx_map[c];
            for (int s = 0; s < 256; s++) table_raw[t][s] += ctx_freq[c][s];
        }
        for (int t = 0; t < n_tables; t++) {
            uint32_t total = 0;
            for (int s = 0; s < 256; s++) total += table_raw[t][s];
            mt_normalize(table_raw[t], total, tables[t]);
        }
        int changed = 0;
        for (int c = 0; c < 256; c++) {
            if (ctx_total[c] == 0) continue;
            uint64_t best_cost = UINT64_MAX;
            int best_t = 0;
            for (int t = 0; t < n_tables; t++) {
                uint64_t cost = group_cost_sparse(ctx_norm[c], ctx_active[c], ctx_n_active[c], tables[t]);
                if (cost < best_cost) { best_cost = cost; best_t = t; }
            }
            if (best_t != ctx_map[c]) { ctx_map[c] = (uint8_t)best_t; changed++; }
        }
        if (changed == 0) break;
    }
    
    /* Final table computation */
    memset(table_raw, 0, sizeof(table_raw));
    for (int c = 0; c < 256; c++) {
        int t = ctx_map[c];
        for (int s = 0; s < 256; s++) table_raw[t][s] += ctx_freq[c][s];
    }
    for (int t = 0; t < n_tables; t++) {
        uint32_t total = 0;
        for (int s = 0; s < 256; s++) total += table_raw[t][s];
        mt_normalize(table_raw[t], total, tables[t]);
    }
    
    /* Step 3: Encode header — 0xC0 flag for 2-byte context mode */
    size_t pos = 0;
    if (dst_cap < 261 + n_tables * 300) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    
    uint32_t orig32 = (uint32_t)src_size;
    memcpy(dst + pos, &orig32, 4); pos += 4;
    dst[pos++] = (uint8_t)(n_tables | 0xC0); /* 0xC0 = 2-byte context mode */
    
    memcpy(dst + pos, ctx_map, 256); pos += 256;
    
    /* Write tables */
    for (int t = 0; t < n_tables; t++) {
        if (pos + 32 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        uint8_t bitmap[32];
        memset(bitmap, 0, 32);
        int n_active = 0;
        for (int s = 0; s < 256; s++) {
            if (tables[t][s] > 0) { bitmap[s >> 3] |= (1 << (s & 7)); n_active++; }
        }
        memcpy(dst + pos, bitmap, 32); pos += 32;
        if (pos + n_active * 2 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        for (int s = 0; s < 256; s++) {
            if (tables[t][s] > 0) {
                uint16_t f = tables[t][s];
                if (f < 128) { dst[pos++] = (uint8_t)f; }
                else { dst[pos++] = (uint8_t)(0x80 | (f >> 8)); dst[pos++] = (uint8_t)(f & 0xFF); }
            }
        }
    }
    
    /* Step 4: Encode data with 4-way interleaved rANS */
    uint16_t cumfreq[MT_MAX_TABLES][256];
    for (int t = 0; t < n_tables; t++) {
        uint16_t cum = 0;
        for (int s = 0; s < 256; s++) { cumfreq[t][s] = cum; cum += tables[t][s]; }
    }
    
    size_t out16_cap = src_size + 4096;
    uint16_t* out16 = malloc(out16_cap * sizeof(uint16_t));
    if (!out16) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    
    size_t out16_pos = 0;
    uint32_t state1 = MT_STATE_LOWER, state2 = MT_STATE_LOWER;
    uint32_t state3 = MT_STATE_LOWER, state4 = MT_STATE_LOWER;
    
    /* Encode backwards */
    for (size_t i = src_size; i > 0; i--) {
        uint8_t sym = src[i - 1];
        uint8_t p2 = (i >= 3) ? src[i - 3] : 0;
        uint8_t p1 = (i >= 2) ? src[i - 2] : 0;
        uint8_t ctx = ctx2_hash(p2, p1);
        int t = ctx_map[ctx];
        uint16_t f = tables[t][sym];
        uint16_t cf = cumfreq[t][sym];
        
        uint32_t* st;
        switch ((i - 1) & 3) {
            case 0: st = &state1; break;
            case 1: st = &state2; break;
            case 2: st = &state3; break;
            default: st = &state4; break;
        }
        
        while (*st >= ((uint32_t)f << 16)) {
            if (out16_pos >= out16_cap) { free(out16); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
            out16[out16_pos++] = (uint16_t)(*st & 0xFFFF);
            *st >>= 16;
        }
        *st = ((*st / f) << MT_PRECISION) + cf + (*st % f);
    }
    
    /* Write states + bitstream */
    size_t needed = pos + 16 + out16_pos * 2;
    if (needed > dst_cap) { free(out16); return MCX_ERROR(MCX_ERR_DST_TOO_SMALL); }
    
    memcpy(dst + pos, &state1, 4); pos += 4;
    memcpy(dst + pos, &state2, 4); pos += 4;
    memcpy(dst + pos, &state3, 4); pos += 4;
    memcpy(dst + pos, &state4, 4); pos += 4;
    
    for (size_t j = 0; j < out16_pos; j++) {
        uint16_t val = out16[out16_pos - 1 - j];
        memcpy(dst + pos + j * 2, &val, 2);
    }
    pos += out16_pos * 2;
    
    free(out16);
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
    
    int n_tables_raw = src[pos++];
    int ctx2_mode = (n_tables_raw & 0xC0) == 0xC0; /* 2-byte context */
    int ctx_mode = (n_tables_raw & 0x80) != 0;      /* 1-byte or 2-byte context */
    int n_tables = n_tables_raw & 0x3F;
    if (n_tables == 0 || n_tables > MT_MAX_TABLES) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    
    /* Context mode: read 256-byte context→table mapping instead of groups */
    uint8_t ctx_map[256];
    int num_groups = 0;
    if (ctx_mode) {
        if (pos + 256 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        memcpy(ctx_map, src + pos, 256); pos += 256;
    } else {
        uint32_t ng32;
        memcpy(&ng32, src + pos, 4); pos += 4;
        num_groups = (int)ng32;
    }
    
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
    
    /* Read selectors (group mode only — context mode uses ctx_map from header) */
    int* assign = NULL;
    if (!ctx_mode) {
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
        assign = malloc(num_groups * sizeof(int));
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
    }
    
    /* Read 4 rANS states */
    if (pos + 16 > src_size) { free(assign); return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); }
    uint32_t state1, state2, state3, state4;
    memcpy(&state1, src + pos, 4); pos += 4;
    memcpy(&state2, src + pos, 4); pos += 4;
    memcpy(&state3, src + pos, 4); pos += 4;
    memcpy(&state4, src + pos, 4); pos += 4;
    
    /* Decode forward — 4-way interleaved */
    const uint16_t* bits = (const uint16_t*)(src + pos);
    size_t bits_pos = 0;
    size_t max_bits = (src_size - pos) / 2;
    
    if (ctx2_mode) {
        /* ── 2-byte context decode: hash(prev2, prev1) to select table ── */
        size_t i = 0;
        uint16_t mask = MT_SCALE - 1;
        uint32_t* states[4] = { &state1, &state2, &state3, &state4 };
        uint8_t prev2 = 0, prev1 = 0;
        
        while (i < orig_size) {
            uint8_t ctx = (uint8_t)((prev2 * 31) ^ prev1);
            int t = ctx_map[ctx];
            uint32_t* st = states[i & 3];
            uint16_t slot = (uint16_t)(*st & mask);
            uint8_t sym = lookup[t][slot];
            *st = tables[t][sym] * (*st >> MT_PRECISION) + slot - cum[t][sym];
            if (*st < MT_STATE_LOWER && bits_pos < max_bits)
                *st = (*st << 16) | bits[bits_pos++];
            dst[i] = sym;
            prev2 = prev1;
            prev1 = sym;
            i++;
        }
    } else if (ctx_mode) {
        /* ── 1-byte context decode: use previous byte to select table ── */
        size_t i = 0;
        uint16_t mask = MT_SCALE - 1;
        uint32_t* states[4] = { &state1, &state2, &state3, &state4 };
        uint8_t prev_byte = 0;
        
        while (i < orig_size) {
            int t = ctx_map[prev_byte];
            uint32_t* st = states[i & 3];
            uint16_t slot = (uint16_t)(*st & mask);
            uint8_t sym = lookup[t][slot];
            *st = tables[t][sym] * (*st >> MT_PRECISION) + slot - cum[t][sym];
            if (*st < MT_STATE_LOWER && bits_pos < max_bits)
                *st = (*st << 16) | bits[bits_pos++];
            dst[i] = sym;
            prev_byte = sym;
            i++;
        }
    } else {
        /* ── Group-based decode (original algorithm) ── */
        int g = 0;
        int g_remaining = MT_GROUP_SIZE;
        int t = assign[0];
        
        size_t i = 0;
        uint16_t mask = MT_SCALE - 1;
        uint32_t* states[4] = { &state1, &state2, &state3, &state4 };
        
        while (i + 3 < orig_size) {
            for (int k = 0; k < 4; k++) {
                uint32_t* st = states[(i + k) & 3];
                uint16_t slot = (uint16_t)(*st & mask);
                uint8_t sym = lookup[t][slot];
                *st = tables[t][sym] * (*st >> MT_PRECISION) + slot - cum[t][sym];
                if (*st < MT_STATE_LOWER && bits_pos < max_bits)
                    *st = (*st << 16) | bits[bits_pos++];
                dst[i + k] = sym;
                if (--g_remaining == 0) { g++; g_remaining = MT_GROUP_SIZE; if (g < num_groups) t = assign[g]; }
            }
            i += 4;
        }
        
        while (i < orig_size) {
            uint32_t* st = states[i & 3];
            uint16_t slot = (uint16_t)(*st & mask);
            uint8_t sym = lookup[t][slot];
            *st = tables[t][sym] * (*st >> MT_PRECISION) + slot - cum[t][sym];
            if (*st < MT_STATE_LOWER && bits_pos < max_bits)
                *st = (*st << 16) | bits[bits_pos++];
            dst[i] = sym;
            if (--g_remaining == 0) { g++; g_remaining = MT_GROUP_SIZE; if (g < num_groups) t = assign[g]; }
            i++;
        }
    }
    
    free(assign);
    return orig_size;
}
