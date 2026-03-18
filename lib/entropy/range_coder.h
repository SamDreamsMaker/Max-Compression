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
void rc_enc_bit(RCEncoder* e, uint16_t* prob, int bit);
void rc_enc_byte(RCEncoder* e, uint16_t* probs, uint8_t byte);
void rc_enc_literal(RCEncoder* e, uint16_t probs[16][256],
                     uint8_t byte, uint8_t prev_byte, int after_match);
size_t rc_enc_flush(RCEncoder* e);

/* ── Decoder API ────────────────────────────────────────────────── */

void rc_dec_init(RCDecoder* d, const uint8_t* src, size_t size);
int rc_dec_bit(RCDecoder* d, uint16_t* prob);
uint8_t rc_dec_byte(RCDecoder* d, uint16_t* probs);
uint8_t rc_dec_literal(RCDecoder* d, uint16_t probs[16][256],
                        uint8_t prev_byte, int after_match);

/* ── Probability Model ──────────────────────────────────────────── */

void rc_prob_init(uint16_t* probs, size_t count);

#endif /* MCX_RANGE_CODER_H */
