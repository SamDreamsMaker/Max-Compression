/**
 * @file cm.c
 * @brief Context Mixing compression engine
 * 
 * Full PAQ-style implementation with 30+ models, SSE, and adaptive mixing.
 */

#include "cm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Utility ────────────────────────────────────────────────────── */

static inline float stretch_f(float p) {
    if (p < 0.0001f) p = 0.0001f;
    if (p > 0.9999f) p = 0.9999f;
    return logf(p / (1.0f - p));
}

static inline float squash_f(float x) {
    if (x > 16.0f) return 0.9999f;
    if (x < -16.0f) return 0.0001f;
    return 1.0f / (1.0f + expf(-x));
}

static inline uint32_t hash32(uint32_t a) {
    a = (a ^ 61) ^ (a >> 16);
    a += (a << 3);
    a ^= (a >> 4);
    a *= 0x27d4eb2d;
    a ^= (a >> 15);
    return a;
}

static inline uint32_t combine_hash(uint32_t h, uint32_t v) {
    return hash32(h * 2654435761u + v);
}

/* ── State Map ──────────────────────────────────────────────────── */

static void sm_init(cm_statemap_t* sm, size_t size, int rate) {
    sm->size = size;
    sm->rate = rate;
    sm->prob = (uint16_t*)malloc(size * sizeof(uint16_t));
    for (size_t i = 0; i < size; i++)
        sm->prob[i] = CM_PROB_HALF;
}

static void sm_free(cm_statemap_t* sm) {
    if (sm->prob) { free(sm->prob); sm->prob = NULL; }
}

static inline uint16_t sm_predict(cm_statemap_t* sm, uint32_t ctx) {
    return sm->prob[ctx % sm->size];
}

static inline void sm_update(cm_statemap_t* sm, uint32_t ctx, int bit) {
    uint32_t c = ctx % sm->size;
    uint16_t p = sm->prob[c];
    if (bit)
        sm->prob[c] = p + ((CM_PROB_MAX - p) >> sm->rate);
    else
        sm->prob[c] = p - (p >> sm->rate);
}

/* ── Match Model ────────────────────────────────────────────────── */

static void match_init(cm_match_t* m, size_t size) {
    m->table_size = size;
    m->table = (uint32_t*)calloc(size, sizeof(uint32_t));
    memset(m->table, 0xFF, size * sizeof(uint32_t));
    m->match_pos = 0;
    m->match_len = 0;
    m->active = 0;
}

static void match_free(cm_match_t* m) {
    if (m->table) { free(m->table); m->table = NULL; }
}

static uint16_t match_predict(cm_match_t* m, const uint8_t* data, size_t pos, int bit_pos) {
    if (!m->active || m->match_pos >= pos)
        return CM_PROB_HALF;
    
    uint8_t match_byte = data[m->match_pos];
    int predicted_bit = (match_byte >> (7 - bit_pos)) & 1;
    
    /* Confidence scales with match length */
    int conf = (int)m->match_len;
    if (conf > 32) conf = 32;
    
    int delta = conf * CM_PROB_HALF / 40;
    return predicted_bit ? (CM_PROB_HALF + delta) : (CM_PROB_HALF - delta);
}

static void match_update_byte(cm_match_t* m, const uint8_t* data, size_t pos) {
    if (pos < 4) return;
    
    /* Continue existing match */
    if (m->active && m->match_pos < pos) {
        if (data[m->match_pos] == data[pos - 1]) {
            m->match_pos++;
            m->match_len++;
        } else {
            m->active = 0;
            m->match_len = 0;
        }
    }
    
    /* Try new match on 4-byte context */
    if (!m->active && pos >= 4) {
        uint32_t h = combine_hash(
            combine_hash((uint32_t)data[pos-4], (uint32_t)data[pos-3]),
            combine_hash((uint32_t)data[pos-2], (uint32_t)data[pos-1])
        );
        uint32_t slot = h % m->table_size;
        
        if (m->table[slot] != 0xFFFFFFFF) {
            size_t ref = m->table[slot];
            if (ref + 4 <= pos && ref >= 4 &&
                data[ref-4] == data[pos-4] && data[ref-3] == data[pos-3] &&
                data[ref-2] == data[pos-2] && data[ref-1] == data[pos-1]) {
                m->active = 1;
                m->match_pos = ref;
                m->match_len = 4;
            }
        }
        m->table[slot] = (uint32_t)pos;
    }
}

