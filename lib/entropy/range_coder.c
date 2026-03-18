/**
 * @file range_coder.c
 * @brief Byte-aligned range coder — Subbotin/LZMA style.
 *
 * Uses the standard TOP/BOT normalization without carry propagation.
 * Foundation for v2.0 LZ + Context Mixer pipeline.
 */

#include "range_coder.h"
#include <string.h>

/* ── Constants ──────────────────────────────────────────────────── */

#define RC_TOP_VALUE   0x01000000U
#define RC_BOT_VALUE   0x00010000U
#define RC_PROB_BITS   11
#define RC_PROB_INIT   (1U << (RC_PROB_BITS - 1))  /* 1024 */
#define RC_PROB_MAX    (1U << RC_PROB_BITS)         /* 2048 */
#define RC_MOVE_BITS   5  /* Adaptation rate */

/* ── Encoder ────────────────────────────────────────────────────── */

void rc_enc_init(RCEncoder* e, uint8_t* dst, size_t cap) {
    e->low = 0;
    e->range = 0xFFFFFFFFU;
    e->buf = dst;
    e->pos = 0;
    e->cap = cap;
    e->ff_count = 0;
    e->cache = 0;
}

static void rc_enc_normalize(RCEncoder* e) {
    while (e->range < RC_TOP_VALUE) {
        if ((e->low & 0xFF000000U) != 0xFF000000U) {
            /* Output cache byte + carry */
            uint8_t carry = (uint8_t)(e->low >> 32);
            if (e->pos < e->cap) e->buf[e->pos++] = e->cache + carry;
            while (e->ff_count > 0) {
                if (e->pos < e->cap) e->buf[e->pos++] = 0xFF + carry;
                e->ff_count--;
            }
            e->cache = (uint8_t)(e->low >> 24);
        } else {
            e->ff_count++;
        }
        e->low = (e->low << 8) & 0xFFFFFFFFU;
        e->range <<= 8;
    }
}

void rc_enc_bit(RCEncoder* e, uint16_t* prob, int bit) {
    uint32_t bound = (e->range >> RC_PROB_BITS) * (*prob);

    if (bit == 0) {
        e->range = bound;
        *prob += (RC_PROB_MAX - *prob) >> RC_MOVE_BITS;
    } else {
        e->low += bound;
        e->range -= bound;
        *prob -= *prob >> RC_MOVE_BITS;
    }
    rc_enc_normalize(e);
}

void rc_enc_byte(RCEncoder* e, uint16_t* probs, uint8_t byte) {
    uint32_t ctx = 1;
    for (int i = 7; i >= 0; i--) {
        int bit = (byte >> i) & 1;
        rc_enc_bit(e, &probs[ctx], bit);
        ctx = (ctx << 1) | bit;
    }
}

void rc_enc_matched_byte(RCEncoder* e, uint16_t* probs, uint8_t byte, uint8_t match_byte) {
    /* LZMA-style matched literal encoding.
     * Uses match_byte bits to select between sub-trees.
     * probs needs 512 entries (indices 0x100..0x1FF used for matched context).
     * When actual bit matches predicted bit, we stay in "matched" state.
     * When they differ, we fall back to normal tree. */
    uint32_t ctx = 1;
    uint32_t match_ctx = match_byte;
    int same = 1; /* Still matching predicted bits */
    
    for (int i = 7; i >= 0; i--) {
        int bit = (byte >> i) & 1;
        int match_bit = (match_ctx >> 7) & 1;
        match_ctx <<= 1;
        
        uint32_t prob_idx;
        if (same) {
            /* In matched state: offset by 0x100 + match_bit * 0x100 */
            prob_idx = ctx + (match_bit ? 0x200 : 0x100);
        } else {
            prob_idx = ctx;
        }
        
        rc_enc_bit(e, &probs[prob_idx], bit);
        ctx = (ctx << 1) | bit;
        
        if (bit != match_bit) same = 0;
    }
}

