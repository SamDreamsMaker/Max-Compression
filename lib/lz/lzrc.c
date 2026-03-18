/**
 * @file lzrc.c
 * @brief LZ + Range Coder compressor (v2.0)
 *
 * Architecture:
 * - Binary tree match finder (configurable window up to 64MB)
 * - Adaptive range coder with context-dependent models
 * - Distance slot coding (LZMA-inspired)
 * - Literal coding with match-context awareness
 *
 * Format (per block):
 *   [4 bytes: original size]
 *   [1 byte: window_log]
 *   [range-coded stream]
 *
 * Token types:
 *   bit=0: literal (context = prev_byte + after_match)
 *   bit=1: match (length + distance)
 */

#include "lzrc.h"
#include "bt_match.h"
#include "../entropy/range_coder.h"
#include "../entropy/lz_models.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Models
 * ============================================================ */

/* is_match context: 256 literal contexts + 16 post-match contexts = 272 */
#define LZRC_CTX_LIT   256
#define LZRC_CTX_MATCH 16
#define LZRC_CTX_TOTAL (LZRC_CTX_LIT + LZRC_CTX_MATCH)

/* Distance slots: 64 slots (covers up to 64MB+) */
#define LZRC_DIST_SLOTS 64
#define LZRC_DIST_SLOT_BITS 6
#define LZRC_DIST_TREE  128  /* binary tree for 6-bit slot coding */

/* Alignment bits for large distances */
#define LZRC_ALIGN_BITS 4
#define LZRC_ALIGN_SIZE (1 << LZRC_ALIGN_BITS)

/* Max match length = 4 + 8 + 8 + 256 = 276 */
#define LZRC_MIN_MATCH  4
#define LZRC_MAX_MATCH  273

typedef struct {
    /* is_match[context] — 0=literal, 1=match */
    uint16_t is_match[LZRC_CTX_TOTAL];
    
    /* Literal model: 16 groups × 256 symbols (bit-tree) */
    uint16_t lit_probs[16][256];
    
    /* Length model: choice + short(8) + choice2 + medium(8) + extra(256) */
    uint16_t len_choice;
    uint16_t len_short[8];
    uint16_t len_choice2;
    uint16_t len_medium[8];
    uint16_t len_extra[256];
    
    /* Distance model: slot tree + special bits + alignment */
    uint16_t dist_tree[LZRC_DIST_TREE];
    uint16_t dist_spec[6][16]; /* extra bits for slots with 1-6 context-coded bits */
    uint16_t dist_align[LZRC_ALIGN_SIZE];
    
    /* Rep match (last distance reuse) */
    uint16_t is_rep;
} LZRCModel;

static void lzrc_model_init(LZRCModel* m) {
    rc_prob_init(m->is_match, LZRC_CTX_TOTAL);
    rc_prob_init(&m->lit_probs[0][0], 16 * 256);
    rc_prob_init(&m->len_choice, 1);
    rc_prob_init(m->len_short, 8);
    rc_prob_init(&m->len_choice2, 1);
    rc_prob_init(m->len_medium, 8);
    rc_prob_init(m->len_extra, 256);
    rc_prob_init(m->dist_tree, LZRC_DIST_TREE);
    rc_prob_init(&m->dist_spec[0][0], 6 * 16);
    rc_prob_init(m->dist_align, LZRC_ALIGN_SIZE);
    rc_prob_init(&m->is_rep, 1);
}

/* ============================================================
 * Length coding
 * ============================================================ */

