/**
 * @file range_coder.h
 * @brief Byte-aligned range coder with adaptive binary probability models.
 *
 * Foundation for v2.0 context-mixed compression.
 */

#ifndef MCX_RANGE_CODER_H
#define MCX_RANGE_CODER_H

#include <stdint.h>
#include <stddef.h>

/* Branch prediction hints (portable) */
#if defined(__GNUC__) || defined(__clang__)
#define MCX_LIKELY(x)   __builtin_expect(!!(x), 1)
#define MCX_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define MCX_LIKELY(x)   (x)
#define MCX_UNLIKELY(x) (x)
#endif

#ifdef _MSC_VER
#define RC_LIKELY(x)   (x)
#define RC_UNLIKELY(x) (x)
#else
#define RC_LIKELY(x)   MCX_LIKELY(x)
#define RC_UNLIKELY(x) MCX_UNLIKELY(x)
#endif

/* ── Constants ──────────────────────────────────────────────────── */
#define RC_PROB_BITS  11
#define RC_PROB_MAX   (1 << RC_PROB_BITS)
#define RC_MOVE_BITS  5
#define RC_TOP_VALUE  0x01000000U

/* ── Encoder State ──────────────────────────────────────────────── */

typedef struct {
    uint64_t low;
    uint32_t range;
    uint8_t* buf;
    size_t pos;
    size_t cap;
    uint32_t ff_count;
    uint8_t cache;
} RCEncoder;

/* ── Decoder State ──────────────────────────────────────────────── */

typedef struct {
    uint32_t range;
    uint32_t code;
    const uint8_t* buf;
    size_t pos;
    size_t size;
} RCDecoder;

/* ── Encoder API ────────────────────────────────────────────────── */

void rc_enc_init(RCEncoder* e, uint8_t* dst, size_t cap);
void rc_enc_encode(RCEncoder* e, uint32_t cum_freq, uint32_t freq, uint32_t total);
void rc_enc_matched_byte(RCEncoder* e, uint16_t* probs, uint8_t byte, uint8_t match_byte);
void rc_enc_literal(RCEncoder* e, uint16_t probs[16][256],
                     uint8_t byte, uint8_t prev_byte, int after_match);
size_t rc_enc_flush(RCEncoder* e);

/* Inline encoder hot path */
static inline void rc_enc_normalize_inline(RCEncoder* e) {
    while (RC_UNLIKELY(e->range < RC_TOP_VALUE)) {
        if ((e->low & 0xFF000000U) != 0xFF000000U) {
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

static inline void rc_enc_bit(RCEncoder* e, uint16_t* prob, int bit) {
    uint32_t bound = (e->range >> RC_PROB_BITS) * (*prob);
    if (bit == 0) {
        e->range = bound;
        *prob += (RC_PROB_MAX - *prob) >> RC_MOVE_BITS;
    } else {
        e->low += bound;
        e->range -= bound;
        *prob -= *prob >> RC_MOVE_BITS;
    }
    rc_enc_normalize_inline(e);
}

static inline void rc_enc_byte(RCEncoder* e, uint16_t* probs, uint8_t byte) {
    uint32_t ctx = 1;
    for (int i = 7; i >= 0; i--) {
        int bit = (byte >> i) & 1;
        rc_enc_bit(e, &probs[ctx], bit);
        ctx = (ctx << 1) | bit;
    }
}

/* ── Decoder API ────────────────────────────────────────────────── */

void rc_dec_init(RCDecoder* d, const uint8_t* src, size_t size);
uint8_t rc_dec_matched_byte(RCDecoder* d, uint16_t* probs, uint8_t match_byte);
uint8_t rc_dec_literal(RCDecoder* d, uint16_t probs[16][256],
                        uint8_t prev_byte, int after_match);

/* Inline hot-path decoder functions for cross-TU inlining */

static inline void rc_dec_normalize_inline(RCDecoder* d) {
    if (RC_UNLIKELY(d->range < RC_TOP_VALUE)) {
        do {
            d->range <<= 8;
            d->code = (d->code << 8) | (d->pos < d->size ? d->buf[d->pos++] : 0);
        } while (d->range < RC_TOP_VALUE);
    }
}

static inline int rc_dec_bit(RCDecoder* d, uint16_t* prob) {
    uint32_t bound = (d->range >> RC_PROB_BITS) * (*prob);
    if (d->code < bound) {
        d->range = bound;
        *prob += (RC_PROB_MAX - *prob) >> RC_MOVE_BITS;
        rc_dec_normalize_inline(d);
        return 0;
    } else {
        d->code -= bound;
        d->range -= bound;
        *prob -= *prob >> RC_MOVE_BITS;
        rc_dec_normalize_inline(d);
        return 1;
    }
}

static inline uint8_t rc_dec_byte(RCDecoder* d, uint16_t* probs) {
    uint32_t ctx = 1;
    for (int i = 0; i < 8; i++) {
        int bit = rc_dec_bit(d, &probs[ctx]);
        ctx = (ctx << 1) | bit;
    }
    return (uint8_t)(ctx & 0xFF);
}

/* ── Probability Model ──────────────────────────────────────────── */

void rc_prob_init(uint16_t* probs, size_t count);

#endif /* MCX_RANGE_CODER_H */