/* ── SSE (Secondary Symbol Estimation) ──────────────────────────── */

static void sse_init(cm_sse_t* sse) {
    for (int c = 0; c < 32; c++)
        for (int b = 0; b < 256; b++)
            sse->table[c][b] = (uint16_t)(CM_PROB_MAX * (b + 0.5) / 256);
}

static uint16_t sse_predict(cm_sse_t* sse, int ctx, uint16_t prob) {
    int bucket = prob * 255 / CM_PROB_MAX;
    if (bucket > 255) bucket = 255;
    return sse->table[ctx & 31][bucket];
}

static void sse_update(cm_sse_t* sse, int ctx, uint16_t prob, int bit) {
    int bucket = prob * 255 / CM_PROB_MAX;
    if (bucket > 255) bucket = 255;
    int c = ctx & 31;
    uint16_t p = sse->table[c][bucket];
    if (bit)
        sse->table[c][bucket] = p + ((CM_PROB_MAX - p) >> 5);
    else
        sse->table[c][bucket] = p - (p >> 5);
}

/* ── Mixer ──────────────────────────────────────────────────────── */

static void mixer_init(cm_mixer_t* mx, int n_models, int n_contexts, float lr) {
    mx->n_models = n_models;
    mx->n_contexts = n_contexts;
    mx->learning_rate = lr;
    mx->weights = (float*)malloc(n_contexts * n_models * sizeof(float));
    for (int i = 0; i < n_contexts * n_models; i++)
        mx->weights[i] = 1.0f / n_models;
}

static void mixer_free(cm_mixer_t* mx) {
    if (mx->weights) { free(mx->weights); mx->weights = NULL; }
}

static float mixer_predict(cm_mixer_t* mx, uint16_t* preds, int ctx) {
    int c = ctx % mx->n_contexts;
    float* w = mx->weights + c * mx->n_models;
    float sum = 0;
    for (int i = 0; i < mx->n_models; i++) {
        float p = (float)preds[i] / CM_PROB_MAX;
        sum += w[i] * stretch_f(p);
    }
    return squash_f(sum);
}

static void mixer_update(cm_mixer_t* mx, uint16_t* preds, int ctx, int bit) {
    int c = ctx % mx->n_contexts;
    float* w = mx->weights + c * mx->n_models;
    float sum = 0;
    float stretched[CM_MAX_MODELS];
    for (int i = 0; i < mx->n_models; i++) {
        stretched[i] = stretch_f((float)preds[i] / CM_PROB_MAX);
        sum += w[i] * stretched[i];
    }
    float mixed = squash_f(sum);
    float err = (float)bit - mixed;
    float lr = mx->learning_rate;
    for (int i = 0; i < mx->n_models; i++)
        w[i] += lr * err * stretched[i];
}

/* ── Range Coder ────────────────────────────────────────────────── */
/* Carry-free range coder (like LZMA/LZRC) */
/* lo and range encode the current interval [lo, lo+range) */

static void rc_enc_init(cm_rc_t* rc, uint8_t* buf, size_t cap) {
    rc->lo = 0;
    rc->hi = 0xFFFFFFFF; /* hi = range */
    rc->buf = buf;
    rc->buf_pos = 0;
    rc->buf_cap = cap;
}