void rc_enc_literal(RCEncoder* e, uint16_t probs[16][256],
                     uint8_t byte, uint8_t prev_byte, int after_match) {
    int ctx_idx = (prev_byte >> 4) | (after_match ? 8 : 0);
    rc_enc_byte(e, probs[ctx_idx], byte);
}

uint8_t rc_dec_matched_byte(RCDecoder* d, uint16_t* probs, uint8_t match_byte) {
    uint32_t ctx = 1;
    uint32_t match_ctx = match_byte;
    int same = 1;
    
    for (int i = 7; i >= 0; i--) {
        int match_bit = (match_ctx >> 7) & 1;
        match_ctx <<= 1;
        
        uint32_t prob_idx;
        if (same) {
            prob_idx = ctx + (match_bit ? 0x200 : 0x100);
        } else {
            prob_idx = ctx;
        }
        
        int bit = rc_dec_bit(d, &probs[prob_idx]);
        ctx = (ctx << 1) | bit;
        
        if (bit != match_bit) same = 0;
    }
    return (uint8_t)(ctx & 0xFF);
}

size_t rc_enc_flush(RCEncoder* e) {
    /* Output remaining bytes */
    for (int i = 0; i < 5; i++) {
        rc_enc_normalize(e);
        /* Force shift */
        if (e->pos < e->cap) {
            uint8_t carry = (uint8_t)(e->low >> 32);
            e->buf[e->pos++] = e->cache + carry;
            while (e->ff_count > 0) {
                if (e->pos < e->cap) e->buf[e->pos++] = 0xFF + carry;
                e->ff_count--;
            }
            e->cache = (uint8_t)(e->low >> 24);
            e->low = (e->low << 8) & 0xFFFFFFFFU;
            e->range = 0xFFFFFFFFU;
        }
    }
    return e->pos;
}

/* ── Decoder ────────────────────────────────────────────────────── */

void rc_dec_init(RCDecoder* d, const uint8_t* src, size_t size) {
    d->range = 0xFFFFFFFFU;
    d->code = 0;
    d->buf = src;
    d->pos = 0;
    d->size = size;

    for (int i = 0; i < 5; i++) {
        d->code = (d->code << 8) | (d->pos < d->size ? d->buf[d->pos++] : 0);
    }
}

static inline void rc_dec_normalize(RCDecoder* d) {
    while (d->range < RC_TOP_VALUE) {
        d->code = (d->code << 8) | (d->pos < d->size ? d->buf[d->pos++] : 0);
        d->range <<= 8;
    }
}

int rc_dec_bit(RCDecoder* d, uint16_t* prob) {
    uint32_t bound = (d->range >> RC_PROB_BITS) * (*prob);

    if (d->code < bound) {
        d->range = bound;
        *prob += (RC_PROB_MAX - *prob) >> RC_MOVE_BITS;
        rc_dec_normalize(d);
        return 0;
    } else {
        d->code -= bound;
        d->range -= bound;
        *prob -= *prob >> RC_MOVE_BITS;
        rc_dec_normalize(d);
        return 1;
    }
}

uint8_t rc_dec_byte(RCDecoder* d, uint16_t* probs) {
    uint32_t ctx = 1;
    for (int i = 0; i < 8; i++) {
        int bit = rc_dec_bit(d, &probs[ctx]);
        ctx = (ctx << 1) | bit;
    }
    return (uint8_t)(ctx & 0xFF);
}

uint8_t rc_dec_literal(RCDecoder* d, uint16_t probs[16][256],
                        uint8_t prev_byte, int after_match) {
    int ctx_idx = (prev_byte >> 4) | (after_match ? 8 : 0);
    return rc_dec_byte(d, probs[ctx_idx]);
}

/* ── Probability Model Init ────────────────────────────────────── */

void rc_prob_init(uint16_t* probs, size_t count) {
    for (size_t i = 0; i < count; i++)
        probs[i] = RC_PROB_INIT;
}
