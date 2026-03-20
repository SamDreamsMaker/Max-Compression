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
#include "hc_match.h"

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
    
    /* Literal model:
     * Normal: 8 groups (prev_byte >> 5) × 256 symbols (bit-tree)
     * Matched: 8 groups (prev_byte >> 5) × 768 symbols (LZMA-style matched literal)
     *   - probs[0..255]: normal tree
     *   - probs[256..511]: matched state, match_bit=0
     *   - probs[512..767]: matched state, match_bit=1 */
    uint16_t lit_probs[8][256];          /* Normal literals */
    uint16_t match_lit_probs[8][768];    /* Matched literals (3 sub-trees) */
    
    /* Length model: choice + short(8) + choice2 + medium(8) + extra(256) */
    uint16_t len_choice;
    uint16_t len_short[8];
    uint16_t len_choice2;
    uint16_t len_medium[8];
    uint16_t len_extra[256];
    
    /* Distance model: slot tree + extra bits + alignment */
    uint16_t dist_tree[LZRC_DIST_TREE];
    uint16_t dist_spec[6][16]; /* extra bits for slots with 1-6 context-coded bits */
    uint16_t dist_align[LZRC_ALIGN_SIZE];
    
    /* Rep match length model (separate from new-match lengths) */
    uint16_t rep_len_choice;
    uint16_t rep_len_short[8];
    uint16_t rep_len_choice2;
    uint16_t rep_len_medium[8];
    uint16_t rep_len_extra[256];

    /* Rep match models */
    uint16_t is_rep;         /* 1 = rep match, 0 = new match */
    uint16_t rep_idx[4];     /* Which rep distance (binary tree: 0 vs 1-3, 1 vs 2-3, 2 vs 3) */
} LZRCModel;

static void lzrc_model_init(LZRCModel* m) {
    rc_prob_init(m->is_match, LZRC_CTX_TOTAL);
    rc_prob_init(&m->lit_probs[0][0], 8 * 256);
    rc_prob_init(&m->match_lit_probs[0][0], 8 * 768);
    rc_prob_init(&m->len_choice, 1);
    rc_prob_init(m->len_short, 8);
    rc_prob_init(&m->len_choice2, 1);
    rc_prob_init(m->len_medium, 8);
    rc_prob_init(m->len_extra, 256);
    rc_prob_init(m->dist_tree, LZRC_DIST_TREE);
    rc_prob_init(&m->dist_spec[0][0], 6 * 16);
    rc_prob_init(m->dist_align, LZRC_ALIGN_SIZE);
    rc_prob_init(&m->rep_len_choice, 1);
    rc_prob_init(m->rep_len_short, 8);
    rc_prob_init(&m->rep_len_choice2, 1);
    rc_prob_init(m->rep_len_medium, 8);
    rc_prob_init(m->rep_len_extra, 256);
    rc_prob_init(&m->is_rep, 1);
    rc_prob_init(m->rep_idx, 4);
}

/* ============================================================
 * Length coding
 * ============================================================ */

static void lzrc_enc_length_raw(RCEncoder* e,
    uint16_t* choice, uint16_t* lshort, uint16_t* choice2,
    uint16_t* lmed, uint16_t* lextra, uint32_t len) {
    len -= LZRC_MIN_MATCH;
    if (len < 8) {
        rc_enc_bit(e, choice, 0);
        rc_enc_bit(e, &lshort[0], (len >> 2) & 1);
        rc_enc_bit(e, &lshort[1 + ((len >> 2) & 1)], (len >> 1) & 1);
        int idx = 3 + ((len >> 1) & 3);
        rc_enc_bit(e, &lshort[idx], len & 1);
    } else if (len < 16) {
        rc_enc_bit(e, choice, 1);
        rc_enc_bit(e, choice2, 0);
        uint32_t rem = len - 8;
        rc_enc_bit(e, &lmed[0], (rem >> 2) & 1);
        rc_enc_bit(e, &lmed[1 + ((rem >> 2) & 1)], (rem >> 1) & 1);
        int idx = 3 + ((rem >> 1) & 3);
        rc_enc_bit(e, &lmed[idx], rem & 1);
    } else {
        rc_enc_bit(e, choice, 1);
        rc_enc_bit(e, choice2, 1);
        uint32_t rem = (len - 16 > 255) ? 255 : len - 16;
        rc_enc_byte(e, lextra, (uint8_t)rem);
    }
}