static void lzrc_enc_length(RCEncoder* e, LZRCModel* m, uint32_t len) {
    len -= LZRC_MIN_MATCH;
    if (len < 8) {
        rc_enc_bit(e, &m->len_choice, 0);
        rc_enc_bit(e, &m->len_short[0], (len >> 2) & 1);
        rc_enc_bit(e, &m->len_short[1 + ((len >> 2) & 1)], (len >> 1) & 1);
        int idx = 3 + ((len >> 1) & 3);
        rc_enc_bit(e, &m->len_short[idx], len & 1);
    } else if (len < 16) {
        rc_enc_bit(e, &m->len_choice, 1);
        rc_enc_bit(e, &m->len_choice2, 0);
        uint32_t rem = len - 8;
        rc_enc_bit(e, &m->len_medium[0], (rem >> 2) & 1);
        rc_enc_bit(e, &m->len_medium[1 + ((rem >> 2) & 1)], (rem >> 1) & 1);
        int idx = 3 + ((rem >> 1) & 3);
        rc_enc_bit(e, &m->len_medium[idx], rem & 1);
    } else {
        rc_enc_bit(e, &m->len_choice, 1);
        rc_enc_bit(e, &m->len_choice2, 1);
        uint32_t rem = (len - 16 > 255) ? 255 : len - 16;
        rc_enc_byte(e, m->len_extra, (uint8_t)rem);
    }
}

static uint32_t lzrc_dec_length(RCDecoder* d, LZRCModel* m) {
    if (!rc_dec_bit(d, &m->len_choice)) {
        int bit0 = rc_dec_bit(d, &m->len_short[0]);
        int bit1 = rc_dec_bit(d, &m->len_short[1 + bit0]);
        int bit2 = rc_dec_bit(d, &m->len_short[3 + (bit0 << 1 | bit1)]);
        return LZRC_MIN_MATCH + (bit0 << 2 | bit1 << 1 | bit2);
    }
    if (!rc_dec_bit(d, &m->len_choice2)) {
        int bit0 = rc_dec_bit(d, &m->len_medium[0]);
        int bit1 = rc_dec_bit(d, &m->len_medium[1 + bit0]);
        int bit2 = rc_dec_bit(d, &m->len_medium[3 + (bit0 << 1 | bit1)]);
        return LZRC_MIN_MATCH + 8 + (bit0 << 2 | bit1 << 1 | bit2);
    }
    return LZRC_MIN_MATCH + 16 + rc_dec_byte(d, m->len_extra);
}

/* ============================================================
 * Distance coding (slot-based, LZMA-inspired)
 * ============================================================ */

static void lzrc_enc_distance(RCEncoder* e, LZRCModel* m, uint32_t dist) {
    int slot = dist_to_slot(dist);
    
    /* Encode slot as 6-bit tree */
    int idx = 1;
    for (int bit = LZRC_DIST_SLOT_BITS - 1; bit >= 0; bit--) {
        int b = (slot >> bit) & 1;
        if (idx < LZRC_DIST_TREE)
            rc_enc_bit(e, &m->dist_tree[idx], b);
        idx = idx * 2 + b;
    }
    
    /* Encode extra value */
    uint32_t base; int extra;
    slot_to_dist(slot, &base, &extra);
    uint32_t ev = dist - base;
    
    if (extra <= 0) {
        /* Slots 0-3: no extra bits */
    } else if (extra <= 6) {
        /* Slots 4-17: context-coded bits (1-6 extra bits) */
        for (int i = extra - 1; i >= 0; i--)
            rc_enc_bit(e, &m->dist_spec[extra - 1][i], (ev >> i) & 1);
    } else {
        /* Slots 18+: direct bits (middle) + alignment bits (bottom 4) */
        int direct = extra - LZRC_ALIGN_BITS;
        for (int i = direct - 1; i >= 0; i--) {
            uint16_t half = 1024;
            rc_enc_bit(e, &half, (ev >> (i + LZRC_ALIGN_BITS)) & 1);
        }
        uint32_t align = ev & (LZRC_ALIGN_SIZE - 1);
        for (int i = LZRC_ALIGN_BITS - 1; i >= 0; i--)
            rc_enc_bit(e, &m->dist_align[i], (align >> i) & 1);
    }
}

