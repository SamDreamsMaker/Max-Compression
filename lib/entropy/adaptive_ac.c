/**
 * @file adaptive_ac.c
 * @brief Adaptive Arithmetic Coder — Order-1 context, zero table overhead.
 *
 * Range coder based on the well-tested pattern from Arturo Campos / Dmitry Subbotin.
 * Uses bottom-up byte-aligned range coder.
 *
 * Format: [4: orig_size LE] [encoded bytes...]
 */

#include "adaptive_ac.h"
#include <string.h>
#include <stdlib.h>

/* ── Adaptive Model ────────────────────────────────────────────── */

#define NUM_CTX     256
#define NUM_SYM     256
#define MAX_TOTAL   (1 << 14)

typedef struct {
    uint16_t freq[NUM_SYM];
    uint32_t total;
} CtxModel;

typedef struct {
    CtxModel c[NUM_CTX];
} Model;

static void model_init(Model* m) {
    for (int c = 0; c < NUM_CTX; c++) {
        for (int s = 0; s < NUM_SYM; s++) m->c[c].freq[s] = 1;
        m->c[c].total = NUM_SYM;
    }
}

static void model_update(Model* m, int ctx, int sym) {
    CtxModel* c = &m->c[ctx];
    c->freq[sym] += 4;
    c->total += 4;
    if (c->total >= MAX_TOTAL) {
        c->total = 0;
        for (int i = 0; i < NUM_SYM; i++) {
            c->freq[i] = (c->freq[i] >> 1) | 1;
            c->total += c->freq[i];
        }
    }
}

/* ── Byte buffer for output ────────────────────────────────────── */

typedef struct {
    uint8_t* buf;
    size_t pos, cap;
} ByteBuf;

static inline void bb_put(ByteBuf* b, uint8_t v) {
    if (b->pos < b->cap) b->buf[b->pos++] = v;
}

/* ── Range Encoder ─────────────────────────────────────────────── 
 * 
 * Uses the "shift-out" approach: whenever the top byte of low and
 * low+range agree, output it and shift both left by 8 bits.
 * When range gets too small, force output and expand.
 *
 * This is a carry-propagation-free approach using the BOT/TOP method:
 * - TOP = 1<<24, BOT = 1<<16
 * - Normalize when range < BOT by:
 *   - If (low ^ (low + range)) >= TOP → output top byte, shift
 *   - Else range = -low & (BOT-1), output top byte, shift  
 */

#define TOP (1u << 24)
#define BOT (1u << 16)

typedef struct {
    uint32_t low, range;
    ByteBuf out;
} Encoder;

static void enc_init(Encoder* e, uint8_t* dst, size_t cap) {
    e->low = 0;
    e->range = 0xFFFFFFFFu;
    e->out.buf = dst;
    e->out.pos = 0;
    e->out.cap = cap;
}

static void enc_normalize(Encoder* e) {
    while (e->range < BOT) {
        if ((e->low ^ (e->low + e->range)) >= TOP) {
            /* Reduce range to force convergence */
            e->range = (uint32_t)(-(int32_t)e->low) & (BOT - 1);
        }
        bb_put(&e->out, (uint8_t)(e->low >> 24));
        e->low <<= 8;
        e->range <<= 8;
    }
}

static void enc_encode(Encoder* e, uint32_t cumfreq, uint32_t freq, uint32_t total) {
    e->range /= total;
    e->low += cumfreq * e->range;
    e->range *= freq;
    enc_normalize(e);
}

static void enc_flush(Encoder* e) {
    /* Force all remaining bytes out */
    for (int i = 0; i < 4; i++) {
        bb_put(&e->out, (uint8_t)(e->low >> 24));
        e->low <<= 8;
    }
}

/* ── Range Decoder ─────────────────────────────────────────────── */

typedef struct {
    uint32_t low, range, code;
    const uint8_t* src;
    size_t pos, size;
} Decoder;

static inline uint8_t dec_byte(Decoder* d) {
    return d->pos < d->size ? d->src[d->pos++] : 0;
}

static void dec_init(Decoder* d, const uint8_t* src, size_t size) {
    d->low = 0;
    d->range = 0xFFFFFFFFu;
    d->src = src;
    d->pos = 0;
    d->size = size;
    d->code = 0;
    for (int i = 0; i < 4; i++)
        d->code = (d->code << 8) | dec_byte(d);
}

static void dec_normalize(Decoder* d) {
    while (d->range < BOT) {
        if ((d->low ^ (d->low + d->range)) >= TOP) {
            d->range = (uint32_t)(-(int32_t)d->low) & (BOT - 1);
        }
        d->code = (d->code << 8) | dec_byte(d);
        d->low <<= 8;
        d->range <<= 8;
    }
}

static uint32_t dec_getfreq(Decoder* d, uint32_t total) {
    d->range /= total;
    uint32_t tmp = (d->code - d->low) / d->range;
    return tmp < total ? tmp : total - 1;
}

static void dec_update(Decoder* d, uint32_t cumfreq, uint32_t freq, uint32_t total) {
    d->low += cumfreq * d->range;
    d->range *= freq;
    dec_normalize(d);
}

/* ── Encode/Decode symbol ─────────────────────────────────────── */

static void encode_symbol(Encoder* e, Model* m, int ctx, int sym) {
    CtxModel* c = &m->c[ctx];
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += c->freq[i];
    enc_encode(e, cum, c->freq[sym], c->total);
    model_update(m, ctx, sym);
}

static int decode_symbol(Decoder* d, Model* m, int ctx) {
    CtxModel* c = &m->c[ctx];
    uint32_t target = dec_getfreq(d, c->total);
    
    uint32_t cum = 0;
    int sym = 0;
    for (int i = 0; i < NUM_SYM; i++) {
        if (cum + c->freq[i] > target) { sym = i; break; }
        cum += c->freq[i];
    }
    
    dec_update(d, cum, c->freq[sym], c->total);
    model_update(m, ctx, sym);
    return sym;
}

/* ── Public API ────────────────────────────────────────────────── */

size_t mcx_adaptive_ac_compress(uint8_t* dst, size_t dst_cap,
                                 const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size == 0 || dst_cap < 8) return 0;
    
    Model* model = (Model*)malloc(sizeof(Model));
    if (!model) return 0;
    model_init(model);
    
    /* Header */
    dst[0] = (uint8_t)(src_size);
    dst[1] = (uint8_t)(src_size >> 8);
    dst[2] = (uint8_t)(src_size >> 16);
    dst[3] = (uint8_t)(src_size >> 24);
    
    Encoder enc;
    enc_init(&enc, dst + 4, dst_cap - 4);
    
    int prev = 0;
    for (size_t i = 0; i < src_size; i++) {
        encode_symbol(&enc, model, prev, src[i]);
        prev = src[i];
    }
    enc_flush(&enc);
    
    free(model);
    return 4 + enc.out.pos;
}

size_t mcx_adaptive_ac_decompress(uint8_t* dst, size_t dst_cap,
                                   const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 8) return 0;
    
    uint32_t orig_size = (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
                         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
    if (dst_cap < orig_size || orig_size == 0) return 0;
    
    Model* model = (Model*)malloc(sizeof(Model));
    if (!model) return 0;
    model_init(model);
    
    Decoder dec;
    dec_init(&dec, src + 4, src_size - 4);
    
    int prev = 0;
    for (size_t i = 0; i < orig_size; i++) {
        dst[i] = (uint8_t)decode_symbol(&dec, model, prev);
        prev = dst[i];
    }
    
    free(model);
    return orig_size;
}