static void lzrc_enc_length(RCEncoder* e, LZRCModel* m, uint32_t len) {
    lzrc_enc_length_raw(e, &m->len_choice, m->len_short, &m->len_choice2,
                        m->len_medium, m->len_extra, len);
}

static void lzrc_enc_rep_length(RCEncoder* e, LZRCModel* m, uint32_t len) {
    lzrc_enc_length_raw(e, &m->rep_len_choice, m->rep_len_short, &m->rep_len_choice2,
                        m->rep_len_medium, m->rep_len_extra, len);
}

/* Length-to-distance context: 0=len4, 1=len5-6, 2=len7-10, 3=len11+ */
static inline int len_to_dist_ctx(uint32_t len) {
    if (len <= 4) return 0;
    if (len <= 6) return 1;
    if (len <= 10) return 2;
    return 3;
}

/* Encode rep index (0-3) as binary tree: 0 vs {1,2,3}, then 1 vs {2,3}, then 2 vs 3 */
static void lzrc_enc_rep_idx(RCEncoder* e, LZRCModel* m, int idx) {
    rc_enc_bit(e, &m->rep_idx[0], idx > 0 ? 1 : 0);
    if (idx > 0) {
        rc_enc_bit(e, &m->rep_idx[1], idx > 1 ? 1 : 0);
        if (idx > 1) {
            rc_enc_bit(e, &m->rep_idx[2], idx > 2 ? 1 : 0);
        }
    }
}

static int lzrc_dec_rep_idx(RCDecoder* d, LZRCModel* m) {
    if (!rc_dec_bit(d, &m->rep_idx[0])) return 0;
    if (!rc_dec_bit(d, &m->rep_idx[1])) return 1;
    if (!rc_dec_bit(d, &m->rep_idx[2])) return 2;
    return 3;
}

/* Update rep distance array: move used rep to front */
static void rep_update(uint32_t rep[4], uint32_t dist, int rep_idx) {
    if (rep_idx >= 0) {
        /* Move rep_idx to position 0 */
        uint32_t d = rep[rep_idx];
        for (int i = rep_idx; i > 0; i--)
            rep[i] = rep[i - 1];
        rep[0] = d;
    } else {
        /* New distance: shift all down, insert at 0 */
        rep[3] = rep[2];
        rep[2] = rep[1];
        rep[1] = rep[0];
        rep[0] = dist;
    }
}

static uint32_t lzrc_dec_length_raw(RCDecoder* d,
    uint16_t* choice, uint16_t* lshort, uint16_t* choice2,
    uint16_t* lmed, uint16_t* lextra) {
    if (!rc_dec_bit(d, choice)) {
        int bit0 = rc_dec_bit(d, &lshort[0]);
        int bit1 = rc_dec_bit(d, &lshort[1 + bit0]);
        int bit2 = rc_dec_bit(d, &lshort[3 + (bit0 << 1 | bit1)]);
        return LZRC_MIN_MATCH + (bit0 << 2 | bit1 << 1 | bit2);
    }
    if (!rc_dec_bit(d, choice2)) {
        int bit0 = rc_dec_bit(d, &lmed[0]);
        int bit1 = rc_dec_bit(d, &lmed[1 + bit0]);
        int bit2 = rc_dec_bit(d, &lmed[3 + (bit0 << 1 | bit1)]);
        return LZRC_MIN_MATCH + 8 + (bit0 << 2 | bit1 << 1 | bit2);
    }
    return LZRC_MIN_MATCH + 16 + rc_dec_byte(d, lextra);
}

static uint32_t lzrc_dec_length(RCDecoder* d, LZRCModel* m) {
    return lzrc_dec_length_raw(d, &m->len_choice, m->len_short, &m->len_choice2,
                               m->len_medium, m->len_extra);
}