static uint32_t lzrc_dec_distance(RCDecoder* d, LZRCModel* m) {
    /* Decode 6-bit slot */
    int slot = 0;
    int idx = 1;
    for (int bit = LZRC_DIST_SLOT_BITS - 1; bit >= 0; bit--) {
        int b = rc_dec_bit(d, &m->dist_tree[idx]);
        slot = (slot << 1) | b;
        idx = idx * 2 + b;
    }
    
    uint32_t base; int extra;
    slot_to_dist(slot, &base, &extra);
    
    if (extra <= 0) {
        return base;
    } else if (extra <= 6) {
        uint32_t ev = 0;
        for (int i = extra - 1; i >= 0; i--)
            ev |= (uint32_t)rc_dec_bit(d, &m->dist_spec[extra - 1][i]) << i;
        return base + ev;
    } else {
        int direct = extra - LZRC_ALIGN_BITS;
        uint32_t ev = 0;
        for (int i = direct - 1; i >= 0; i--) {
            uint16_t half = 1024;
            int bit = rc_dec_bit(d, &half);
            ev |= (uint32_t)bit << (i + LZRC_ALIGN_BITS);
        }
        uint32_t align = 0;
        for (int i = LZRC_ALIGN_BITS - 1; i >= 0; i--)
            align |= (uint32_t)rc_dec_bit(d, &m->dist_align[i]) << i;
        return base + ev + align;
    }
}

/* ============================================================
 * Compress
 * ============================================================ */

size_t mcx_lzrc_compress(uint8_t* dst, size_t dst_cap,
                          const uint8_t* src, size_t src_size,
                          int window_log, int bt_depth) {
    if (!dst || !src || src_size == 0 || dst_cap < 10) return 0;
    if (window_log < 10) window_log = 10;
    if (window_log > 26) window_log = 26; /* 64MB max */
    
    uint32_t window_size = 1u << window_log;
    int hash_log = (window_log < 18) ? window_log : 18; /* max 256K hash */
    
    BTMatchFinder bt;
    if (bt_init(&bt, src, src_size, window_size, hash_log, bt_depth) != 0)
        return 0;
    
    LZRCModel* model = (LZRCModel*)malloc(sizeof(LZRCModel));
    if (!model) { bt_free(&bt); return 0; }
    lzrc_model_init(model);
    
    /* Header: 4 bytes size + 1 byte window_log */
    dst[0] = src_size & 0xFF;
    dst[1] = (src_size >> 8) & 0xFF;
    dst[2] = (src_size >> 16) & 0xFF;
    dst[3] = (src_size >> 24) & 0xFF;
    dst[4] = (uint8_t)window_log;
    
    RCEncoder enc;
    rc_enc_init(&enc, dst + 5, dst_cap - 5);
    
    uint8_t prev = 0;
    int after_match = 0;
    uint32_t last_dist = 1;
    
    /* Pending match from lazy evaluation */
    BTMatch pending = {0, 0};
    int pending_rep = 0;
    int have_pending = 0;
    
    while (bt.pos < src_size) {
        uint32_t pos = bt.pos;
        BTMatch matches[8];
        int n = bt_find(&bt, matches, 8);
        
        /* Pick best match at current position */
        BTMatch cur = {0, 0};
        for (int i = 0; i < n; i++) {
            if (matches[i].length > cur.length)
                cur = matches[i];
        }
        
        /* Check rep match */
        uint32_t rep_len = 0;
        if (pos >= last_dist && pos + 4 <= src_size) {
            while (rep_len < LZRC_MAX_MATCH && pos + rep_len < src_size &&
                   src[pos + rep_len] == src[pos - last_dist + rep_len])
                rep_len++;
        }
        
        int cur_rep = 0;
        if (rep_len >= LZRC_MIN_MATCH && rep_len >= cur.length) {
            cur_rep = 1;
            cur.length = rep_len;
            cur.offset = last_dist;
        }
        
        /* Lazy evaluation: if we have a pending match, check if current is better */
        if (have_pending) {
            if (cur.length >= LZRC_MIN_MATCH && cur.length > pending.length + 1) {
                /* Current match is significantly better — emit literal for pending position,
                 * then use current match instead */
                int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
                rc_enc_bit(&enc, &model->is_match[ctx], 0);
                int lit_ctx = after_match ? 8 + (prev >> 5) : (prev >> 5);
                rc_enc_byte(&enc, model->lit_probs[lit_ctx], src[pos - 1]);
                prev = src[pos - 1];
                after_match = 0;
                
                /* Now use current match as pending for next iteration */
                pending = cur;
                pending_rep = cur_rep;
                have_pending = 1;
                continue;
            }
            
            /* Pending match is good enough — emit it */
            int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
            rc_enc_bit(&enc, &model->is_match[ctx], 1);
            rc_enc_bit(&enc, &model->is_rep, pending_rep ? 1 : 0);
            lzrc_enc_length(&enc, model, pending.length);
            if (!pending_rep) {
                lzrc_enc_distance(&enc, model, pending.offset);
                last_dist = pending.offset;
            }
            
            /* Skip remaining matched bytes (we already advanced 1 via bt_find) */
            uint32_t pending_start = pos - 1;
            if (pending.length > 2)
                bt_skip(&bt, pending.length - 2);
            
            prev = src[pending_start + pending.length - 1];
            after_match = 1;
            have_pending = 0;
            continue;
        }
        
        /* No pending match */
        if (cur.length >= LZRC_MIN_MATCH) {
            /* Found a match — defer it (lazy check at next position) */
            pending = cur;
            pending_rep = cur_rep;
            have_pending = 1;
        } else {
            /* Literal */
            int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
            rc_enc_bit(&enc, &model->is_match[ctx], 0);
            int lit_ctx = after_match ? 8 + (prev >> 5) : (prev >> 5);
            rc_enc_byte(&enc, model->lit_probs[lit_ctx], src[pos]);
            prev = src[pos];
            after_match = 0;
        }
    }
    
    /* Flush any pending match */
    if (have_pending) {
        int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
        rc_enc_bit(&enc, &model->is_match[ctx], 1);
        rc_enc_bit(&enc, &model->is_rep, pending_rep ? 1 : 0);
        lzrc_enc_length(&enc, model, pending.length);
        if (!pending_rep) {
            lzrc_enc_distance(&enc, model, pending.offset);
        }
    }
    
    size_t enc_size = rc_enc_flush(&enc);
    bt_free(&bt);
    free(model);
    return 5 + enc_size;
}