static inline void rc_enc_bit(cm_rc_t* rc, int bit, uint16_t prob) {
    /* prob = P(bit=1) in [1, 4095] */
    /* hi = range. lo = low point. Interval is [lo, lo+hi) */
    uint32_t bound = (rc->hi >> CM_PROB_BITS) * (uint32_t)prob;
    if (bound == 0) bound = 1;
    if (bound >= rc->hi) bound = rc->hi - 1;
    if (bit) {
        rc->lo += bound;
        rc->hi -= bound;
    } else {
        rc->hi = bound;
    }
    /* Normalize: output bytes when range < 2^24 */
    /* Handle carry: if lo wrapped around (carry), propagate through buffer */
    if (rc->lo < bound && bit) {
        /* Carry happened */
        if (rc->buf_pos > 0) {
            size_t p = rc->buf_pos - 1;
            while (p > 0 && rc->buf[p] == 0xFF) { rc->buf[p] = 0; p--; }
            rc->buf[p]++;
        }
    }
    while (rc->hi < 0x01000000u) {
        if (rc->buf_pos < rc->buf_cap)
            rc->buf[rc->buf_pos++] = (uint8_t)(rc->lo >> 24);
        rc->lo <<= 8;
        rc->hi <<= 8;
    }
}

static size_t rc_enc_flush(cm_rc_t* rc) {
    for (int i = 0; i < 4; i++) {
        if (rc->buf_pos < rc->buf_cap)
            rc->buf[rc->buf_pos++] = (uint8_t)(rc->lo >> 24);
        rc->lo <<= 8;
    }
    return rc->buf_pos;
}

/* Range decoder */
static void rc_dec_init(cm_rc_t* rc, const uint8_t* buf, size_t size) {
    rc->lo = 0;
    rc->buf = (uint8_t*)buf;
    rc->buf_pos = 0;
    rc->buf_cap = size;
    rc->hi = 0;
    for (int i = 0; i < 4 && rc->buf_pos < rc->buf_cap; i++) {
        rc->hi = (rc->hi << 8) | rc->buf[rc->buf_pos++];
    }
}

static inline int rc_dec_bit(cm_rc_t* rc, uint16_t prob) {
    uint32_t range = rc->lo; /* We'll repurpose lo as 'range' after init */
    /* Actually, for decoder we need a different structure. */
    /* Use lo=range_lo, hi=code, buf for reading */
    /* TODO: proper decoder */
    (void)prob;
    return 0;
}

/* ── CM State Initialization ────────────────────────────────────── */

