/**
 * Exp 015: CM with StateMap (count-based adaptation)
 * 
 * Key insight from PAQ: instead of fixed shift-rate adaptation,
 * track the number of observations per context and adapt faster
 * for new contexts, slower for well-established ones.
 * 
 * StateMap: each entry is (prediction, count)
 * - New context (count=0): adapt fast (big steps)
 * - Old context (count=100+): adapt slow (small steps)
 * 
 * This is the #1 technique that makes PAQ beat everything else.
 */


/* For brevity, include the working exp014 and just swap the model */

#include "cm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

/* ── Range Coder (same as exp014) ──────────────────────────────── */

#define RC_TOP   0x01000000U
#define PROB_BITS 12
#define PROB_MAX  (1 << PROB_BITS)
#define PROB_HALF (PROB_MAX / 2)

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

static inline void rcenc_bit(rcenc_t *e, uint16_t prob, int bit) {
    uint32_t bound = (e->range >> PROB_BITS) * prob;
    if (bit == 0) { e->range = bound; }
    else { e->low += bound; e->range -= bound; }
    rcenc_normalize(e);
}

static size_t rcenc_flush(rcenc_t *e) {
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

typedef struct {
    uint32_t range, code;
    const uint8_t *buf;
    size_t pos, size;
} rcdec_t;

static void rcdec_init(rcdec_t *d, const uint8_t *buf, size_t size) {
    d->buf = buf; d->pos = 0; d->size = size;
    d->range = 0xFFFFFFFF; d->code = 0;
    for (int i = 0; i < 5 && d->pos < d->size; i++)
        d->code = (d->code << 8) | d->buf[d->pos++];
}

static inline int rcdec_bit(rcdec_t *d, uint16_t prob) {
    uint32_t bound = (d->range >> PROB_BITS) * prob;
    int bit;
    if (d->code < bound) {
        d->range = bound; bit = 0;
    } else {
        d->code -= bound; d->range -= bound; bit = 1;
    }
    while (d->range < RC_TOP) {
        d->range <<= 8;
        d->code = (d->code << 8) | (d->pos < d->size ? d->buf[d->pos++] : 0);
    }
    return bit;
}

/* ── StateMap: count-based adaptive probability ────────────────── */
/* Each entry stores: probability (22 bits) + count (10 bits)       */
/* Packed as uint32_t: [prob:22][count:10]                          */
/* Count saturates at 1023                                          */

#define SM_PROB_SHIFT 10
#define SM_COUNT_MASK ((1 << SM_PROB_SHIFT) - 1)  /* 1023 */
#define SM_PROB_INIT  (PROB_HALF << SM_PROB_SHIFT)

typedef struct {
    uint32_t *t;   /* packed (prob << 10) | count */
    uint32_t n;
    uint32_t mask; /* n-1 for fast masking */
    int rate_n;
} smap_t;

static void smap_init(smap_t *s, uint32_t n) {
    s->n = n; s->mask = n - 1; s->rate_n = 670;
    s->t = (uint32_t*)malloc(n * sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++)
        s->t[i] = SM_PROB_INIT; /* prob=HALF, count=0 */
}

static void smap_free(smap_t *s) { free(s->t); s->t = NULL; }

static inline uint16_t smap_get(smap_t *s, uint32_t c) {
    return (uint16_t)(s->t[c & s->mask] >> SM_PROB_SHIFT);
}

/* Adaptation rate table: rate decreases as count increases */
static const int adapt_rate[16] = {
    80, 52, 40, 32, 26, 21, 17, 14, 12, 10, 9, 8, 7, 6, 5, 5
};

static inline void smap_update(smap_t *s, uint32_t c, int bit) {
    c &= (s->n - 1);
    uint32_t v = s->t[c];
    int count = v & SM_COUNT_MASK;
    int prob = v >> SM_PROB_SHIFT;
    
    /* Rate based on count: tuned curve */
    int rate;
    rate = s->rate_n / (count + 3);
    if (rate < 1) rate = 1;
    
    /* Update probability */
    if (bit == 0)
        prob += (PROB_MAX - prob) * rate >> 8;
    else
        prob -= prob * rate >> 8;
    
    /* Clamp */
    if (prob < 1) prob = 1;
    if (prob > PROB_MAX - 1) prob = PROB_MAX - 1;
    
    /* Increment count (saturate at 1023) */
    if (count < SM_COUNT_MASK) count++;
    
    s->t[c] = ((uint32_t)prob << SM_PROB_SHIFT) | count;
}

/* ── Tables ────────────────────────────────────────────────────── */

static float stretch_tab[4097];
static float squash_tab[4097];

void mcx_cm_init(void) {
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

/* ── Mixer ─────────────────────────────────────────────────────── */

#define MAX_INPUTS 64

typedef struct {
    float w[MAX_INPUTS];
    float bias;
    int n;
} mixer_t;

static void mixer_init(mixer_t *mx, int n) {
    mx->n = n;
    for (int i = 0; i < n; i++) mx->w[i] = 1.9f / n;
    mx->bias = 0.0f;
}

#ifdef _MSC_VER
#define MCX_RESTRICT __restrict
#else
#define MCX_RESTRICT __restrict__
#endif

static inline float mixer_mix(mixer_t *mx, float * MCX_RESTRICT s) {
    float sum = 0;
    const float *w = mx->w;
    int i = 0;
    for (; i + 3 < mx->n; i += 4)
        sum += w[i]*s[i] + w[i+1]*s[i+1] + w[i+2]*s[i+2] + w[i+3]*s[i+3];
    for (; i < mx->n; i++)
        sum += w[i]*s[i];
    sum += mx->bias; sum *= 1.10f;
    float r = squash(sum);
    if (r < 0.001f) r = 0.001f;
    if (r > 0.999f) r = 0.999f;
    return r;
}

static inline void mixer_learn(mixer_t *mx, float * MCX_RESTRICT s, int bit, float lr) {
    float sum = 0;
    float *w = mx->w;
    int i = 0;
    for (; i + 3 < mx->n; i += 4)
        sum += w[i]*s[i] + w[i+1]*s[i+1] + w[i+2]*s[i+2] + w[i+3]*s[i+3];
    for (; i < mx->n; i++)
        sum += w[i]*s[i];
    sum += mx->bias; sum *= 1.10f;
    float err_lr = ((1.0f - bit) - squash(sum)) * lr;
    for (i = 0; i + 3 < mx->n; i += 4) {
        w[i]   += err_lr * s[i];
        w[i+1] += err_lr * s[i+1];
        w[i+2] += err_lr * s[i+2];
        w[i+3] += err_lr * s[i+3];
    }
    for (; i < mx->n; i++)
        w[i] += err_lr * s[i];
    mx->bias += err_lr;
}

/* ── Hash ──────────────────────────────────────────────────────── */

static inline uint32_t h32(uint32_t a) {
    a = (a ^ 61) ^ (a >> 16); a += (a << 3);
    a ^= (a >> 4); a *= 0x27d4eb2d; a ^= (a >> 15);
    return a;
}

/* ── Match Model ───────────────────────────────────────────────── */

#define MATCH_HASH4 (1 << 22)  /* 4M entries for 4-byte matches */
#define MATCH_HASH8 (1 << 20)  /* 1M entries for 8-byte matches */

typedef struct {
    uint32_t *tab4;   /* 4-byte context hash → position */
    uint32_t *tab8;   /* 8-byte context hash → position */
    const uint8_t *data;
    uint32_t mpos, mlen;
    int active;
} match_t;

static void match_init(match_t *m, const uint8_t *data) {
    m->tab4 = (uint32_t*)malloc(MATCH_HASH4 * sizeof(uint32_t));
    m->tab8 = (uint32_t*)malloc(MATCH_HASH8 * sizeof(uint32_t));
    memset(m->tab4, 0xFF, MATCH_HASH4 * sizeof(uint32_t));
    memset(m->tab8, 0xFF, MATCH_HASH8 * sizeof(uint32_t));
    m->data = data; m->active = 0; m->mlen = 0;
}
static void match_free(match_t *m) { free(m->tab4); free(m->tab8); }

static uint16_t match_predict(match_t *m, uint32_t pos, int bp) {
    if (!m->active || m->mpos >= pos) return PROB_HALF;
    int pred = (m->data[m->mpos] >> (7 - bp)) & 1;
    int len = m->mlen > 256 ? 256 : (int)m->mlen;
    /* Logarithmic confidence curve — faster ramp for short matches */
    int delta;
    if (len < 4) delta = len * 250;          /* 0-1000 */
    else if (len < 12) delta = 1000 + (len-4) * 100; /* 1000-1800 */
    else if (len < 32) delta = 1800 + (len-12) * 8;  /* 1800-1960 */
    else delta = 1960 + (len-32) * 1;        /* very slow ramp */
    if (delta > 2020) delta = 2020;
    return pred ? (PROB_HALF - delta) : (PROB_HALF + delta);
}

static void match_update(match_t *m, uint32_t pos) {
    if (pos < 8) return;
    if (m->active && m->mpos < pos) {
        if (m->data[m->mpos] == m->data[pos-1]) { m->mpos++; m->mlen++; }
        else { m->active = 0; m->mlen = 0; }
    }
    if (!m->active) {
        /* Try 8-byte match first (higher priority) */
        uint32_t h8 = h32(m->data[pos-1] | ((uint32_t)m->data[pos-2]<<8) |
                         ((uint32_t)m->data[pos-3]<<16) | ((uint32_t)m->data[pos-4]<<24));
        h8 = h32(h8 ^ (m->data[pos-5] | ((uint32_t)m->data[pos-6]<<8) |
                       ((uint32_t)m->data[pos-7]<<16) | ((uint32_t)m->data[pos-8]<<24)));
        h8 %= MATCH_HASH8;
        uint32_t ref8 = m->tab8[h8];
        if (ref8 != 0xFFFFFFFF && ref8 >= 8 && ref8 + 8 <= pos &&
            m->data[ref8-1]==m->data[pos-1] && m->data[ref8-2]==m->data[pos-2] &&
            m->data[ref8-3]==m->data[pos-3] && m->data[ref8-4]==m->data[pos-4]) {
            { uint32_t len = 4;
              while (len < pos && ref8 >= len + 1 &&
                     m->data[ref8-1-len] == m->data[pos-1-len]) len++;
              m->active = 1; m->mpos = ref8; m->mlen = len; }
            /* Extend match forward from ref8 */
        }
        m->tab8[h8] = pos;
        
        /* Fall back to 4-byte match */
        if (!m->active) {
            uint32_t h4 = (m->data[pos-1] | ((uint32_t)m->data[pos-2]<<8) |
                          ((uint32_t)m->data[pos-3]<<16) | ((uint32_t)m->data[pos-4]<<24));
            h4 = (h4 * 2654435761u) >> (32 - 22);
            uint32_t ref4 = m->tab4[h4];
            if (ref4 != 0xFFFFFFFF && ref4 >= 4 && ref4 + 4 <= pos &&
                m->data[ref4-1]==m->data[pos-1] && m->data[ref4-2]==m->data[pos-2] &&
                m->data[ref4-3]==m->data[pos-3] && m->data[ref4-4]==m->data[pos-4]) {
                { uint32_t len = 4;
                  while (len < pos && ref4 >= len + 1 &&
                         m->data[ref4-1-len] == m->data[pos-1-len]) len++;
                  m->active = 1; m->mpos = ref4; m->mlen = len; }
            }
            m->tab4[h4] = pos;
        }
    }
}

/* ── SSE ───────────────────────────────────────────────────────── */

#define SSE_CTXS 2048
#define SSE_BUCKETS 256

typedef struct {
    uint16_t t[SSE_CTXS][SSE_BUCKETS]; /* probabilities */
    uint8_t  n[SSE_CTXS][SSE_BUCKETS]; /* counts (0-255) */
} sse_t;

static void sse_init(sse_t *s) {
    for (int c = 0; c < SSE_CTXS; c++)
        for (int b = 0; b < SSE_BUCKETS; b++) {
            s->t[c][b] = (uint16_t)(PROB_MAX * (b + 0.5) / SSE_BUCKETS);
            s->n[c][b] = 0;
        }
}

static uint16_t sse_map(sse_t *s, int ctx, uint16_t prob) {
    /* Interpolated lookup between adjacent buckets */
    int scaled = prob * (SSE_BUCKETS - 1);
    int b = scaled / PROB_MAX;
    int frac = scaled % PROB_MAX; /* fractional part */
    int c = ctx & (SSE_CTXS - 1);
    if (b >= SSE_BUCKETS - 1) return s->t[c][SSE_BUCKETS - 1];
    /* Linear interpolation */
    uint32_t p0 = s->t[c][b];
    uint32_t p1 = s->t[c][b + 1];
    return (uint16_t)(p0 + (p1 - p0) * frac / PROB_MAX);
}

static void sse_update(sse_t *s, int ctx, uint16_t prob, int bit) {
    int b = prob * (SSE_BUCKETS - 1) / PROB_MAX;
    int c = ctx & (SSE_CTXS - 1);
    uint16_t p = s->t[c][b];
    int count = s->n[c][b];
    int rate = (count < 4) ? 2 : (count < 16) ? 3 : (count < 64) ? 4 : 5;
    if (count < 255) s->n[c][b] = count + 1;
    if (bit == 0) s->t[c][b] = p + ((PROB_MAX - p) >> rate);
    else          s->t[c][b] = p - (p >> rate);
}

static inline uint8_t char_class(uint8_t c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 2;
    if (c >= '0' && c <= '9') return 3;
    if (c == ' ') return 4;
    if (c == '\n' || c == '\r') return 5;
    return (c < 128) ? 6 : 7;
}

/* ── CM Engine (with StateMap) ─────────────────────────────────── */

#define N_MODELS 62

typedef struct {
    smap_t o0, o1, o2, o3, o4, o5, o6, o7;
    smap_t word, sparse13, sparse14, sparse24;
    smap_t charclass, o13;  /* nibble→o13 */
    smap_t indirect, o2_word, o11; /* gap15→o11 */
    smap_t o9, o12;         /* delta→o9, o1_nibble→o12 */
    smap_t o8;              /* order-8 */
    smap_t word2;           /* word order-2 (two consecutive words) */
    smap_t o14;             /* sparse024→o14 */
    smap_t word_cc;         /* word hash × char class */
    smap_t o1_cc;           /* order-1 × char class sequence */
    smap_t word_len;        /* word hash × word length */
    smap_t prevword_byte;   /* prev word hash × current byte */
    smap_t upper2;          /* upper nibble order-2 */
    smap_t o10;             /* o4_cc→o10 */
    smap_t word3;           /* word order-3 */
    smap_t word4;           /* word order-4 */
    smap_t run;             /* byte × run length */
    smap_t cc_seq3;         /* char-class 4-gram × byte value */
    smap_t word_boundary;   /* word boundary context */
    smap_t wind;            /* word-indirect context */
    smap_t o3ind;           /* o3-indirect context */
    smap_t colmod;          /* column/line-position model */
    smap_t colmod2;         /* column model with line length */
    smap_t colmod3;         /* column model 3 */
    smap_t colmod4;
    smap_t colmod5;
    smap_t wlenmod;
    smap_t sentmod;
    smap_t wind2;
    smap_t nibcross; smap_t wposmod; smap_t vcmod; smap_t vcmod2;
    smap_t sylmod; smap_t casemod; smap_t punctmod;
    smap_t bigrammod;
    smap_t triwordmod;
    smap_t sparsesm;    /* sparse match StateMap */
    smap_t digram;      /* digram frequency model */
    smap_t errmod;      /* error history model */
    smap_t gapmod;      /* XOR byte-diff model */
    smap_t cimod;       /* case-insensitive trigram */
    smap_t o4ind;       /* order-4 indirect (2-byte table) */
    smap_t o5ind;       /* order-5 indirect (1<<18 table) */
    smap_t o6ind;       /* order-6 indirect (1<<18 table) */
    smap_t o3ind2;      /* raw order-3 indirect with char_class */
    uint16_t digram_count[65536]; /* bigram frequency counts */
    uint8_t err_history; /* last 8 bit prediction errors packed */
    uint32_t byte_pos;   /* total bytes processed */
    uint32_t *smatch_tab; /* sparse match hash → position */
    int smatch_active;
    uint32_t smatch_pos;
    int smatch_len;
    int word_pos; int syl_count; int last_was_vowel;
    uint32_t vc_history; uint32_t case_history; uint32_t punct_history;
    match_t match;
    smap_t matchsm;   /* learned match StateMap prediction */
    sse_t apm;
    sse_t apm2;  /* second-stage APM with different context */
    sse_t apm3;  /* third-stage APM — deepest in chain */
    sse_t apm_par; /* parallel APM with word_hash context */
    mixer_t mx1[4096], mx2[128], mx3[8], mx4[1024], mx5[512], mx6[256], mx7[128];
    mixer_t mx8[512]; /* word_length(8) × bp(8) × char_class(4) × match(2) */
    float lr;
    uint8_t prev[14];
    uint32_t word_hash;
    uint32_t prev_word_hash; /* hash of previous word */
    uint32_t prev2_word_hash;
    uint32_t prev3_word_hash;
    uint8_t word_length;     /* current word length */
    uint8_t run_length;      /* consecutive same byte count */
    uint8_t partial;
    uint8_t *ictx;
    uint32_t ictx_size;
    uint8_t *ictx2;  /* word-indirect */
    uint32_t ictx2_size;
    uint8_t *ictx3;  /* o3-indirect */
    uint32_t ictx3_size;
    uint32_t total_bits; /* total bits processed */
    uint32_t line_pos;
    uint32_t last_line_len;
    uint32_t run_len;
    uint8_t run_class;
    uint32_t sent_pos;
    uint16_t *ictx5;
    uint32_t ictx5_size;
    uint16_t *ictx4;    /* o4-indirect 2-byte table */
    uint32_t ictx4_size;
    uint8_t *ictx6;     /* o5-indirect (1<<18) */
    uint32_t ictx6_size;
    uint8_t *ictx7;     /* o6-indirect (1<<18) */
    uint32_t ictx7_size;
    uint8_t *ictx8;     /* o3-indirect2 (1<<18) */
    uint32_t ictx8_size;
} cm_t;

static void cm_init(cm_t *cm, const uint8_t *data, size_t data_size) {
    memset(cm, 0, sizeof(cm_t));
    /* Scale tables with file size to avoid OOM */
    int hi_log = 21; /* high-order models — default for large files */
    int lo_log = 20;
    if (data_size <= 256*1024) { hi_log = 24; lo_log = 23; }
    else if (data_size <= 2*1024*1024) { hi_log = 23; lo_log = 23; }
    else if (data_size <= 16*1024*1024) { hi_log = 22; lo_log = 21; }
    smap_init(&cm->o0, 512);
    smap_init(&cm->o1, 256*256);
    smap_init(&cm->o2, 1<<hi_log);
    smap_init(&cm->o3, 1<<hi_log);
    smap_init(&cm->o4, 1<<hi_log); cm->o4.rate_n = 520;
    smap_init(&cm->o5, 1<<hi_log); cm->o5.rate_n = 450;
    smap_init(&cm->o6, 1<<hi_log); cm->o6.rate_n = 450;
    smap_init(&cm->o7, 1<<hi_log); cm->o7.rate_n = 380;
    smap_init(&cm->word, 1<<lo_log); cm->word.rate_n = 780;
    smap_init(&cm->sparse13, 1<<lo_log); cm->sparse13.rate_n = 500;
    smap_init(&cm->sparse14, 1<<lo_log); cm->sparse14.rate_n = 360;
    smap_init(&cm->sparse24, 1<<lo_log);
    smap_init(&cm->charclass, 1<<lo_log); cm->charclass.rate_n = 180;
    smap_init(&cm->o13, 1<<hi_log); cm->o13.rate_n = 400;
    smap_init(&cm->indirect, 1<<lo_log);
    smap_init(&cm->o2_word, 1<<lo_log); cm->o2_word.rate_n = 280;
    smap_init(&cm->o11, 1<<hi_log); cm->o11.rate_n = 400;
    smap_init(&cm->o9, 1<<hi_log); cm->o9.rate_n = 400;
    smap_init(&cm->o12, 1<<hi_log); cm->o12.rate_n = 400;
    smap_init(&cm->o8, 1<<hi_log); cm->o8.rate_n = 450;
    smap_init(&cm->word2, 1<<lo_log); cm->word2.rate_n = 530;
    smap_init(&cm->o14, 1<<hi_log); cm->o14.rate_n = 400;
    smap_init(&cm->word_cc, 1<<lo_log);
    smap_init(&cm->o1_cc, 1<<lo_log); cm->o1_cc.rate_n = 200;
    smap_init(&cm->word_len, 1<<lo_log); cm->word_len.rate_n = 210;
    smap_init(&cm->prevword_byte, 1<<lo_log); cm->prevword_byte.rate_n = 280;
    smap_init(&cm->upper2, 1<<lo_log); cm->upper2.rate_n = 500;
    smap_init(&cm->word3, 1<<lo_log);
    smap_init(&cm->word4, 1<<lo_log); cm->word4.rate_n = 500;
    smap_init(&cm->run, 1<<lo_log); cm->run.rate_n = 135;
    smap_init(&cm->o10, 1<<hi_log); cm->o10.rate_n = 400;
    smap_init(&cm->cc_seq3, 1<<lo_log);
    smap_init(&cm->word_boundary, 1<<lo_log); cm->word_boundary.rate_n = 400;
    smap_init(&cm->wind, 1<<lo_log); cm->wind.rate_n = 415;
    smap_init(&cm->o3ind, 1<<lo_log);
    smap_init(&cm->colmod, 1<<lo_log);
    smap_init(&cm->colmod2, 1<<lo_log);
    smap_init(&cm->colmod3, 1<<lo_log); smap_init(&cm->colmod4, 1<<lo_log);
    smap_init(&cm->colmod5, 1<<lo_log); cm->colmod5.rate_n = 520; smap_init(&cm->wlenmod, 1<<lo_log); cm->wlenmod.rate_n = 1000;
    smap_init(&cm->sentmod, 1<<lo_log); smap_init(&cm->wind2, 1<<lo_log); cm->wind2.rate_n = 500;
    smap_init(&cm->nibcross, 1<<hi_log); cm->nibcross.rate_n = 550;
    smap_init(&cm->wposmod, 1<<hi_log); smap_init(&cm->vcmod, 1<<hi_log); cm->wposmod.rate_n = 550; cm->vcmod.rate_n = 550;
    smap_init(&cm->vcmod2, 1<<hi_log); smap_init(&cm->sylmod, 1<<hi_log); cm->vcmod2.rate_n = 550;
    smap_init(&cm->casemod, 1<<hi_log); smap_init(&cm->punctmod, 1<<hi_log); cm->casemod.rate_n = 550;
    smap_init(&cm->bigrammod, 1<<hi_log); cm->bigrammod.rate_n = 550;
    smap_init(&cm->triwordmod, 1<<hi_log); cm->triwordmod.rate_n = 950;
    smap_init(&cm->sparsesm, 1<<14); cm->sparsesm.rate_n = 900;
    smap_init(&cm->digram, 1<<lo_log); cm->digram.rate_n = 550;
    smap_init(&cm->errmod, 1<<lo_log); cm->errmod.rate_n = 550;
    smap_init(&cm->gapmod, 1<<lo_log);
    smap_init(&cm->cimod, 1<<lo_log);
    memset(cm->digram_count, 0, sizeof(cm->digram_count));
    cm->err_history = 0; cm->byte_pos = 0;
    cm->smatch_tab = (uint32_t*)malloc((1<<18) * sizeof(uint32_t));
    memset(cm->smatch_tab, 0xFF, (1<<18) * sizeof(uint32_t));
    cm->smatch_active = 0; cm->smatch_len = 0;
    cm->word_pos=0; cm->syl_count=0; cm->last_was_vowel=0;
    cm->vc_history=0; cm->case_history=0; cm->punct_history=0;
    cm->ictx5_size = 1 << 22;
    cm->ictx5 = (uint16_t*)calloc(cm->ictx5_size, sizeof(uint16_t));
    match_init(&cm->match, data);
    smap_init(&cm->matchsm, 1 << 16); cm->matchsm.rate_n = 1000;
    sse_init(&cm->apm);
    sse_init(&cm->apm2);
    sse_init(&cm->apm3);
    sse_init(&cm->apm_par);
    for (int i = 0; i < 4096; i++) mixer_init(&cm->mx1[i], N_MODELS);
    for (int i = 0; i < 128; i++) mixer_init(&cm->mx2[i], N_MODELS);
    for (int i = 0; i < 8; i++) mixer_init(&cm->mx3[i], N_MODELS);
    for (int i = 0; i < 1024; i++) mixer_init(&cm->mx4[i], N_MODELS);
    for (int i = 0; i < 512; i++) mixer_init(&cm->mx5[i], N_MODELS);
    for (int i = 0; i < 256; i++) mixer_init(&cm->mx6[i], N_MODELS);
    for (int i = 0; i < 128; i++) mixer_init(&cm->mx7[i], N_MODELS);
    for (int i = 0; i < 512; i++) mixer_init(&cm->mx8[i], N_MODELS);
    cm->lr = 0.021f; /* initial; decays via 0.002 + 0.019/(1+bits/5000) */
    cm->partial = 1;
    cm->ictx_size = 1 << 22;
    cm->ictx = (uint8_t*)calloc(cm->ictx_size, 1);
    cm->ictx2_size = 1 << 22;
    cm->ictx2 = (uint8_t*)calloc(cm->ictx2_size, 1);
    cm->ictx3_size = 1 << 22;
    cm->ictx3 = (uint8_t*)calloc(cm->ictx3_size, 1);
    /* New indirect-context smaps: capped smaller than lo_log to stay in 3.9GB
       budget when lo_log=23 (small-file tier). Research used 1<<lo_log on a
       machine with more RAM; on this Atom we trade some precision for fit. */
    int ind_log = lo_log > 22 ? 22 : lo_log;
    cm->ictx4_size = 1 << 23;
    cm->ictx4 = (uint16_t*)calloc(cm->ictx4_size, sizeof(uint16_t));
    smap_init(&cm->o4ind, 1<<ind_log);
    cm->ictx6_size = 1 << 18;
    cm->ictx6 = (uint8_t*)calloc(cm->ictx6_size, 1);
    smap_init(&cm->o5ind, 1<<ind_log);
    cm->ictx7_size = 1 << 18;
    cm->ictx7 = (uint8_t*)calloc(cm->ictx7_size, 1);
    smap_init(&cm->o6ind, 1<<ind_log);
    cm->ictx8_size = 1 << 18;
    cm->ictx8 = (uint8_t*)calloc(cm->ictx8_size, 1);
    smap_init(&cm->o3ind2, 1<<ind_log);
}

static void cm_free(cm_t *cm) {
    smap_free(&cm->o0); smap_free(&cm->o1); smap_free(&cm->o2);
    smap_free(&cm->o3); smap_free(&cm->o4); smap_free(&cm->o5);
    smap_free(&cm->o6); smap_free(&cm->o7); smap_free(&cm->word);
    smap_free(&cm->sparse13); smap_free(&cm->sparse14); smap_free(&cm->sparse24);
    smap_free(&cm->charclass); smap_free(&cm->o13);
    smap_free(&cm->indirect); smap_free(&cm->o2_word); smap_free(&cm->o11);
    smap_free(&cm->o9); smap_free(&cm->o12);
    smap_free(&cm->o8);
    smap_free(&cm->word2); smap_free(&cm->o14);
    smap_free(&cm->word_cc); smap_free(&cm->o1_cc); smap_free(&cm->word_len); smap_free(&cm->prevword_byte);
    smap_free(&cm->upper2); smap_free(&cm->o10);
    smap_free(&cm->word3); smap_free(&cm->word4); smap_free(&cm->run);
    smap_free(&cm->cc_seq3);
    smap_free(&cm->word_boundary);
    smap_free(&cm->wind);
    smap_free(&cm->o3ind); smap_free(&cm->colmod); smap_free(&cm->colmod2); smap_free(&cm->colmod3); smap_free(&cm->colmod4);
    smap_free(&cm->colmod5); smap_free(&cm->wlenmod);
    smap_free(&cm->sentmod); smap_free(&cm->wind2);
    smap_free(&cm->nibcross); smap_free(&cm->wposmod);
    smap_free(&cm->vcmod); smap_free(&cm->vcmod2);
    smap_free(&cm->sylmod); smap_free(&cm->casemod); smap_free(&cm->punctmod);
    smap_free(&cm->bigrammod); smap_free(&cm->triwordmod);
    smap_free(&cm->sparsesm); smap_free(&cm->digram);
    smap_free(&cm->errmod); smap_free(&cm->gapmod); smap_free(&cm->cimod);
    if (cm->smatch_tab) free(cm->smatch_tab);
    if (cm->ictx5) free(cm->ictx5);
    match_free(&cm->match);
    smap_free(&cm->matchsm);
    if (cm->ictx) free(cm->ictx);
    if (cm->ictx2) free(cm->ictx2);
    if (cm->ictx3) free(cm->ictx3);
    if (cm->ictx4) free(cm->ictx4);
    if (cm->ictx6) free(cm->ictx6);
    if (cm->ictx7) free(cm->ictx7);
    if (cm->ictx8) free(cm->ictx8);
    smap_free(&cm->o4ind); smap_free(&cm->o5ind); smap_free(&cm->o6ind); smap_free(&cm->o3ind2);
}

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
    /* order-13 */
    ctx[13] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ h32((uint32_t)p[8]<<24|p[9]<<16|p[10]<<8|p[11]) ^ ((uint32_t)p[12]<<8)) ^ par;
    
    uint32_t o2h = h32(h01) & (cm->ictx_size - 1);
    ctx[14] = h32(((uint32_t)cm->ictx[o2h] << 11) | ((uint32_t)char_class(p[0]) << 8) | par);
    ctx[15] = h32(h01 ^ cm->word_hash) ^ par;
    /* o11: order-11 */
    ctx[16] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<24|p[9]<<16|p[10]<<8)) ^ par;
    /* o9: order-9 */
    ctx[17] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<8)) ^ par;
    /* o12: order-12 */
    ctx[18] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<24|p[9]<<16|p[10]<<8|p[11])) ^ par;
    /* Order-8 */
    ctx[19] = h32(h32(h0123) ^ h32(((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7])) ^ par;
    /* Word order-2 (current word × previous word) */
    ctx[20] = h32(cm->word_hash ^ cm->prev_word_hash) ^ par;
    /* o14: order-14 */
    ctx[21] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ h32((uint32_t)p[8]<<24|p[9]<<16|p[10]<<8|p[11]) ^ ((uint32_t)p[12]<<16|p[13]<<8)) ^ par;
    /* Word × char class */
    ctx[22] = h32(cm->word_hash ^ ((uint32_t)char_class(p[0])<<16)) ^ par;
    /* Order-1 × char class sequence (3 consecutive classes) */
    ctx[23] = h32(((uint32_t)p[0]<<16) | ((uint32_t)char_class(p[0])<<12) |
                  ((uint32_t)char_class(p[1])<<8) | ((uint32_t)char_class(p[2])<<4)) ^ par;
    /* Word hash × word length */
    ctx[24] = h32(cm->word_hash ^ ((uint32_t)cm->word_length << 24)) ^ par;
    /* Previous word × current byte */
    ctx[25] = h32(cm->prev_word_hash ^ ((uint32_t)p[0] << 16)) ^ par;
    /* Upper nibble order-2 */
    ctx[26] = ((uint32_t)(p[0]>>4)<<12) | ((uint32_t)(p[1]>>4)<<8) | ((uint32_t)(p[2]>>4)<<4) | par;
    /* o10: order-10 */
    ctx[27] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<16|p[9]<<8)) ^ par;
    ctx[28] = h32(cm->word_hash ^ cm->prev_word_hash ^ cm->prev2_word_hash) ^ par;
    ctx[29] = h32(cm->word_hash ^ cm->prev_word_hash ^ cm->prev2_word_hash ^ cm->prev3_word_hash) ^ par;
    /* char-class 4-gram: 4 consecutive char classes × current byte */
    ctx[30] = h32(((uint32_t)char_class(p[0])<<18)|((uint32_t)char_class(p[1])<<15)|
                  ((uint32_t)char_class(p[2])<<12)|((uint32_t)char_class(p[3])<<9)|
                  ((uint32_t)p[0]<<1)) ^ par;
    /* word boundary: transition type × recent context */
    { int wb = (char_class(p[0]) != char_class(p[1])) ? 1 : 0;
      int wb2 = (char_class(p[1]) != char_class(p[2])) ? 1 : 0;
      ctx[31] = h32(((uint32_t)wb<<17)|((uint32_t)wb2<<16)|((uint32_t)p[0]<<8)|p[1]) ^ par;
    }
    /* word-indirect: word_hash → predicted next byte */
    { uint32_t wh = cm->word_hash & (cm->ictx2_size - 1);
      ctx[32] = h32(((uint32_t)cm->ictx2[wh] << 11) | ((uint32_t)char_class(p[0]) << 8) | par);
    }
    /* o3-indirect: h(prev2,prev1,prev0) → predicted next byte */
    { uint32_t o3h = h32(((uint32_t)p[2]<<16)|((uint32_t)p[1]<<8)|p[0]) & (cm->ictx3_size - 1);
      ctx[33] = h32(((uint32_t)cm->ictx3[o3h] << 8) | par);
    }
    /* Column model: position within line */
    ctx[34] = h32(((uint32_t)(cm->line_pos & 0xFF) << 11) | ((uint32_t)char_class(p[0]) << 8) | par);
    ctx[35] = h32(((uint32_t)(cm->line_pos & 0xFF) << 16) | ((uint32_t)cm->last_line_len << 8) | par);
    ctx[36] = h32(((uint32_t)(cm->line_pos & 0xFF) << 16) | ((uint32_t)p[0] << 8) | par);
    ctx[37] = h32(((uint32_t)(cm->line_pos & 0xFF) << 8) | par);
    ctx[38] = h32(((uint32_t)(cm->line_pos & 0xFF) << 14) | ((uint32_t)char_class(p[0]) << 11) | ((uint32_t)char_class(p[1]) << 8) | par);
    ctx[39] = h32(((uint32_t)(cm->run_len & 31) << 11) | ((uint32_t)cm->run_class << 8) | par);
    { uint32_t sp = cm->sent_pos;
      int sp_bucket = (sp < 4) ? sp : (sp < 16) ? 4 + (sp>>2) : (sp < 64) ? 8 + (sp>>4) : 12;
      ctx[40] = h32(((uint32_t)sp_bucket << 12) | ((uint32_t)p[0] << 4) | par);
    }
    { uint32_t wh5 = cm->word_hash & (cm->ictx5_size - 1);
      uint16_t w2 = cm->ictx5[wh5];
      ctx[41] = h32(((uint32_t)w2 << 8) | par);
    }
}

