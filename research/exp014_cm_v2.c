/**
 * Exp 014: CM v2 — Context Mixing with proven LZMA-style RC
 * 
 * Uses the exact same range coder pattern as MCX's LZRC (proven, tested).
 * Probabilities represent P(bit=0) to match LZMA convention.
 * 
 * 9 models: order-0,1,2,3,4,6, word, sparse, match
 * Logistic mixer with 256 context-dependent weight sets
 * SSE refinement
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

/* ── Range Coder — exact copy from MCX range_coder.h ───────────── */
/* prob = P(bit=0), bit=0 → range=bound, bit=1 → lo+=bound        */

#define RC_TOP   0x01000000U
#define PROB_BITS 12
#define PROB_MAX  (1 << PROB_BITS)   /* 4096 */
#define PROB_HALF (PROB_MAX / 2)
#define MOVE_BITS 5

/* Encoder — exact MCX pattern */
typedef struct {
    uint64_t low;
    uint32_t range;
    uint8_t *buf;
    size_t pos, cap;
    uint32_t ff_count;
    uint8_t cache;
} rcenc_t;

static void rcenc_init(rcenc_t *e, uint8_t *buf, size_t cap) {
    e->low = 0; e->range = 0xFFFFFFFF;
    e->buf = buf; e->pos = 0; e->cap = cap;
    e->ff_count = 0; e->cache = 0;
}

static inline void rcenc_normalize(rcenc_t *e) {
    while (e->range < RC_TOP) {
        if ((e->low & 0xFF000000U) != 0xFF000000U) {
            uint8_t carry = (uint8_t)(e->low >> 32);
            if (e->pos < e->cap) e->buf[e->pos++] = e->cache + carry;
            while (e->ff_count > 0) {
                if (e->pos < e->cap) e->buf[e->pos++] = 0xFF + carry;
                e->ff_count--;
            }
            e->cache = (uint8_t)((e->low >> 24) & 0xFF);
        } else {
            e->ff_count++;
        }
        e->low = (e->low << 8) & 0xFFFFFFFFU;
        e->range <<= 8;
    }
}

/* prob = P(bit=0). bit=0: range=bound. bit=1: lo+=bound, range-=bound */
static inline void rcenc_bit(rcenc_t *e, uint16_t prob, int bit) {
    uint32_t bound = (e->range >> PROB_BITS) * prob;
    if (bit == 0) {
        e->range = bound;
    } else {
        e->low += bound;
        e->range -= bound;
    }
    rcenc_normalize(e);
}