static void cm_init(cm_state_t* cm, const uint8_t* data, size_t size) {
    memset(cm, 0, sizeof(cm_state_t));
    cm->data = data;
    cm->data_size = size;
    
    /* Initialize all models */
    int m = 0;
    sm_init(&cm->models[m++], CM_O0_SIZE, 4);      /* 0: order-0 */
    sm_init(&cm->models[m++], CM_O1_SIZE, 5);       /* 1: order-1 */
    sm_init(&cm->models[m++], CM_O2_SIZE, 5);       /* 2: order-2 */
    sm_init(&cm->models[m++], CM_O3_SIZE, 5);       /* 3: order-3 */
    sm_init(&cm->models[m++], CM_O4_SIZE, 6);       /* 4: order-4 */
    sm_init(&cm->models[m++], CM_O5_SIZE, 6);       /* 5: order-5 */
    sm_init(&cm->models[m++], CM_O6_SIZE, 6);       /* 6: order-6 */
    sm_init(&cm->models[m++], CM_O8_SIZE, 7);       /* 7: order-8 */
    sm_init(&cm->models[m++], CM_WORD_SIZE, 6);     /* 8: word context */
    sm_init(&cm->models[m++], CM_WORD_SIZE, 6);     /* 9: word+prev */
    sm_init(&cm->models[m++], CM_SPARSE_SIZE, 5);   /* 10: sparse(-1,-3) */
    sm_init(&cm->models[m++], CM_SPARSE_SIZE, 5);   /* 11: sparse(-1,-4) */
    sm_init(&cm->models[m++], CM_SPARSE_SIZE, 5);   /* 12: sparse(-2,-4) */
    sm_init(&cm->models[m++], CM_O3_SIZE, 5);       /* 13: sparse(-1,-2,-4) */
    sm_init(&cm->models[m++], CM_O4_SIZE, 6);       /* 14: line context */
    sm_init(&cm->models[m++], CM_O2_SIZE, 5);       /* 15: upper nibble ctx */
    sm_init(&cm->models[m++], CM_O2_SIZE, 5);       /* 16: byte run ctx */
    sm_init(&cm->models[m++], CM_O3_SIZE, 5);       /* 17: order-1 + bit run */
    sm_init(&cm->models[m++], CM_O2_SIZE, 5);       /* 18: order-2 (alternate hash) */
    sm_init(&cm->models[m++], CM_O4_SIZE, 6);       /* 19: order-3 + sparse */
    sm_init(&cm->models[m++], CM_SPARSE_SIZE, 5);   /* 20: char class context */
    sm_init(&cm->models[m++], CM_O3_SIZE, 5);       /* 21: position mod 4 × o1 */
    sm_init(&cm->models[m++], CM_O3_SIZE, 5);       /* 22: diff context */
    sm_init(&cm->models[m++], CM_O4_SIZE, 6);       /* 23: order-3 + word */
    sm_init(&cm->models[m++], CM_SPARSE_SIZE, 5);   /* 24: interval context */
    sm_init(&cm->models[m++], CM_O2_SIZE, 5);       /* 25: 2-byte XOR context */
    sm_init(&cm->models[m++], CM_O3_SIZE, 5);       /* 26: order-2 + char class */
    sm_init(&cm->models[m++], CM_SPARSE_SIZE, 5);   /* 27: byte pair repeat */
    sm_init(&cm->models[m++], CM_O4_SIZE, 6);       /* 28: order-4 (alt hash) */
    sm_init(&cm->models[m++], CM_O3_SIZE, 5);       /* 29: gap context */
    cm->n_models = m;
    
    /* Match model */
    match_init(&cm->match, CM_MATCH_SIZE);
    
    /* SSE */
    sse_init(&cm->sse);
    
    /* Mixer: 8 mixer contexts (by order-0 of partial byte) */
    mixer_init(&cm->mixer, m + 1, 256, 0.003f); /* +1 for match model */
    
    cm->partial = 1; /* Start with bit prefix = 1 */
}

static void cm_free(cm_state_t* cm) {
    for (int i = 0; i < cm->n_models; i++)
        sm_free(&cm->models[i]);
    match_free(&cm->match);
    mixer_free(&cm->mixer);
}

/* ── Context computation ────────────────────────────────────────── */

static inline uint8_t char_class(uint8_t c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 2;
    if (c >= '0' && c <= '9') return 3;
    if (c == ' ') return 4;
    if (c == '\n' || c == '\r') return 5;
    if (c < 32) return 6;
    if (c < 128) return 7;
    return 8;
}