static uint32_t lzrc_dec_rep_length(RCDecoder* d, LZRCModel* m) {
    return lzrc_dec_length_raw(d, &m->rep_len_choice, m->rep_len_short, &m->rep_len_choice2,
                               m->rep_len_medium, m->rep_len_extra);
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
    if (window_log < 20) window_log = 20; /* Min 1MB — BT tree wrapping issues below */
    if (window_log > 26) window_log = 26; /* 64MB max */
    
    uint32_t window_size = 1u << window_log;
    /* Scale hash table with window: min(window_log+2, 22) for good distribution */
    int hash_log = window_log + 2;
    if (hash_log > 22) hash_log = 22; /* 4M entries max (16MB) */
    if (hash_log < 16) hash_log = 16; /* 64K entries min */
    
    BTMatchFinder bt;
    if (bt_init(&bt, src, src_size, window_size, hash_log, bt_depth) != 0)
        return 0;
    
    /* Heuristic: enable 3-byte context hash (bytes 0,1,3) for structured data.
     * Detect structure by comparing byte-2 entropy vs bytes 0,1,3.
     * If byte 2 is significantly more random, ctx3 hash reduces collisions. */
    if (src_size >= 256) {
        uint8_t seen[4][256];
        memset(seen, 0, sizeof(seen));
        int uniq[4] = {0,0,0,0};
        size_t sample = (src_size < 4096) ? src_size : 4096;
        for (size_t si = 0; si + 3 < sample; si += 4) {
            for (int k = 0; k < 4; k++) {
                uint8_t b = src[si + k];
                if (!seen[k][b]) { seen[k][b] = 1; uniq[k]++; }
            }
        }
        /* If byte 2 has >50% more unique values than avg of 0,1,3 → structured */
        int avg013 = (uniq[0] + uniq[1] + uniq[3]) / 3;
        if (avg013 > 0 && uniq[2] > avg013 * 3 / 2) {
            bt_set_ctx3_hash(&bt, 1);
        }
    }
    
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
    uint32_t rep_dist[4] = {1, 1, 1, 1};  /* 4 rep distances */
    
    /* Pending match from lazy evaluation */
    BTMatch pending = {0, 0};
    int pending_rep = -1;  /* -1 = no rep, 0-3 = rep index */
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
        
        /* Check all 4 rep distances, pick longest */
        int cur_rep = -1;
        for (int r = 0; r < 4; r++) {
            if (pos < rep_dist[r]) continue;
            uint32_t rlen = 0;
            while (rlen < LZRC_MAX_MATCH && pos + rlen < src_size &&
                   src[pos + rlen] == src[pos - rep_dist[r] + rlen])
                rlen++;
            /* Prefer rep over new match if same length (rep is cheaper to encode) */
            if (rlen >= LZRC_MIN_MATCH && (int)rlen > (int)cur.length - (r == 0 ? 1 : 0)) {
                cur_rep = r;
                cur.length = rlen;
                cur.offset = rep_dist[r];
            }
        }
        
        /* Lazy evaluation: if we have a pending match, check if current is better */
        if (have_pending) {
            if (cur.length >= LZRC_MIN_MATCH && cur.length > pending.length + 1) {
                /* Current match is significantly better — emit literal for pending position,
                 * then use current match instead */
                int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
                rc_enc_bit(&enc, &model->is_match[ctx], 0);
                int lit_grp = prev >> 5;
                if (after_match && (pos - 1) >= rep_dist[0]) {
                    uint8_t match_byte = src[pos - 1 - rep_dist[0]];
                    rc_enc_matched_byte(&enc, model->match_lit_probs[lit_grp], src[pos - 1], match_byte);
                } else {
                    rc_enc_byte(&enc, model->lit_probs[lit_grp], src[pos - 1]);
                }
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
            rc_enc_bit(&enc, &model->is_rep, pending_rep >= 0 ? 1 : 0);
            if (pending_rep >= 0) {
                lzrc_enc_rep_length(&enc, model, pending.length);
                lzrc_enc_rep_idx(&enc, model, pending_rep);
            } else {
                lzrc_enc_length(&enc, model, pending.length);
                lzrc_enc_distance(&enc, model, pending.offset);
            }
            rep_update(rep_dist, pending.offset, pending_rep);
            
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
            int lit_grp = prev >> 5;
            if (after_match && pos >= rep_dist[0]) {
                uint8_t match_byte = src[pos - rep_dist[0]];
                rc_enc_matched_byte(&enc, model->match_lit_probs[lit_grp], src[pos], match_byte);
            } else {
                rc_enc_byte(&enc, model->lit_probs[lit_grp], src[pos]);
            }
            prev = src[pos];
            after_match = 0;
        }
    }
    
    /* Flush any pending match */
    if (have_pending) {
        int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
        rc_enc_bit(&enc, &model->is_match[ctx], 1);
        rc_enc_bit(&enc, &model->is_rep, pending_rep >= 0 ? 1 : 0);
        if (pending_rep >= 0) {
            lzrc_enc_rep_length(&enc, model, pending.length);
            lzrc_enc_rep_idx(&enc, model, pending_rep);
        } else {
            lzrc_enc_length(&enc, model, pending.length);
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
    uint32_t rep_dist[4] = {1, 1, 1, 1};
    size_t pos = 0;
    
    while (pos < orig_size) {
        int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
        
        if (rc_dec_bit(&dec, &model->is_match[ctx])) {
            /* Match */
            int is_rep = rc_dec_bit(&dec, &model->is_rep);
            uint32_t len;
            uint32_t dist;
            int rep_idx = -1;
            
            if (is_rep) {
                len = lzrc_dec_rep_length(&dec, model);
                rep_idx = lzrc_dec_rep_idx(&dec, model);
                dist = rep_dist[rep_idx];
            } else {
                len = lzrc_dec_length(&dec, model);
                dist = lzrc_dec_distance(&dec, model);
            }
            rep_update(rep_dist, dist, rep_idx);
            
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
            int lit_grp = prev >> 5;
            if (after_match && pos >= rep_dist[0]) {
                uint8_t match_byte = dst[pos - rep_dist[0]];
                dst[pos] = rc_dec_matched_byte(&dec, model->match_lit_probs[lit_grp], match_byte);
            } else {
                dst[pos] = rc_dec_byte(&dec, model->lit_probs[lit_grp]);
            }
            prev = dst[pos];
            pos++;
            after_match = 0;
        }
    }
    
    free(model);
    return pos;
}

/* ============================================================
 * Fast Compress (Hash Chain match finder)
 * ============================================================
 * Same encoding as mcx_lzrc_compress but uses hash chains
 * instead of binary tree. ~4x faster, ~2% worse ratio.
 */

size_t mcx_lzrc_compress_fast(uint8_t* dst, size_t dst_cap,
                               const uint8_t* src, size_t src_size,
                               int window_log, int hc_depth) {
    if (!dst || !src || src_size == 0 || dst_cap < 10) return 0;
    if (window_log < 20) window_log = 20;
    if (window_log > 26) window_log = 26;
    
    uint32_t window_size = 1u << window_log;
    int hash_log = window_log + 2;
    if (hash_log > 22) hash_log = 22;
    if (hash_log < 16) hash_log = 16;
    
    HCMatchFinder hc;
    if (hc_init(&hc, src, src_size, window_size, hash_log, hc_depth) != 0)
        return 0;
    
    LZRCModel* model = (LZRCModel*)malloc(sizeof(LZRCModel));
    if (!model) { hc_free(&hc); return 0; }
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
    uint32_t rep_dist[4] = {1, 1, 1, 1};
    
    /* Pending match from lazy evaluation */
    BTMatch pending = {0, 0};  /* BTMatch and HCMatch have same layout */
    int pending_rep = -1;
    int have_pending = 0;
    
    while (hc.pos < src_size) {
        uint32_t pos = hc.pos;
        HCMatch hc_match;
        int n = hc_find(&hc, &hc_match, 1);
        
        /* Convert HC match to BT match struct (same fields) */
        BTMatch cur = {0, 0};
        if (n > 0) {
            cur.length = hc_match.length;
            cur.offset = hc_match.offset;
        }
        
        /* Check all 4 rep distances */
        int cur_rep = -1;
        for (int r = 0; r < 4; r++) {
            if (pos < rep_dist[r]) continue;
            uint32_t rlen = 0;
            while (rlen < LZRC_MAX_MATCH && pos + rlen < src_size &&
                   src[pos + rlen] == src[pos - rep_dist[r] + rlen])
                rlen++;
            if (rlen >= LZRC_MIN_MATCH && (int)rlen > (int)cur.length - (r == 0 ? 1 : 0)) {
                cur_rep = r;
                cur.length = rlen;
                cur.offset = rep_dist[r];
            }
        }
        
        /* Lazy evaluation */
        if (have_pending) {
            if (cur.length >= LZRC_MIN_MATCH && cur.length > pending.length + 1) {
                int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
                rc_enc_bit(&enc, &model->is_match[ctx], 0);
                int lit_grp = prev >> 5;
                if (after_match && (pos - 1) >= rep_dist[0]) {
                    uint8_t match_byte = src[pos - 1 - rep_dist[0]];
                    rc_enc_matched_byte(&enc, model->match_lit_probs[lit_grp], src[pos - 1], match_byte);
                } else {
                    rc_enc_byte(&enc, model->lit_probs[lit_grp], src[pos - 1]);
                }
                prev = src[pos - 1];
                after_match = 0;
                pending = cur;
                pending_rep = cur_rep;
                have_pending = 1;
                continue;
            }
            
            /* Emit pending match */
            int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
            rc_enc_bit(&enc, &model->is_match[ctx], 1);
            rc_enc_bit(&enc, &model->is_rep, pending_rep >= 0 ? 1 : 0);
            if (pending_rep >= 0) {
                lzrc_enc_rep_length(&enc, model, pending.length);
                lzrc_enc_rep_idx(&enc, model, pending_rep);
            } else {
                lzrc_enc_length(&enc, model, pending.length);
                lzrc_enc_distance(&enc, model, pending.offset);
            }
            rep_update(rep_dist, pending.offset, pending_rep);
            
            uint32_t pending_start = pos - 1;
            if (pending.length > 2)
                hc_skip(&hc, pending.length - 2);
            
            prev = src[pending_start + pending.length - 1];
            after_match = 1;
            have_pending = 0;
            continue;
        }
        
        /* No pending match */
        if (cur.length >= LZRC_MIN_MATCH) {
            pending = cur;
            pending_rep = cur_rep;
            have_pending = 1;
        } else {
            int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
            rc_enc_bit(&enc, &model->is_match[ctx], 0);
            int lit_grp = prev >> 5;
            if (after_match && pos >= rep_dist[0]) {
                uint8_t match_byte = src[pos - rep_dist[0]];
                rc_enc_matched_byte(&enc, model->match_lit_probs[lit_grp], src[pos], match_byte);
            } else {
                rc_enc_byte(&enc, model->lit_probs[lit_grp], src[pos]);
            }
            prev = src[pos];
            after_match = 0;
        }
    }
    
    /* Flush any pending match */
    if (have_pending) {
        int ctx = after_match ? (LZRC_CTX_LIT + (prev >> 4)) : prev;
        rc_enc_bit(&enc, &model->is_match[ctx], 1);
        rc_enc_bit(&enc, &model->is_rep, pending_rep >= 0 ? 1 : 0);
        if (pending_rep >= 0) {
            lzrc_enc_rep_length(&enc, model, pending.length);
            lzrc_enc_rep_idx(&enc, model, pending_rep);
        } else {
            lzrc_enc_length(&enc, model, pending.length);
            lzrc_enc_distance(&enc, model, pending.offset);
        }
    }
    
    size_t enc_size = rc_enc_flush(&enc);
    hc_free(&hc);
    free(model);
    return 5 + enc_size;
}