/* ============================================================
 * Decompress
 * ============================================================ */

size_t mcx_lzrc_decompress(uint8_t* dst, size_t dst_cap,
                            const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 6) return 0;
    
    uint32_t orig_size = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    /* uint8_t window_log = src[4]; — not needed for decompression */
    
    if (orig_size > dst_cap) return 0;
    
    LZRCModel* model = (LZRCModel*)malloc(sizeof(LZRCModel));
    if (!model) return 0;
    lzrc_model_init(model);
    
    RCDecoder dec;
    rc_dec_init(&dec, src + 5, src_size - 5);
    
    uint8_t prev = 0;
    int after_match = 0;
    uint32_t last_dist = 1;
    size_t pos = 0;
    
    while (pos < orig_size) {
        int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
        
        if (rc_dec_bit(&dec, &model->is_match[ctx])) {
            /* Match */
            int is_rep = rc_dec_bit(&dec, &model->is_rep);
            uint32_t len = lzrc_dec_length(&dec, model);
            uint32_t dist;
            
            if (is_rep) {
                dist = last_dist;
            } else {
                dist = lzrc_dec_distance(&dec, model);
                last_dist = dist;
            }
            
            if (dist == 0 || pos < dist || pos + len > orig_size) {
                free(model);
                return 0; /* Corrupt */
            }
            
            /* Copy match */
            for (uint32_t i = 0; i < len; i++)
                dst[pos + i] = dst[pos - dist + i];
            
            prev = dst[pos + len - 1];
            pos += len;
            after_match = 1;
        } else {
            /* Literal */
            int lit_ctx = after_match ? 8 + (prev >> 5) : (prev >> 5);
            dst[pos] = rc_dec_byte(&dec, model->lit_probs[lit_ctx]);
            prev = dst[pos];
            pos++;
            after_match = 0;
        }
    }
    
    free(model);
    return pos;
}