static void cm_compute_contexts(cm_state_t* cm) {
    uint8_t p0 = cm->prev[0], p1 = cm->prev[1], p2 = cm->prev[2], p3 = cm->prev[3];
    uint8_t p4 = cm->prev[4], p5 = cm->prev[5], p6 = cm->prev[6], p7 = cm->prev[7];
    uint8_t par = cm->partial;
    int bp = cm->bit_pos;
    
    /* Order-0: partial byte only */
    cm->ctx[0] = par;
    
    /* Order-1: prev byte + partial */
    cm->ctx[1] = (uint32_t)p0 * 256 + par;
    
    /* Order-2 */
    cm->ctx[2] = combine_hash((uint32_t)p1 << 8 | p0, par) * 8 + bp;
    
    /* Order-3 */
    cm->ctx[3] = combine_hash(combine_hash(p2, p1), (uint32_t)p0 << 8 | par);
    
    /* Order-4 */
    cm->ctx[4] = combine_hash(combine_hash(p3, p2), combine_hash(p1, (uint32_t)p0 << 8 | par));
    
    /* Order-5 */
    cm->ctx[5] = combine_hash(cm->ctx[4], p4);
    
    /* Order-6 */
    cm->ctx[6] = combine_hash(cm->ctx[5], p5);
    
    /* Order-8 */
    cm->ctx[7] = combine_hash(combine_hash(cm->ctx[6], p6), p7);
    
    /* Word context */
    cm->ctx[8] = combine_hash(cm->word_hash, par);
    
    /* Word + prev byte */
    cm->ctx[9] = combine_hash(cm->word_hash, (uint32_t)p0 << 8 | par);
    
    /* Sparse: bytes -1, -3 */
    cm->ctx[10] = combine_hash((uint32_t)p0 << 8 | p2, par);
    
    /* Sparse: bytes -1, -4 */
    cm->ctx[11] = combine_hash((uint32_t)p0 << 8 | p3, par);
    
    /* Sparse: bytes -2, -4 */
    cm->ctx[12] = combine_hash((uint32_t)p1 << 8 | p3, par);
    
    /* Sparse: bytes -1, -2, -4 */
    cm->ctx[13] = combine_hash(combine_hash(p0, p1), (uint32_t)p3 << 8 | par);
    
    /* Line context (hash since last newline) */
    cm->ctx[14] = combine_hash(cm->line_hash, par);
    
    /* Upper nibble context */
    cm->ctx[15] = ((uint32_t)(p0 >> 4) << 12) | ((uint32_t)(p1 >> 4) << 8) | par;
    
    /* Byte run context */
    cm->ctx[16] = (p0 == p1 ? 256 : 0) | par;
    
    /* Order-1 + bit run length */
    {
        int brl = 0;
        uint8_t last_bit = par & 1;
        uint8_t tmp = par >> 1;
        while (tmp > 0 && (tmp & 1) == last_bit) { brl++; tmp >>= 1; }
        cm->ctx[17] = combine_hash((uint32_t)p0 << 8 | par, brl);
    }
    
    /* Order-2 alternate hash */
    cm->ctx[18] = hash32(((uint32_t)p1 << 16) | ((uint32_t)p0 << 8) | par);
    
    /* Order-3 + sparse gap */
    cm->ctx[19] = combine_hash(combine_hash(p0, p2), (uint32_t)p4 << 8 | par);
    
    /* Char class context */
    cm->ctx[20] = ((uint32_t)char_class(p0) << 12) | 
                  ((uint32_t)char_class(p1) << 8) | par;
    
    /* Position mod 4 × order-1 */
    cm->ctx[21] = combine_hash((uint32_t)p0 << 8 | par, (uint32_t)(cm->pos & 3));
    
    /* Diff context (delta between consecutive bytes) */
    cm->ctx[22] = combine_hash((uint32_t)(uint8_t)(p0 - p1) << 8 | par, p0 >> 4);
    
    /* Order-3 + word */
    cm->ctx[23] = combine_hash(cm->ctx[3], cm->word_hash);
    
    /* Interval context (max - min of last 4 bytes) */
    {
        uint8_t mn = p0, mx = p0;
        if (p1 < mn) mn = p1; if (p1 > mx) mx = p1;
        if (p2 < mn) mn = p2; if (p2 > mx) mx = p2;
        if (p3 < mn) mn = p3; if (p3 > mx) mx = p3;
        cm->ctx[24] = ((uint32_t)(mx - mn) << 8) | par;
    }
    
    /* 2-byte XOR context */
    cm->ctx[25] = ((uint32_t)(p0 ^ p1) << 8) | par;
    
    /* Order-2 + char class */
    cm->ctx[26] = combine_hash(cm->ctx[2], char_class(p0));
    
    /* Byte pair repeat */
    cm->ctx[27] = (p0 == p2 && p1 == p3) ? (256 + par) : par;
    
    /* Order-4 alternate hash */
    cm->ctx[28] = hash32(((uint32_t)p3 << 24) | ((uint32_t)p2 << 16) | 
                         ((uint32_t)p1 << 8) | p0) ^ par;
    
    /* Gap context: bytes at -1, -2, -5 */
    cm->ctx[29] = combine_hash(combine_hash(p0, p1), (uint32_t)p4 << 8 | par);
}