static size_t rcenc_flush(rcenc_t *e) {
    /* Exact copy of MCX rc_enc_flush */
    for (int i = 0; i < 5; i++) {
        rcenc_normalize(e);
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

/* Decoder */
typedef struct {
    uint32_t range, code;
    const uint8_t *buf;
    size_t pos, size;
} rcdec_t;

static void rcdec_init(rcdec_t *d, const uint8_t *buf, size_t size) {
    d->buf = buf; d->pos = 0; d->size = size;
    d->range = 0xFFFFFFFF;
    d->code = 0;
    for (int i = 0; i < 5 && d->pos < d->size; i++)
        d->code = (d->code << 8) | d->buf[d->pos++];
}

static inline void rcdec_normalize(rcdec_t *d) {
    while (d->range < RC_TOP) {
        d->range <<= 8;
        d->code = (d->code << 8) | (d->pos < d->size ? d->buf[d->pos++] : 0);
    }
}

/* prob = P(bit=0). Returns decoded bit. */
static inline int rcdec_bit(rcdec_t *d, uint16_t prob) {
    uint32_t bound = (d->range >> PROB_BITS) * prob;
    if (d->code < bound) {
        d->range = bound;
        rcdec_normalize(d);
        return 0;
    } else {
        d->code -= bound;
        d->range -= bound;
        rcdec_normalize(d);
        return 1;
    }
}

/* ── Probability Model ─────────────────────────────────────────── */

typedef struct {
    uint16_t *p;  /* P(bit=0) in range [0, PROB_MAX] */
    uint32_t n;
} model_t;

static void model_init(model_t *m, uint32_t n) {
    m->n = n;
    m->p = (uint16_t*)malloc(n * sizeof(uint16_t));
    for (uint32_t i = 0; i < n; i++) m->p[i] = PROB_HALF;
}

static void model_free(model_t *m) { free(m->p); m->p = NULL; }

static inline uint16_t model_get(model_t *m, uint32_t c) {
    return m->p[c % m->n];
}

/* Update: prob tracks P(bit=0) */
static inline void model_update(model_t *m, uint32_t c, int bit, int rate) {
    c %= m->n;
    uint16_t p = m->p[c];
    if (bit == 0)
        m->p[c] = p + ((PROB_MAX - p) >> rate);
    else
        m->p[c] = p - (p >> rate);
}

/* ── Logistic Mixer ────────────────────────────────────────────── */

#define MAX_INPUTS 20

/* Lookup table for stretch (int prob → float logit) and squash */
static float stretch_tab[4097]; /* [0..4096] → logit */
static float squash_tab[4097];  /* index = (x+8)*256, x in [-8,8] → prob */

static void init_tables(void) {
    for (int i = 0; i <= 4096; i++) {
        float p = (float)i / 4096.0f;
        if (p < 0.0001f) p = 0.0001f;
        if (p > 0.9999f) p = 0.9999f;
        stretch_tab[i] = logf(p / (1.0f - p));
    }
    for (int i = 0; i <= 4096; i++) {
        float x = ((float)i / 256.0f) - 8.0f;
        squash_tab[i] = 1.0f / (1.0f + expf(-x));
    }
}

static inline float stretch(float p) {
    int i = (int)(p * 4096.0f + 0.5f);
    if (i < 0) i = 0; if (i > 4096) i = 4096;
    return stretch_tab[i];
}

static inline float squash(float x) {
    int i = (int)((x + 8.0f) * 256.0f + 0.5f);
    if (i < 0) i = 0; if (i > 4096) i = 4096;
    return squash_tab[i];
}

typedef struct {
    float w[MAX_INPUTS];
    int n;
} mixer_t;

static void mixer_init(mixer_t *mx, int n) {
    mx->n = n;
    for (int i = 0; i < n; i++) mx->w[i] = 1.0f / n;
}

static float mixer_mix(mixer_t *mx, float *s) {
    float sum = 0;
    for (int i = 0; i < mx->n; i++) sum += mx->w[i] * s[i];
    float r = squash(sum);
    if (r < 0.001f) r = 0.001f;
    if (r > 0.999f) r = 0.999f;
    return r;
}

static void mixer_learn(mixer_t *mx, float *s, int bit, float lr) {
    float sum = 0;
    for (int i = 0; i < mx->n; i++) sum += mx->w[i] * s[i];
    /* bit=0 means P(0) should increase, so target = 1.0 for P(0) */
    float err = (1.0f - bit) - squash(sum);
    for (int i = 0; i < mx->n; i++)
        mx->w[i] += lr * err * s[i];
}

/* ── Match Model ───────────────────────────────────────────────── */

#define MATCH_HASH_SIZE (1 << 22) /* 4M entries for better match finding */

typedef struct {
    uint32_t *tab;
    const uint8_t *data;
    uint32_t mpos, mlen;
    int active;
} match_t;

static void match_init(match_t *m, const uint8_t *data) {
    m->tab = (uint32_t*)calloc(MATCH_HASH_SIZE, sizeof(uint32_t));
    memset(m->tab, 0xFF, MATCH_HASH_SIZE * sizeof(uint32_t));
    m->data = data; m->active = 0; m->mlen = 0;
}
static void match_free(match_t *m) { free(m->tab); }

/* Returns P(bit=0) based on match prediction */
static uint16_t match_predict(match_t *m, uint32_t pos, int bp) {
    if (!m->active || m->mpos >= pos) return PROB_HALF;
    uint8_t match_byte = m->data[m->mpos];
    int predicted_bit = (match_byte >> (7 - bp)) & 1;
    int conf = m->mlen > 64 ? 64 : (int)m->mlen;
    int delta = (conf * 1800) / 64;
    /* P(bit=0): if predicted=0, P(0) is high. If predicted=1, P(0) is low */
    return predicted_bit ? (PROB_HALF - delta) : (PROB_HALF + delta);
}

static void match_update(match_t *m, uint32_t pos) {
    if (pos < 4) return;
    if (m->active && m->mpos < pos) {
        if (m->data[m->mpos] == m->data[pos-1]) { m->mpos++; m->mlen++; }
        else { m->active = 0; m->mlen = 0; }
    }
    if (!m->active) {
        /* Try 4-byte match */
        uint32_t h4 = (m->data[pos-1] | ((uint32_t)m->data[pos-2]<<8) |
                       ((uint32_t)m->data[pos-3]<<16) | ((uint32_t)m->data[pos-4]<<24));
        h4 = (h4 * 2654435761u) >> (32 - 22);
        uint32_t ref = m->tab[h4];
        if (ref != 0xFFFFFFFF && ref >= 4 && ref + 4 <= pos &&
            m->data[ref-1]==m->data[pos-1] && m->data[ref-2]==m->data[pos-2] &&
            m->data[ref-3]==m->data[pos-3] && m->data[ref-4]==m->data[pos-4]) {
            m->active = 1; m->mpos = ref; m->mlen = 4;
        }
        m->tab[h4] = pos;
    }
}

/* ── SSE ───────────────────────────────────────────────────────── */

#define SSE_CTXS 256
#define SSE_BUCKETS 128

typedef struct {
    uint16_t t[SSE_CTXS][SSE_BUCKETS];
} sse_t;

static void sse_init(sse_t *s) {
    for (int c = 0; c < SSE_CTXS; c++)
        for (int b = 0; b < SSE_BUCKETS; b++)
            s->t[c][b] = (uint16_t)(PROB_MAX * (b + 0.5) / SSE_BUCKETS);
}

static uint16_t sse_map(sse_t *s, int ctx, uint16_t prob) {
    int b = prob * (SSE_BUCKETS - 1) / PROB_MAX;
    return s->t[ctx % SSE_CTXS][b];
}

static void sse_update(sse_t *s, int ctx, uint16_t prob, int bit) {
    int b = prob * (SSE_BUCKETS - 1) / PROB_MAX;
    int c = ctx % SSE_CTXS;
    uint16_t p = s->t[c][b];
    if (bit == 0)
        s->t[c][b] = p + ((PROB_MAX - p) >> 5);
    else
        s->t[c][b] = p - (p >> 5);
}

/* ── Hash ──────────────────────────────────────────────────────── */

static inline uint32_t h32(uint32_t a) {
    a = (a ^ 61) ^ (a >> 16);
    a += (a << 3); a ^= (a >> 4);
    a *= 0x27d4eb2d; a ^= (a >> 15);
    return a;
}

/* ── CM Engine ─────────────────────────────────────────────────── */

#define N_MODELS 20

typedef struct {
    model_t o0, o1, o2, o3, o4, o5, o6, o7;
    model_t word, sparse13, sparse14, sparse24;
    model_t charclass, nibble;
    model_t indirect;       /* indirect: predict byte after similar context */
    model_t o2_word;        /* order-2 combined with word hash */
    model_t gap15;          /* bytes -1,-5 */
    model_t delta;          /* diff between consecutive bytes */
    model_t o1_nibble;      /* order-1 on upper nibbles */
    match_t match;
    sse_t sse;
    mixer_t mx1[256];     /* Mixer 1: per prev[0] */
    mixer_t mx2[8];      /* Mixer 2: per nibble of prev[0] */
    mixer_t mx3[8];       /* Mixer 3: per bit position */
    mixer_t mxf[1];      /* Final mixer: combines mx1,mx2,mx3 outputs (global) */
    float lr;
    uint8_t prev[8];
    uint32_t word_hash;
    uint8_t partial;
    /* Indirect context: maps context hash → last byte seen in that context */
    uint8_t *ictx;        /* indirect context table */
    uint32_t ictx_size;
} cm_t;

static void cm_init(cm_t *cm, const uint8_t *data) {
    memset(cm, 0, sizeof(cm_t));
    model_init(&cm->o0, 512);
    model_init(&cm->o1, 256*256);      /* 64K — exact, no hash */
    model_init(&cm->o2, 1<<20);        /* 1M */
    model_init(&cm->o3, 1<<22);        /* 4M */
    model_init(&cm->o4, 1<<23);        /* 8M */
    model_init(&cm->o5, 1<<24);        /* 16M */
    model_init(&cm->o6, 1<<24);        /* 16M */
    model_init(&cm->o7, 1<<24);        /* 16M */
    model_init(&cm->word, 1<<22);      /* 4M */
    model_init(&cm->sparse13, 1<<20);  /* 1M */
    model_init(&cm->sparse14, 1<<20);  /* 1M */
    model_init(&cm->sparse24, 1<<20);  /* 1M */
    model_init(&cm->charclass, 1<<18); /* 256K */
    model_init(&cm->nibble, 1<<18);    /* 256K */
    model_init(&cm->indirect, 1<<22);   /* 4M — indirect context */
    model_init(&cm->o2_word, 1<<22);   /* 4M — order-2 × word */
    model_init(&cm->gap15, 1<<20);     /* 1M — bytes -1,-5 */
    model_init(&cm->delta, 1<<20);    /* 1M — delta context */
    model_init(&cm->o1_nibble, 1<<16);/* 64K — nibble order-1 */
    match_init(&cm->match, data);
    sse_init(&cm->sse);
    for (int i = 0; i < 256; i++) mixer_init(&cm->mx1[i], N_MODELS);
    for (int i = 0; i < 8; i++) mixer_init(&cm->mx2[i], N_MODELS);
    for (int i = 0; i < 8; i++) mixer_init(&cm->mx3[i], N_MODELS);
    mixer_init(&cm->mxf[0], 3);
    cm->lr = 0.012f;
    cm->partial = 1;
    cm->ictx_size = 1 << 22; /* 4M */
    cm->ictx = (uint8_t*)calloc(cm->ictx_size, 1);
}

static void cm_free(cm_t *cm) {
    model_free(&cm->o0); model_free(&cm->o1); model_free(&cm->o2);
    model_free(&cm->o3); model_free(&cm->o4); model_free(&cm->o5);
    model_free(&cm->o6); model_free(&cm->o7);
    model_free(&cm->word); model_free(&cm->sparse13);
    model_free(&cm->sparse14); model_free(&cm->sparse24);
    model_free(&cm->charclass); model_free(&cm->nibble);
    model_free(&cm->indirect); model_free(&cm->o2_word); model_free(&cm->gap15);
    model_free(&cm->delta); model_free(&cm->o1_nibble);
    match_free(&cm->match);
    if (cm->ictx) { free(cm->ictx); cm->ictx = NULL; }
}

static inline uint8_t char_class(uint8_t c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 2;
    if (c >= '0' && c <= '9') return 3;
    if (c == ' ') return 4;
    if (c == '\n' || c == '\r') return 5;
    return (c < 128) ? 6 : 7;
}

/* Compute all context hashes */
static void cm_contexts(cm_t *cm, uint32_t pos, int bp, uint32_t *ctx) {
    uint8_t par = cm->partial;
    uint8_t *p = cm->prev;
    uint32_t h01 = ((uint32_t)p[1]<<8)|p[0];
    uint32_t h0123 = ((uint32_t)p[3]<<24)|((uint32_t)p[2]<<16)|h01;
    
    ctx[0] = par;
    ctx[1] = (uint32_t)p[0]*256+par;
    ctx[2] = h32(h01) ^ par;
    ctx[3] = h32(((uint32_t)p[2]<<16)|h01) ^ par;
    ctx[4] = h32(h0123) ^ par;
    ctx[5] = h32(h32(h0123) ^ p[4]) ^ par;
    ctx[6] = h32(h32(h0123) ^ ((uint32_t)p[4]<<8|p[5])) ^ par;
    ctx[7] = h32(h32(h0123) ^ ((uint32_t)p[5]<<16|((uint32_t)p[6]<<8)|p[4])) ^ par;
    ctx[8] = h32(cm->word_hash) ^ par;
    ctx[9] = h32(((uint32_t)p[0]<<8)|p[2]) ^ par;
    ctx[10] = h32(((uint32_t)p[0]<<8)|p[3]) ^ par;
    ctx[11] = h32(((uint32_t)p[1]<<8)|p[3]) ^ par;
    ctx[12] = ((uint32_t)char_class(p[0])<<12)|((uint32_t)char_class(p[1])<<8)|par;
    ctx[13] = ((uint32_t)(p[0]>>4)<<12)|((uint32_t)(p[1]>>4)<<8)|par;
    
    /* Indirect context: look up what byte usually follows this o2 context,
       then use THAT byte as context for prediction */
    {
        uint32_t o2h = h32(h01) % cm->ictx_size;
        uint8_t indirect_byte = cm->ictx[o2h];
        ctx[14] = h32(((uint32_t)indirect_byte << 8) | par);
    }
    
    /* Order-2 × word hash */
    ctx[15] = h32(h01 ^ cm->word_hash) ^ par;
    
    /* Gap: bytes -1, -5 */
    ctx[16] = h32(((uint32_t)p[0]<<8)|p[4]) ^ par;
    
    /* Delta: diff between consecutive bytes × partial */
    ctx[17] = h32(((uint32_t)(uint8_t)(p[0]-p[1])<<8)|((uint8_t)(p[1]-p[2]))) ^ par;
    
    /* Nibble order-1 */
    ctx[18] = ((uint32_t)(p[0]>>4)<<8) | ((uint32_t)(p[1]>>4)<<4) | (par & 0xF);
}

/* Get P(bit=0) for current bit position */
static uint16_t cm_predict(cm_t *cm, uint32_t pos, int bp, float *stretched_out) {
    uint32_t ctx[N_MODELS];
    cm_contexts(cm, pos, bp, ctx);
    
    uint16_t preds[N_MODELS];
    preds[0]  = model_get(&cm->o0, ctx[0]);
    preds[1]  = model_get(&cm->o1, ctx[1]);
    preds[2]  = model_get(&cm->o2, ctx[2]);
    preds[3]  = model_get(&cm->o3, ctx[3]);
    preds[4]  = model_get(&cm->o4, ctx[4]);
    preds[5]  = model_get(&cm->o5, ctx[5]);
    preds[6]  = model_get(&cm->o6, ctx[6]);
    preds[7]  = model_get(&cm->o7, ctx[7]);
    preds[8]  = model_get(&cm->word, ctx[8]);
    preds[9]  = model_get(&cm->sparse13, ctx[9]);
    preds[10] = model_get(&cm->sparse14, ctx[10]);
    preds[11] = model_get(&cm->sparse24, ctx[11]);
    preds[12] = model_get(&cm->charclass, ctx[12]);
    preds[13] = model_get(&cm->nibble, ctx[13]);
    preds[14] = model_get(&cm->indirect, ctx[14]);
    preds[15] = model_get(&cm->o2_word, ctx[15]);
    preds[16] = model_get(&cm->gap15, ctx[16]);
    preds[17] = model_get(&cm->delta, ctx[17]);
    preds[18] = model_get(&cm->o1_nibble, ctx[18]);
    preds[19] = match_predict(&cm->match, pos, bp);
    
    for (int i = 0; i < N_MODELS; i++) {
        /* Mask out uninformative predictions (exactly 50%) */
        if (preds[i] == PROB_HALF)
            stretched_out[i] = 0.0f;
        else
            stretched_out[i] = stretch((float)preds[i] / PROB_MAX);
    }
    
    /* Three first-stage mixers, weighted average (mx1 is finest-grained) */
    float m1 = mixer_mix(&cm->mx1[cm->prev[0]], stretched_out);
    float m2 = mixer_mix(&cm->mx2[char_class(cm->prev[0])], stretched_out);
    float m3 = mixer_mix(&cm->mx3[bp], stretched_out);
    float mixed = (m1 + m2 + m3) / 3.0f;
    
    uint16_t mp = (uint16_t)(mixed * PROB_MAX);
    if (mp < 1) mp = 1;
    if (mp > PROB_MAX - 1) mp = PROB_MAX - 1;
    
    uint16_t final = sse_map(&cm->sse, cm->partial & (SSE_CTXS-1), mp);
    if (final < 1) final = 1;
    if (final > PROB_MAX - 1) final = PROB_MAX - 1;
    
    return final;
}

static void cm_update(cm_t *cm, uint32_t pos, int bp, int bit,
                      float *stretched, uint16_t mixed_prob) {
    uint32_t ctx[N_MODELS];
    cm_contexts(cm, pos, bp, ctx);
    
    model_update(&cm->o0, ctx[0], bit, 3);   /* Fast adapt for short ctx */
    model_update(&cm->o1, ctx[1], bit, 4);
    model_update(&cm->o2, ctx[2], bit, 4);
    model_update(&cm->o3, ctx[3], bit, 5);
    model_update(&cm->o4, ctx[4], bit, 5);
    model_update(&cm->o5, ctx[5], bit, 5);
    model_update(&cm->o6, ctx[6], bit, 6);
    model_update(&cm->o7, ctx[7], bit, 6);
    model_update(&cm->word, ctx[8], bit, 5);
    model_update(&cm->sparse13, ctx[9], bit, 4);
    model_update(&cm->sparse14, ctx[10], bit, 4);
    model_update(&cm->sparse24, ctx[11], bit, 4);
    model_update(&cm->charclass, ctx[12], bit, 4);
    model_update(&cm->nibble, ctx[13], bit, 4);
    model_update(&cm->indirect, ctx[14], bit, 5);
    model_update(&cm->o2_word, ctx[15], bit, 5);
    model_update(&cm->gap15, ctx[16], bit, 4);
    model_update(&cm->delta, ctx[17], bit, 5);
    model_update(&cm->o1_nibble, ctx[18], bit, 4);
    
    mixer_learn(&cm->mx1[cm->prev[0]], stretched, bit, cm->lr);
    mixer_learn(&cm->mx2[char_class(cm->prev[0])], stretched, bit, cm->lr);
    mixer_learn(&cm->mx3[bp], stretched, bit, cm->lr);
    
    /* SSE update */
    sse_update(&cm->sse, cm->partial & (SSE_CTXS-1), mixed_prob, bit);
    
    cm->partial = (cm->partial << 1) | bit;
}

static void cm_byte_done(cm_t *cm, uint8_t byte) {
    /* Update indirect context table: map (prev[0],prev[1]) → byte just seen */
    {
        uint32_t o2h = h32(((uint32_t)cm->prev[1]<<8)|cm->prev[0]) % cm->ictx_size;
        cm->ictx[o2h] = byte;
    }
    
    for (int j = 7; j > 0; j--) cm->prev[j] = cm->prev[j-1];
    cm->prev[0] = byte;
    cm->partial = 1;
    if ((byte>='a'&&byte<='z')||(byte>='A'&&byte<='Z')||byte=='\'')
        cm->word_hash = cm->word_hash * 31 + byte;
    else
        cm->word_hash = 0;
}

/* ── Compress ──────────────────────────────────────────────────── */

static size_t cm_compress(uint8_t *dst, size_t cap,
                          const uint8_t *src, size_t size) {
    if (cap < 8) return 0;
    dst[0]=(size)&0xFF; dst[1]=(size>>8)&0xFF;
    dst[2]=(size>>16)&0xFF; dst[3]=(size>>24)&0xFF;
    
    cm_t cm;
    cm_init(&cm, src);
    rcenc_t rc;
    rcenc_init(&rc, dst+4, cap-4);
    
    float stretched[N_MODELS];
    
    for (size_t i = 0; i < size; i++) {
        match_update(&cm.match, (uint32_t)i);
        cm.partial = 1;
        uint8_t byte = src[i];
        
        for (int bit = 7; bit >= 0; bit--) {
            int b = (byte >> bit) & 1;
            int bp = 7 - bit;
            
            uint16_t prob = cm_predict(&cm, (uint32_t)i, bp, stretched);
            rcenc_bit(&rc, prob, b);
            
            /* Recompute mixed_prob for updates */
            float em1 = mixer_mix(&cm.mx1[cm.prev[0]], stretched);
            float em2 = mixer_mix(&cm.mx2[char_class(cm.prev[0])], stretched);
            float em3 = mixer_mix(&cm.mx3[bp], stretched);
            float mixed = (em1 + em2 + em3) / 3.0f;
            uint16_t mp = (uint16_t)(mixed * PROB_MAX);
            if (mp < 1) mp = 1; if (mp > PROB_MAX-1) mp = PROB_MAX-1;
            
            cm_update(&cm, (uint32_t)i, bp, b, stretched, mp);
        }
        cm_byte_done(&cm, byte);
    }
    
    size_t comp = rcenc_flush(&rc) + 4;
    cm_free(&cm);
    return comp;
}

/* ── Decompress ────────────────────────────────────────────────── */

static size_t cm_decompress(uint8_t *dst, size_t cap,
                            const uint8_t *src, size_t src_size) {
    if (src_size < 4) return 0;
    size_t orig = (size_t)src[0] | ((size_t)src[1]<<8) |
                  ((size_t)src[2]<<16) | ((size_t)src[3]<<24);
    if (orig > cap) return 0;
    
    cm_t cm;
    cm_init(&cm, dst);
    rcdec_t rc;
    rcdec_init(&rc, src+4, src_size-4);
    
    float stretched[N_MODELS];
    
    for (size_t i = 0; i < orig; i++) {
        if (i > 0) match_update(&cm.match, (uint32_t)i);
        cm.partial = 1;
        uint8_t byte = 0;
        
        for (int bit = 7; bit >= 0; bit--) {
            int bp = 7 - bit;
            
            uint16_t prob = cm_predict(&cm, (uint32_t)i, bp, stretched);
            int b = rcdec_bit(&rc, prob);
            
            float dm1 = mixer_mix(&cm.mx1[cm.prev[0]], stretched);
            float dm2 = mixer_mix(&cm.mx2[char_class(cm.prev[0])], stretched);
            float dm3 = mixer_mix(&cm.mx3[bp], stretched);
            float mixed = (dm1 + dm2 + dm3) / 3.0f;
            uint16_t mp = (uint16_t)(mixed * PROB_MAX);
            if (mp < 1) mp = 1; if (mp > PROB_MAX-1) mp = PROB_MAX-1;
            
            cm_update(&cm, (uint32_t)i, bp, b, stretched, mp);
            byte = (byte << 1) | b;
        }
        dst[i] = byte;
        cm_byte_done(&cm, byte);
    }
    
    cm_free(&cm);
    return orig;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    init_tables();
    if (argc < 2) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }
    
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== CM v2 ===\n");
    printf("Input: %s (%zu bytes)\n\n", argv[1], size);
    
    size_t cap = size + size/4 + 1024;
    uint8_t *comp = malloc(cap);
    
    clock_t t0 = clock();
    size_t cs = cm_compress(comp, cap, data, size);
    double ct = (double)(clock()-t0)/CLOCKS_PER_SEC;
    
    printf("Compressed: %zu bytes (%.4f bpb, %.3fx)\n", cs, cs*8.0/size, (double)size/cs);
    printf("Compress: %.1f KB/s (%.2fs)\n\n", size/1024.0/ct, ct);
    
    uint8_t *dec = malloc(size);
    t0 = clock();
    size_t ds = cm_decompress(dec, size, comp, cs);
    double dt = (double)(clock()-t0)/CLOCKS_PER_SEC;
    
    int ok = (ds == size && memcmp(data, dec, size) == 0);
    printf("Roundtrip: %s\n", ok ? "✅ PASS" : "❌ FAIL");
    printf("Decompress: %.1f KB/s (%.2fs)\n\n", size/1024.0/dt, dt);
    
    if (!ok && ds == size) {
        for (size_t i = 0; i < size; i++)
            if (data[i] != dec[i]) {
                printf("Diff at %zu: want 0x%02X got 0x%02X\n", i, data[i], dec[i]);
                break;
            }
    }
    
    printf("Baselines: MCX-L12 43154 (3.52x) | bzip2 43207 | PAQ8 ~40000 (~3.8x)\n");
    
    free(data); free(comp); free(dec);
    return ok ? 0 : 1;
}