static uint16_t cm_predict(cm_t *cm, uint32_t pos, int bp, float *str, uint16_t *raw_mp) {
    uint32_t ctx[N_MODELS];
    cm_contexts(cm, pos, bp, ctx);
    
    uint16_t preds[N_MODELS];
    preds[0]  = smap_get(&cm->o0, ctx[0]);
    preds[1]  = smap_get(&cm->o1, ctx[1]);
    preds[2]  = smap_get(&cm->o2, ctx[2]);
    preds[3]  = smap_get(&cm->o3, ctx[3]);
    preds[4]  = smap_get(&cm->o4, ctx[4]);
    preds[5]  = smap_get(&cm->o5, ctx[5]);
    preds[6]  = smap_get(&cm->o6, ctx[6]);
    preds[7]  = smap_get(&cm->o7, ctx[7]);
    preds[8]  = smap_get(&cm->word, ctx[8]);
    preds[9]  = smap_get(&cm->sparse13, ctx[9]);
    preds[10] = smap_get(&cm->sparse14, ctx[10]);
    preds[11] = smap_get(&cm->sparse24, ctx[11]);
    preds[12] = smap_get(&cm->charclass, ctx[12]);
    preds[13] = smap_get(&cm->o13, ctx[13]);
    preds[14] = smap_get(&cm->indirect, ctx[14]);
    preds[15] = smap_get(&cm->o2_word, ctx[15]);
    preds[16] = smap_get(&cm->o11, ctx[16]);
    preds[17] = smap_get(&cm->o9, ctx[17]);
    preds[18] = smap_get(&cm->o12, ctx[18]);
    preds[19] = smap_get(&cm->o8, ctx[19]);
    preds[20] = smap_get(&cm->word2, ctx[20]);
    preds[21] = smap_get(&cm->o14, ctx[21]);
    preds[22] = smap_get(&cm->word_cc, ctx[22]);
    preds[23] = smap_get(&cm->o1_cc, ctx[23]);
    preds[24] = smap_get(&cm->word_len, ctx[24]);
    preds[25] = smap_get(&cm->prevword_byte, ctx[25]);
    preds[26] = smap_get(&cm->upper2, ctx[26]);
    preds[27] = smap_get(&cm->o10, ctx[27]);
    preds[28] = smap_get(&cm->word3, ctx[28]);
    preds[29] = smap_get(&cm->word4, ctx[29]);
    { uint32_t run_ctx = h32(((uint32_t)cm->prev[0] << 8) | cm->run_length) ^ cm->partial;
      preds[30] = smap_get(&cm->run, run_ctx); }
    preds[31] = match_predict(&cm->match, pos, bp);
    /* Override with learned StateMap if match active */
    if (cm->match.active && cm->match.mpos < pos) {
        int ml = cm->match.mlen > 256 ? 256 : (int)cm->match.mlen;
        int pred_bit = (cm->match.data[cm->match.mpos] >> (7 - bp)) & 1;
        int ml_b = ml < 4 ? ml : ml < 8 ? 4 : ml < 16 ? 5 : ml < 32 ? 6 : 7;
        int mcc = (cm->prev[0] >= 'a' && cm->prev[0] <= 'z') ? 0 : (cm->prev[0] >= 'A' && cm->prev[0] <= 'Z') ? 1 : (cm->prev[0] >= '0' && cm->prev[0] <= '9') ? 2 : 3;
        uint32_t msm_ctx = (pred_bit << 15) | (ml_b << 12) | (mcc << 10) | ((cm->prev[0] >> 6) << 8) | (cm->partial << 4) | bp;
        preds[31] = smap_get(&cm->matchsm, msm_ctx);
    }
    preds[32] = smap_get(&cm->cc_seq3, ctx[30]);
    preds[33] = smap_get(&cm->word_boundary, ctx[31]);
    preds[34] = smap_get(&cm->wind, ctx[32]);
    preds[35] = smap_get(&cm->o3ind, ctx[33]);
    preds[36] = smap_get(&cm->colmod, ctx[34]);
    preds[37] = smap_get(&cm->colmod2, ctx[35]);
    preds[38] = smap_get(&cm->colmod3, ctx[36]);
    preds[39] = smap_get(&cm->colmod4, ctx[37]);
    preds[40] = smap_get(&cm->colmod5, ctx[38]);
    preds[41] = smap_get(&cm->wlenmod, ctx[39]);
    preds[42] = smap_get(&cm->sentmod, ctx[40]);
    preds[43] = smap_get(&cm->wind2, ctx[41]);
    {uint32_t nc; if(bp>=4) nc=h32(((uint32_t)cm->partial<<8)|cm->prev[0])^((uint32_t)cm->prev[1]<<16); else nc=h32(((uint32_t)cm->prev[0]<<12)|((uint32_t)cm->prev[1]>>4<<4)|bp); preds[44]=smap_get(&cm->nibcross,nc);}
    /* preds[45]-[57]: models ported from research exp015 */
    { int wp = cm->word_pos;
      int wp_bucket = (wp < 2) ? wp : (wp < 5) ? 2 : (wp < 10) ? 3 : 4;
      uint32_t wpos_ctx = h32(((uint32_t)wp_bucket << 16) | ((uint32_t)cm->prev[0] << 8) | cm->prev[1]) ^ (cm->partial << 20);
      preds[45] = smap_get(&cm->wposmod, wpos_ctx); }
    { uint32_t vc_ctx = h32(((cm->vc_history & 0xFF) << 8) | cm->prev[0]) ^ (cm->partial << 16);
      preds[46] = smap_get(&cm->vcmod, vc_ctx); }
    { uint32_t vc2_ctx = h32(((cm->vc_history & 0xFF) << 16) | (cm->word_hash & 0xFFFF)) ^ (cm->partial << 24);
      preds[47] = smap_get(&cm->vcmod2, vc2_ctx); }
    { int syl_b = (cm->syl_count < 4) ? cm->syl_count : 4;
      uint32_t syl_ctx = h32(((uint32_t)syl_b << 16) | ((uint32_t)cm->prev[0] << 8) | cm->prev[1]) ^ (cm->partial << 20);
      preds[48] = smap_get(&cm->sylmod, syl_ctx); }
    { uint32_t case_ctx = h32(((cm->case_history & 0xFF) << 8) | cm->prev[0]) ^ (cm->partial << 16);
      preds[49] = smap_get(&cm->casemod, case_ctx); }
    { uint32_t pnct_ctx = h32(((cm->punct_history & 0xFFF) << 8) | cm->prev[0]) ^ (cm->partial << 20);
      preds[50] = smap_get(&cm->punctmod, pnct_ctx); }
    { int wp_b = (cm->word_pos < 2) ? cm->word_pos : (cm->word_pos < 5) ? 2 : 3;
      uint32_t bg_ctx = h32(((uint32_t)cm->prev[0] << 16) | ((uint32_t)cm->prev[1] << 8) | (wp_b << 4) | bp);
      preds[51] = smap_get(&cm->bigrammod, bg_ctx); }
    { uint32_t tw_ctx = h32(((uint32_t)cm->prev[0] << 16) | ((uint32_t)cm->prev[1] << 8) | cm->prev[2]) ^ (cm->word_hash << 4) ^ (cm->partial << 24);
      preds[52] = smap_get(&cm->triwordmod, tw_ctx); }
    { uint16_t sp = PROB_HALF;
      if (cm->smatch_active && cm->smatch_pos < pos) {
          int pred = (cm->match.data[cm->smatch_pos] >> (7 - bp)) & 1;
          int slen = cm->smatch_len > 64 ? 64 : cm->smatch_len;
          int sl_b = slen < 4 ? slen : slen < 8 ? 4 : slen < 16 ? 5 : slen < 32 ? 6 : 7;
          uint32_t ssm_ctx = (pred << 13) | (sl_b << 10) | ((cm->prev[0] >> 5) << 7) | (cm->partial << 3) | bp;
          sp = smap_get(&cm->sparsesm, ssm_ctx);
      }
      preds[53] = sp; }
    { uint16_t bg = ((uint16_t)cm->prev[0] << 8) | cm->partial;
      int freq = cm->digram_count[(cm->prev[1] << 8) | cm->prev[0]];
      int fb = freq < 2 ? 0 : freq < 8 ? 1 : freq < 32 ? 2 : 3;
      uint32_t dg_ctx = h32((fb << 16) | bg);
      preds[54] = smap_get(&cm->digram, dg_ctx); }
    { uint32_t eh_ctx = h32((cm->err_history << 16) | ((uint16_t)cm->prev[0] << 8) | cm->partial);
      preds[55] = smap_get(&cm->errmod, eh_ctx); }
    { uint32_t gap_ctx = h32(((uint32_t)(cm->prev[0] ^ cm->prev[1]) << 8) | cm->prev[2]) ^ (cm->partial << 20);
      preds[56] = smap_get(&cm->gapmod, gap_ctx); }
    { uint8_t p0 = cm->prev[0], p1 = cm->prev[1], p2 = cm->prev[2];
      if (p0 >= 'A' && p0 <= 'Z') p0 |= 0x20;
      if (p1 >= 'A' && p1 <= 'Z') p1 |= 0x20;
      if (p2 >= 'A' && p2 <= 'Z') p2 |= 0x20;
      uint32_t ci_ctx = h32(((uint32_t)p0 << 16) | ((uint32_t)p1 << 8) | p2) ^ (cm->partial << 20);
      preds[57] = smap_get(&cm->cimod, ci_ctx); }
    /* o4-indirect: h(prev[0..3]) → predicted 2-byte → context */
    { uint32_t o4h = h32(((uint32_t)cm->prev[3]<<24)|((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx4_size - 1);
      uint16_t iv4 = cm->ictx4[o4h];
      preds[58] = smap_get(&cm->o4ind, h32(((uint32_t)iv4 << 8) | cm->partial)); }
    /* o5-indirect */
    { uint32_t o5h = h32(((uint32_t)cm->prev[4]<<24)|((uint32_t)cm->prev[3]<<16)|((uint32_t)cm->prev[2]<<8)|cm->prev[1]) ^ h32(cm->prev[0]);
      o5h &= (cm->ictx6_size - 1);
      preds[59] = smap_get(&cm->o5ind, h32(((uint32_t)cm->ictx6[o5h] << 8) | cm->partial)); }
    /* o6-indirect */
    { uint32_t o6h = h32(((uint32_t)cm->prev[5]<<24)|((uint32_t)cm->prev[4]<<16)|((uint32_t)cm->prev[3]<<8)|cm->prev[2]) ^ h32(((uint32_t)cm->prev[1]<<8)|cm->prev[0]);
      o6h &= (cm->ictx7_size - 1);
      preds[60] = smap_get(&cm->o6ind, h32(((uint32_t)cm->ictx7[o6h] << 8) | cm->partial)); }
    /* o3-indirect2: raw o3 with char_class enrichment */
    { uint32_t o3h2 = h32(((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx8_size - 1);
      preds[61] = smap_get(&cm->o3ind2, h32(((uint32_t)cm->ictx8[o3h2] << 11) | ((uint32_t)char_class(cm->prev[0]) << 8) | cm->partial)); }

    for (int i = 0; i < N_MODELS; i++) {
        str[i] = stretch_tab[preds[i]]; /* direct lookup, no float division */
    }
    
    float m1 = mixer_mix(&cm->mx1[(cm->prev[0] << 4) | (cm->prev[1] >> 6 << 3) | bp], str);
    float m2 = mixer_mix(&cm->mx2[((char_class(cm->prev[0]) << 4) | (bp << 1) | (cm->prev[1] >> 7)) & 127], str);
    float m3 = mixer_mix(&cm->mx3[bp], str);
    int mx4_ctx = (((cm->prev[0] >> 4) << 5) | ((cm->prev[1] >> 4) << 1) | bp/4) & 1023;
    float m4 = mixer_mix(&cm->mx4[mx4_ctx], str);
    int mx5_ctx = (cm->word_hash ^ (cm->word_hash >> 9)) & 511;
    float m5 = mixer_mix(&cm->mx5[mx5_ctx], str);
    int mlen_bucket = 0;
    if (cm->match.active) {
        int ml = cm->match.mlen > 256 ? 256 : (int)cm->match.mlen;
        mlen_bucket = (ml < 5) ? 1 : (ml < 17) ? 2 : 3;
    }
    int mx6_ctx = ((cm->line_pos < 8 ? 1 : 0) << 7) | (mlen_bucket << 5) | (char_class(cm->prev[0]) << 3) | bp;
    float m6 = mixer_mix(&cm->mx6[mx6_ctx], str);
    int lp_b = 0; { int lp = cm->line_pos & 0xFF; lp_b = (lp<6)?0:(lp<18)?1:(lp<36)?2:(lp<72)?3:4; }
    float m7 = mixer_mix(&cm->mx7[(lp_b << 4) | (bp << 1) | (cm->match.active ? 1 : 0)], str);
    /* mx8: word_length(8) × bp(8) × char_class(4) × match(2) = 512 */
    int wl8 = cm->word_length;
    int wl8_b = wl8<2?0:wl8<4?1:wl8<6?2:wl8<10?3:wl8<16?4:wl8<30?5:wl8<60?6:7;
    int cc8 = (cm->prev[0]>='a'&&cm->prev[0]<='z')?0:(cm->prev[0]>='A'&&cm->prev[0]<='Z')?1:(cm->prev[0]>='0'&&cm->prev[0]<='9')?2:3;
    int mx8_ctx = (cm->match.active?1:0)*256 + wl8_b*32 + bp*4 + cc8;
    float m8 = mixer_mix(&cm->mx8[mx8_ctx], str);
    float s4 = stretch(m4); float s1 = stretch(m1), s8 = stretch(m8); float s6 = stretch(m6);
    float cross = s1 * s8 * 0.006f + s4 * s8 * 0.003f + s1 * s6 * 0.004f;
    float mixed = squash((s1*7 + stretch(m2) + stretch(m3) + s4*3 + stretch(m5) + stretch(m6)*4 + stretch(m7)*2 + s8*8 + cross) / 27.0f);
    
    uint16_t mp = (uint16_t)(mixed * PROB_MAX);
    if (mp < 1) mp = 1; if (mp > PROB_MAX-1) mp = PROB_MAX-1;
    if (raw_mp) *raw_mp = mp;
    
    /* APM: second SSE with different context (match state) */
    int apm_ctx = ((cm->match.active ? 1 : 0) << 11 | (cm->prev[0] >> 5) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 7)) & (SSE_CTXS-1);
    uint16_t apm_p = sse_map(&cm->apm, apm_ctx, mp);
    if (apm_p < 1) apm_p = 1; if (apm_p > PROB_MAX-1) apm_p = PROB_MAX-1;
    
    /* Blend: 0 SSE + 5 APM + 2 APM2 + 25 mixer = 32 */
    int apm2_ctx = ((cm->prev[0] >> 4) << 7 | (cm->partial & 0xF) << 3 | bp) & (SSE_CTXS-1);
    uint16_t apm2_p = sse_map(&cm->apm2, apm2_ctx, apm_p);
    if (apm2_p < 1) apm2_p = 1; if (apm2_p > PROB_MAX-1) apm2_p = PROB_MAX-1;
    int apm3_ctx = ((cm->prev[0] >> 3) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 6)) & (SSE_CTXS-1);
    uint16_t apm3_p = sse_map(&cm->apm3, apm3_ctx, apm2_p);
    if (apm3_p < 1) apm3_p = 1; if (apm3_p > PROB_MAX-1) apm3_p = PROB_MAX-1;
    int par_ctx = ((cm->word_hash & 0x1F) << 7 | (cm->partial & 0xF) << 3 | bp) & (SSE_CTXS-1);
    uint16_t par_p = sse_map(&cm->apm_par, par_ctx, mp);
    if (par_p < 1) par_p = 1; if (par_p > PROB_MAX-1) par_p = PROB_MAX-1;
    uint16_t final = (apm2_p * 1 + apm3_p * 2 + par_p * 2 + mp * 56) / 61;
    if (final < 1) final = 1; if (final > PROB_MAX-1) final = PROB_MAX-1;
    return final;
}

static void cm_update(cm_t *cm, uint32_t pos, int bp, int bit,
                      float *str, uint16_t mp) {
    uint32_t ctx[N_MODELS];
    cm_contexts(cm, pos, bp, ctx);
    
    smap_update(&cm->o0, ctx[0], bit);
    smap_update(&cm->o1, ctx[1], bit);
    smap_update(&cm->o2, ctx[2], bit);
    smap_update(&cm->o3, ctx[3], bit);
    smap_update(&cm->o4, ctx[4], bit);
    smap_update(&cm->o5, ctx[5], bit);
    smap_update(&cm->o6, ctx[6], bit);
    smap_update(&cm->o7, ctx[7], bit);
    smap_update(&cm->word, ctx[8], bit);
    smap_update(&cm->sparse13, ctx[9], bit);
    smap_update(&cm->sparse14, ctx[10], bit);
    smap_update(&cm->sparse24, ctx[11], bit);
    smap_update(&cm->charclass, ctx[12], bit);
    smap_update(&cm->o13, ctx[13], bit);
    smap_update(&cm->indirect, ctx[14], bit);
    smap_update(&cm->o2_word, ctx[15], bit);
    smap_update(&cm->o11, ctx[16], bit);
    smap_update(&cm->o9, ctx[17], bit);
    smap_update(&cm->o12, ctx[18], bit);
    smap_update(&cm->o8, ctx[19], bit);
    smap_update(&cm->word2, ctx[20], bit);
    smap_update(&cm->o14, ctx[21], bit);
    smap_update(&cm->word_cc, ctx[22], bit);
    smap_update(&cm->o1_cc, ctx[23], bit);
    smap_update(&cm->word_len, ctx[24], bit);
    smap_update(&cm->prevword_byte, ctx[25], bit);
    smap_update(&cm->upper2, ctx[26], bit);
    smap_update(&cm->o10, ctx[27], bit);
    smap_update(&cm->word3, ctx[28], bit);
    smap_update(&cm->word4, ctx[29], bit);
    { uint32_t run_ctx = h32(((uint32_t)cm->prev[0] << 8) | cm->run_length) ^ cm->partial;
      smap_update(&cm->run, run_ctx, bit); }
    smap_update(&cm->cc_seq3, ctx[30], bit);
    /* Match StateMap update */
    if (cm->match.active && cm->match.mpos < pos) {
        int ml = cm->match.mlen > 256 ? 256 : (int)cm->match.mlen;
        int pred_bit = (cm->match.data[cm->match.mpos] >> (7 - bp)) & 1;
        int ml_b = ml < 4 ? ml : ml < 8 ? 4 : ml < 16 ? 5 : ml < 32 ? 6 : 7;
        int mcc = (cm->prev[0] >= 'a' && cm->prev[0] <= 'z') ? 0 : (cm->prev[0] >= 'A' && cm->prev[0] <= 'Z') ? 1 : (cm->prev[0] >= '0' && cm->prev[0] <= '9') ? 2 : 3;
        uint32_t msm_ctx = (pred_bit << 15) | (ml_b << 12) | (mcc << 10) | ((cm->prev[0] >> 6) << 8) | (cm->partial << 4) | bp;
        smap_update(&cm->matchsm, msm_ctx, bit);
    }
    smap_update(&cm->word_boundary, ctx[31], bit);
    smap_update(&cm->wind, ctx[32], bit);
    smap_update(&cm->o3ind, ctx[33], bit);
    smap_update(&cm->colmod, ctx[34], bit);
    smap_update(&cm->colmod2, ctx[35], bit);
    smap_update(&cm->colmod3, ctx[36], bit);
    smap_update(&cm->colmod4, ctx[37], bit);
    smap_update(&cm->colmod5, ctx[38], bit);
    smap_update(&cm->wlenmod, ctx[39], bit);
    smap_update(&cm->sentmod, ctx[40], bit);
    smap_update(&cm->wind2, ctx[41], bit);
    {uint32_t nc; if(bp>=4) nc=h32(((uint32_t)cm->partial<<8)|cm->prev[0])^((uint32_t)cm->prev[1]<<16); else nc=h32(((uint32_t)cm->prev[0]<<12)|((uint32_t)cm->prev[1]>>4<<4)|bp); smap_update(&cm->nibcross,nc,bit);}
    {int wp=cm->word_pos; int wpb=(wp<2)?wp:(wp<5)?2:(wp<10)?3:4;
     smap_update(&cm->wposmod,h32(((uint32_t)wpb<<16)|((uint32_t)cm->prev[0]<<8)|cm->prev[1])^(cm->partial<<20),bit);}
    {uint32_t vc=h32(((cm->vc_history&0xFF)<<8)|cm->prev[0])^(cm->partial<<16);
     smap_update(&cm->vcmod,vc,bit);
     smap_update(&cm->vcmod2,h32(((cm->vc_history&0xFF)<<16)|(cm->word_hash&0xFFFF))^(cm->partial<<24),bit);}
    {int sb=(cm->syl_count<4)?cm->syl_count:4;
     smap_update(&cm->sylmod,h32(((uint32_t)sb<<16)|((uint32_t)cm->prev[0]<<8)|cm->prev[1])^(cm->partial<<20),bit);}
    {smap_update(&cm->casemod,h32(((cm->case_history&0xFF)<<8)|cm->prev[0])^(cm->partial<<16),bit);}
    {smap_update(&cm->punctmod,h32(((cm->punct_history&0xFFF)<<8)|cm->prev[0])^(cm->partial<<20),bit);}
    /* bigrammod (51) */
    { int wp_b = (cm->word_pos < 2) ? cm->word_pos : (cm->word_pos < 5) ? 2 : 3;
      uint32_t bg_ctx = h32(((uint32_t)cm->prev[0] << 16) | ((uint32_t)cm->prev[1] << 8) | (wp_b << 4) | bp);
      smap_update(&cm->bigrammod, bg_ctx, bit); }
    /* triwordmod (52) */
    { uint32_t tw_ctx = h32(((uint32_t)cm->prev[0] << 16) | ((uint32_t)cm->prev[1] << 8) | cm->prev[2]) ^ (cm->word_hash << 4) ^ (cm->partial << 24);
      smap_update(&cm->triwordmod, tw_ctx, bit); }
    /* sparsesm (53) */
    if (cm->smatch_active && cm->smatch_pos < pos) {
        int pred = (cm->match.data[cm->smatch_pos] >> (7 - bp)) & 1;
        int slen = cm->smatch_len > 64 ? 64 : cm->smatch_len;
        int sl_b = slen < 4 ? slen : slen < 8 ? 4 : slen < 16 ? 5 : slen < 32 ? 6 : 7;
        uint32_t ssm_ctx = (pred << 13) | (sl_b << 10) | ((cm->prev[0] >> 5) << 7) | (cm->partial << 3) | bp;
        smap_update(&cm->sparsesm, ssm_ctx, bit);
    }
    /* digram (54) */
    { uint16_t bg = ((uint16_t)cm->prev[0] << 8) | cm->partial;
      int freq = cm->digram_count[(cm->prev[1] << 8) | cm->prev[0]];
      int fb = freq < 2 ? 0 : freq < 8 ? 1 : freq < 32 ? 2 : 3;
      uint32_t dg_ctx = h32((fb << 16) | bg);
      smap_update(&cm->digram, dg_ctx, bit); }
    /* errmod (55) */
    { uint32_t eh_ctx = h32((cm->err_history << 16) | ((uint16_t)cm->prev[0] << 8) | cm->partial);
      smap_update(&cm->errmod, eh_ctx, bit);
      int predicted = (mp > (PROB_MAX/2)) ? 1 : 0;
      int err = (predicted != bit) ? 1 : 0;
      cm->err_history = ((cm->err_history << 1) | err) & 0xFF; }
    /* gapmod (56) */
    { uint32_t gap_ctx = h32(((uint32_t)(cm->prev[0] ^ cm->prev[1]) << 8) | cm->prev[2]) ^ (cm->partial << 20);
      smap_update(&cm->gapmod, gap_ctx, bit); }
    /* cimod (57) */
    { uint8_t p0 = cm->prev[0], p1 = cm->prev[1], p2 = cm->prev[2];
      if (p0 >= 'A' && p0 <= 'Z') p0 |= 0x20;
      if (p1 >= 'A' && p1 <= 'Z') p1 |= 0x20;
      if (p2 >= 'A' && p2 <= 'Z') p2 |= 0x20;
      uint32_t ci_ctx = h32(((uint32_t)p0 << 16) | ((uint32_t)p1 << 8) | p2) ^ (cm->partial << 20);
      smap_update(&cm->cimod, ci_ctx, bit); }
    /* o4-indirect update */
    { uint32_t o4h = h32(((uint32_t)cm->prev[3]<<24)|((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx4_size - 1);
      smap_update(&cm->o4ind, h32(((uint32_t)cm->ictx4[o4h] << 8) | cm->partial), bit); }
    { uint32_t o5h = h32(((uint32_t)cm->prev[4]<<24)|((uint32_t)cm->prev[3]<<16)|((uint32_t)cm->prev[2]<<8)|cm->prev[1]) ^ h32(cm->prev[0]);
      o5h &= (cm->ictx6_size - 1);
      smap_update(&cm->o5ind, h32(((uint32_t)cm->ictx6[o5h] << 8) | cm->partial), bit); }
    { uint32_t o6h = h32(((uint32_t)cm->prev[5]<<24)|((uint32_t)cm->prev[4]<<16)|((uint32_t)cm->prev[3]<<8)|cm->prev[2]) ^ h32(((uint32_t)cm->prev[1]<<8)|cm->prev[0]);
      o6h &= (cm->ictx7_size - 1);
      smap_update(&cm->o6ind, h32(((uint32_t)cm->ictx7[o6h] << 8) | cm->partial), bit); }
    { uint32_t o3h2i = h32(((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx8_size - 1);
      smap_update(&cm->o3ind2, h32(((uint32_t)cm->ictx8[o3h2i] << 11) | ((uint32_t)char_class(cm->prev[0]) << 8) | cm->partial), bit); }
    if(bp==7){uint8_t lc=cm->prev[0]|0x20;
     if((lc>='a'&&lc<='z')||cm->prev[0]=='\'') cm->word_pos++; else cm->word_pos=0;
     int vc=0; if(lc=='a'||lc=='e'||lc=='i'||lc=='o'||lc=='u') vc=1; else if(lc>='a'&&lc<='z') vc=2;
     cm->vc_history=(cm->vc_history<<2)|vc;
     int iv=(vc==1); if(!iv&&cm->last_was_vowel) cm->syl_count++;
     if(vc==0){cm->syl_count=0;cm->last_was_vowel=0;} else cm->last_was_vowel=iv;
     int cv=0; if(cm->prev[0]>='A'&&cm->prev[0]<='Z') cv=1; else if(cm->prev[0]>='a'&&cm->prev[0]<='z') cv=2;
     cm->case_history=(cm->case_history<<2)|cv;
     int pv; if((cm->prev[0]>='a'&&cm->prev[0]<='z')||(cm->prev[0]>='A'&&cm->prev[0]<='Z')) pv=0;
     else if(cm->prev[0]>='0'&&cm->prev[0]<='9') pv=1;
     else if(cm->prev[0]==' '||cm->prev[0]=='\n') pv=2; else pv=3;
     cm->punct_history=(cm->punct_history<<2)|pv;}
    
    /* Adaptive mixer learning rate: fast early, slow later */
    /* Smooth exponential decay: lr = 0.05 / (1 + total_bits/20000) */
    float lr = 0.002f + 0.013f / (1.0f + (float)cm->total_bits / 5000.0f);
    if (lr < 0.002f) lr = 0.002f;
    mixer_learn(&cm->mx1[(cm->prev[0] << 4) | (cm->prev[1] >> 6 << 3) | bp], str, bit, lr * 1.5f);
    mixer_learn(&cm->mx2[((char_class(cm->prev[0]) << 4) | (bp << 1) | (cm->prev[1] >> 7)) & 127], str, bit, lr);
    mixer_learn(&cm->mx3[bp], str, bit, lr * 0.7f);
    {
        int mx4_ctx = (((cm->prev[0] >> 4) << 5) | ((cm->prev[1] >> 4) << 1) | bp/4) & 1023;
        mixer_learn(&cm->mx4[mx4_ctx], str, bit, lr);
        int mx5_ctx = (cm->word_hash ^ (cm->word_hash >> 9)) & 511;
        mixer_learn(&cm->mx5[mx5_ctx], str, bit, lr * 3.0f);
        int mlen_bucket = 0;
        if (cm->match.active) {
            int ml = cm->match.mlen > 256 ? 256 : (int)cm->match.mlen;
            mlen_bucket = (ml < 5) ? 1 : (ml < 17) ? 2 : 3;
        }
        int mx6_ctx = ((cm->line_pos < 8 ? 1 : 0) << 7) | (mlen_bucket << 5) | (char_class(cm->prev[0]) << 3) | bp;
        mixer_learn(&cm->mx6[mx6_ctx], str, bit, lr * 0.5f);
    { int lp = cm->line_pos & 0xFF; int lp_b = (lp<6)?0:(lp<18)?1:(lp<36)?2:(lp<72)?3:4;
      mixer_learn(&cm->mx7[(lp_b << 4) | (bp << 1) | (cm->match.active ? 1 : 0)], str, bit, lr * 0.5f); }
    { int wl8 = cm->word_length;
      int wl8_b = wl8<2?0:wl8<4?1:wl8<6?2:wl8<10?3:wl8<16?4:wl8<30?5:wl8<60?6:7;
      int cc8 = (cm->prev[0]>='a'&&cm->prev[0]<='z')?0:(cm->prev[0]>='A'&&cm->prev[0]<='Z')?1:(cm->prev[0]>='0'&&cm->prev[0]<='9')?2:3;
      int mx8_ctx = (cm->match.active?1:0)*256 + wl8_b*32 + bp*4 + cc8;
      mixer_learn(&cm->mx8[mx8_ctx], str, bit, lr * 0.5f); }
    }
    cm->total_bits++;
    sse_update(&cm->apm, ((cm->match.active ? 1 : 0) << 11 | (cm->prev[0] >> 5) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 7)) & (SSE_CTXS-1), mp, bit);
    sse_update(&cm->apm2, ((cm->prev[0] >> 4) << 7 | (cm->partial & 0xF) << 3 | bp) & (SSE_CTXS-1), mp, bit);
    sse_update(&cm->apm3, ((cm->prev[0] >> 3) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 6)) & (SSE_CTXS-1), mp, bit);
    sse_update(&cm->apm_par, ((cm->word_hash & 0x1F) << 7 | (cm->partial & 0xF) << 3 | bp) & (SSE_CTXS-1), mp, bit);
    
    cm->partial = (cm->partial << 1) | bit;
}

static void cm_byte_done(cm_t *cm, uint8_t byte) {
    { uint8_t cc = char_class(byte);
      if (cc == cm->run_class) cm->run_len++;
      else { cm->run_len = 1; cm->run_class = cc; }
    }
    cm->sent_pos++;
    if (byte == '.' || byte == '!' || byte == '?') cm->sent_pos = 0;
    if (byte == 10) { cm->last_line_len = cm->line_pos & 0xFF; cm->line_pos = 0; }
    else { cm->line_pos++; }
    { uint32_t wh5 = cm->word_hash & (cm->ictx5_size - 1); cm->ictx5[wh5] = (uint16_t)((cm->prev[0] << 8) | byte); }
    uint32_t o2h = h32(((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx_size - 1);
    cm->ictx[o2h] = byte;
    { uint32_t wh2 = cm->word_hash & (cm->ictx2_size - 1); cm->ictx2[wh2] = byte; }
    { uint32_t o3h2 = h32(((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx3_size - 1); cm->ictx3[o3h2] = byte; }
    { uint32_t o4h2 = h32(((uint32_t)cm->prev[3]<<24)|((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx4_size - 1); cm->ictx4[o4h2] = (uint16_t)((cm->prev[0] << 8) | byte); }
    { uint32_t o5h2 = h32(((uint32_t)cm->prev[4]<<24)|((uint32_t)cm->prev[3]<<16)|((uint32_t)cm->prev[2]<<8)|cm->prev[1]) ^ h32(cm->prev[0]);
      o5h2 &= (cm->ictx6_size - 1); cm->ictx6[o5h2] = byte; }
    { uint32_t o6h2 = h32(((uint32_t)cm->prev[5]<<24)|((uint32_t)cm->prev[4]<<16)|((uint32_t)cm->prev[3]<<8)|cm->prev[2]) ^ h32(((uint32_t)cm->prev[1]<<8)|cm->prev[0]);
      o6h2 &= (cm->ictx7_size - 1); cm->ictx7[o6h2] = byte; }
    { uint32_t o3h3 = h32(((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx8_size - 1); cm->ictx8[o3h3] = byte; }

    if (byte == cm->prev[0] && cm->run_length < 255)
        cm->run_length++;
    else
        cm->run_length = 1;
    /* Sparse match: hash prev[1..3] */
    if (cm->byte_pos >= 4) {
        uint32_t sh = h32(((uint32_t)cm->prev[1] << 16) | ((uint32_t)cm->prev[2] << 8) | cm->prev[3]);
        uint32_t si = sh & ((1<<18)-1);
        uint32_t spos = cm->smatch_tab[si];
        if (!cm->smatch_active && spos != 0xFFFFFFFF && spos + 3 < cm->byte_pos) {
            if (cm->match.data[spos] == byte) {
                cm->smatch_active = 1;
                cm->smatch_pos = spos + 1;
                cm->smatch_len = 4;
            }
        } else if (cm->smatch_active) {
            if (cm->match.data[cm->smatch_pos - 1] == byte) {
                cm->smatch_len++;
            } else {
                cm->smatch_active = 0;
                cm->smatch_len = 0;
            }
        }
        cm->smatch_tab[si] = cm->byte_pos;
    }
    cm->byte_pos++;
    /* Digram frequency update */
    { uint16_t bg = ((uint16_t)cm->prev[0] << 8) | byte; if (cm->digram_count[bg] < 65535) cm->digram_count[bg]++; }
    for (int j = 13; j > 0; j--) cm->prev[j] = cm->prev[j-1];
    cm->prev[0] = byte;
    cm->partial = 1;
    if ((byte>='a'&&byte<='z')||(byte>='A'&&byte<='Z')||byte=='\'') {
        cm->word_hash = cm->word_hash * 31 + byte;
        if (cm->word_length < 255) cm->word_length++;
    } else {
        if (cm->word_hash) {
            cm->prev3_word_hash = cm->prev2_word_hash;
            cm->prev2_word_hash = cm->prev_word_hash;
            cm->prev_word_hash = cm->word_hash;
        }
        cm->word_hash = 0;
        cm->word_length = 0;
    }
}

/* ── Compress / Decompress ─────────────────────────────────────── */

size_t mcx_cm_compress(uint8_t *dst, size_t cap,
                          const uint8_t *src, size_t size) {
    if (cap < 8) return 0;
    dst[0]=size&0xFF; dst[1]=(size>>8)&0xFF;
    dst[2]=(size>>16)&0xFF; dst[3]=(size>>24)&0xFF;
    
    cm_t *cmp = (cm_t*)calloc(1, sizeof(cm_t)); if(!cmp) return 0;
    cm_init(cmp, src, size);
    rcenc_t rc; rcenc_init(&rc, dst+4, cap-4);
    float str[N_MODELS];
    
    for (size_t i = 0; i < size; i++) {
        match_update(&cmp->match, (uint32_t)i);
        cmp->partial = 1;
        uint8_t byte = src[i];
        
        for (int bit = 7; bit >= 0; bit--) {
            int b = (byte >> bit) & 1;
            int bp = 7 - bit;
            
            uint16_t mp;
            uint16_t prob = cm_predict(cmp, (uint32_t)i, bp, str, &mp);
            rcenc_bit(&rc, prob, b);
            
            cm_update(cmp, (uint32_t)i, bp, b, str, mp);
        }
        cm_byte_done(cmp, byte);
    }
    
    size_t comp = rcenc_flush(&rc) + 4;
    cm_free(cmp); free(cmp);
    return comp;
}

size_t mcx_cm_decompress(uint8_t *dst, size_t cap,
                            const uint8_t *src, size_t src_size) {
    if (src_size < 4) return 0;
    size_t orig = (size_t)src[0] | ((size_t)src[1]<<8) |
                  ((size_t)src[2]<<16) | ((size_t)src[3]<<24);
    if (orig > cap) return 0;
    
    cm_t *cmp = (cm_t*)calloc(1, sizeof(cm_t)); if(!cmp) return 0;
    cm_init(cmp, dst, orig);
    rcdec_t rc; rcdec_init(&rc, src+4, src_size-4);
    float str[N_MODELS];
    
    for (size_t i = 0; i < orig; i++) {
        if (i > 0) match_update(&cmp->match, (uint32_t)i);
        cmp->partial = 1;
        uint8_t byte = 0;
        
        for (int bit = 7; bit >= 0; bit--) {
            int bp = 7 - bit;
            uint16_t mp;
            uint16_t prob = cm_predict(cmp, (uint32_t)i, bp, str, &mp);
            int b = rcdec_bit(&rc, prob);
            
            cm_update(cmp, (uint32_t)i, bp, b, str, mp);
            byte = (byte << 1) | b;
        }
        dst[i] = byte;
        cm_byte_done(cmp, byte);
    }
    
    cm_free(cmp); free(cmp);
    return orig;
}

/* ── Main ──────────────────────────────────────────────────────── */