/* ── Compress ───────────────────────────────────────────────────── */

size_t mcx_cm_compress(uint8_t* dst, size_t dst_cap,
                       const uint8_t* src, size_t src_size) {
    cm_state_t cm;
    cm_init(&cm, src, src_size);
    
    /* Header: original size (4 bytes) */
    if (dst_cap < 4) { cm_free(&cm); return 0; }
    dst[0] = (src_size >> 0) & 0xFF;
    dst[1] = (src_size >> 8) & 0xFF;
    dst[2] = (src_size >> 16) & 0xFF;
    dst[3] = (src_size >> 24) & 0xFF;
    
    rc_enc_init(&cm.rc, dst + 4, dst_cap - 4);
    
    uint16_t preds[CM_MAX_MODELS + 1]; /* +1 for match model */
    
    for (size_t i = 0; i < src_size; i++) {
        cm.pos = i;
        uint8_t byte = src[i];
        
        /* Update match model at byte boundary */
        match_update_byte(&cm.match, src, i);
        
        cm.partial = 1;
        
        for (int bit = 7; bit >= 0; bit--) {
            int b = (byte >> bit) & 1;
            cm.bit_pos = 7 - bit;
            
            /* Compute all contexts */
            cm_compute_contexts(&cm);
            
            /* Get predictions from all models */
            for (int m = 0; m < cm.n_models; m++)
                preds[m] = sm_predict(&cm.models[m], cm.ctx[m]);
            
            /* Match model prediction */
            preds[cm.n_models] = match_predict(&cm.match, src, i, cm.bit_pos);
            
            /* Mix predictions */
            int mixer_ctx = cm.partial; /* Use partial byte as mixer context */
            float mixed = mixer_predict(&cm.mixer, preds, mixer_ctx);
            
            /* SSE refinement */
            uint16_t mixed_prob = (uint16_t)(mixed * CM_PROB_MAX);
            if (mixed_prob < 1) mixed_prob = 1;
            if (mixed_prob > CM_PROB_MAX - 1) mixed_prob = CM_PROB_MAX - 1;
            
            int sse_ctx = cm.partial;
            uint16_t final_prob = sse_predict(&cm.sse, sse_ctx, mixed_prob);
            if (final_prob < 1) final_prob = 1;
            if (final_prob > CM_PROB_MAX - 1) final_prob = CM_PROB_MAX - 1;
            
            /* Encode bit */
            rc_enc_bit(&cm.rc, b, final_prob);
            
            /* Update all models */
            for (int m = 0; m < cm.n_models; m++)
                sm_update(&cm.models[m], cm.ctx[m], b);
            
            mixer_update(&cm.mixer, preds, mixer_ctx, b);
            sse_update(&cm.sse, sse_ctx, mixed_prob, b);
            
            cm.partial = (cm.partial << 1) | b;
        }
        
        /* Update prev bytes */
        for (int j = 7; j > 0; j--) cm.prev[j] = cm.prev[j-1];
        cm.prev[0] = byte;
        
        /* Update word/line hash */
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '\'')
            cm.word_hash = cm.word_hash * 31 + byte;
        else
            cm.word_hash = 0;
        
        if (byte == '\n' || byte == '\r')
            cm.line_hash = 0;
        else
            cm.line_hash = cm.line_hash * 31 + byte;
    }
    
    size_t comp_size = rc_enc_flush(&cm.rc) + 4;
    cm_free(&cm);
    return comp_size;
}

/* ── Decompress ─────────────────────────────────────────────────── */

size_t mcx_cm_decompress(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size) {
    if (src_size < 4) return 0;
    
    /* Read original size */
    size_t orig_size = (size_t)src[0] | ((size_t)src[1] << 8) |
                       ((size_t)src[2] << 16) | ((size_t)src[3] << 24);
    
    if (orig_size > dst_cap) return 0;
    
    cm_state_t cm;
    cm_init(&cm, dst, orig_size); /* Data will be filled as we decode */
    
    /* Initialize range decoder */
    cm.rc.buf = (uint8_t*)(src + 4);
    cm.rc.buf_cap = src_size - 4;
    cm.rc.buf_pos = 0;
    
    /* Read initial code value into lo, range into hi */
    uint32_t code = 0;
    for (int i = 0; i < 4 && cm.rc.buf_pos < cm.rc.buf_cap; i++)
        code = (code << 8) | cm.rc.buf[cm.rc.buf_pos++];
    
    cm.rc.lo = code;  /* lo = code for decoder */
    cm.rc.hi = 0xFFFFFFFF; /* hi = range */
    
    uint16_t preds[CM_MAX_MODELS + 1];
    
    for (size_t i = 0; i < orig_size; i++) {
        cm.pos = i;
        
        /* Update match model at byte boundary */
        if (i > 0) match_update_byte(&cm.match, dst, i);
        
        cm.partial = 1;
        uint8_t byte = 0;
        
        for (int bit = 7; bit >= 0; bit--) {
            cm.bit_pos = 7 - bit;
            
            /* Compute contexts */
            cm_compute_contexts(&cm);
            
            /* Get predictions */
            for (int m = 0; m < cm.n_models; m++)
                preds[m] = sm_predict(&cm.models[m], cm.ctx[m]);
            preds[cm.n_models] = match_predict(&cm.match, dst, i, cm.bit_pos);
            
            /* Mix */
            int mixer_ctx = cm.partial;
            float mixed = mixer_predict(&cm.mixer, preds, mixer_ctx);
            uint16_t mixed_prob = (uint16_t)(mixed * CM_PROB_MAX);
            if (mixed_prob < 1) mixed_prob = 1;
            if (mixed_prob > CM_PROB_MAX - 1) mixed_prob = CM_PROB_MAX - 1;
            
            int sse_ctx = cm.partial;
            uint16_t final_prob = sse_predict(&cm.sse, sse_ctx, mixed_prob);
            if (final_prob < 1) final_prob = 1;
            if (final_prob > CM_PROB_MAX - 1) final_prob = CM_PROB_MAX - 1;
            
            /* Decode bit — mirror of encoder */
            uint32_t bound = (cm.rc.hi >> CM_PROB_BITS) * final_prob;
            
            int b;
            if (cm.rc.lo >= bound) {
                b = 1;
                cm.rc.lo -= bound;
                cm.rc.hi -= bound;
            } else {
                b = 0;
                cm.rc.hi = bound;
            }
            
            /* Normalize decoder */
            while (cm.rc.hi < 0x01000000u) {
                cm.rc.lo <<= 8;
                cm.rc.hi <<= 8;
                if (cm.rc.buf_pos < cm.rc.buf_cap)
                    cm.rc.lo |= cm.rc.buf[cm.rc.buf_pos++];
            }
            
            /* Update models */
            for (int m = 0; m < cm.n_models; m++)
                sm_update(&cm.models[m], cm.ctx[m], b);
            mixer_update(&cm.mixer, preds, mixer_ctx, b);
            sse_update(&cm.sse, sse_ctx, mixed_prob, b);
            
            byte = (byte << 1) | b;
            cm.partial = (cm.partial << 1) | b;
        }
        
        dst[i] = byte;
        
        /* Update prev bytes */
        for (int j = 7; j > 0; j--) cm.prev[j] = cm.prev[j-1];
        cm.prev[0] = byte;
        
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '\'')
            cm.word_hash = cm.word_hash * 31 + byte;
        else
            cm.word_hash = 0;
        
        if (byte == '\n' || byte == '\r')
            cm.line_hash = 0;
        else
            cm.line_hash = cm.line_hash * 31 + byte;
    }
    
    cm_free(&cm);
    return orig_size;
}
