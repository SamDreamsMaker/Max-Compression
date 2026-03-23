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

/* Just copy exp014 and replace model_t with statemap_t */
/* For brevity, include the working exp014 and just swap the model */

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
    uint32_t n;    /* table size (power of 2) */
    uint32_t mask; /* n-1 for fast masking */
    int rate_n;    /* numerator for adaptation rate */
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
    c &= s->mask;
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

/* ── Mixer ─────────────────────────────────────────────────────── */

#define MAX_INPUTS 48  /* room for future models */

typedef struct {
    float w[MAX_INPUTS];
    float bias;
    int n;
} mixer_t;

static void mixer_init(mixer_t *mx, int n) {
    mx->n = n;
    for (int i = 0; i < n; i++) mx->w[i] = 1.0f / n;
    mx->bias = 0.0f;
}

static float mixer_mix(mixer_t *mx, float *s) {
    float sum = 0;
    const float *w = mx->w;
    int i = 0;
    for (; i + 3 < mx->n; i += 4)
        sum += w[i]*s[i] + w[i+1]*s[i+1] + w[i+2]*s[i+2] + w[i+3]*s[i+3];
    for (; i < mx->n; i++) sum += w[i] * s[i];
    sum += mx->bias; sum *= 1.15f;
    float r = squash(sum);
    if (r < 0.001f) r = 0.001f;
    if (r > 0.999f) r = 0.999f;
    return r;
}

static void mixer_learn(mixer_t *mx, float *s, int bit, float lr) {
    float sum = 0;
    for (int i = 0; i < mx->n; i++) sum += mx->w[i] * s[i];
    sum += mx->bias; sum *= 1.15f;
    float err = (1.0f - bit) - squash(sum);
    for (int i = 0; i < mx->n; i++)
        mx->w[i] += lr * err * s[i];
    mx->bias += lr * err;
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
    /* Logarithmic confidence curve */
    int delta;
    if (len < 4) delta = len * 250;         /* 0-1000 */
    else if (len < 12) delta = 1000 + (len-4) * 100; /* 1000-1800 */
    else if (len < 32) delta = 1800 + (len-12) * 8;  /* 1800-1960 */
    else delta = 1960 + (len-32) * 1;       /* very slow ramp */
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
            /* Count actual backward match length */
            uint32_t len = 4;
            while (len < pos && ref8 >= len + 1 &&
                   m->data[ref8-1-len] == m->data[pos-1-len]) len++;
            m->active = 1; m->mpos = ref8; m->mlen = len;
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
                uint32_t len = 4;
                while (len < pos && ref4 >= len + 1 &&
                       m->data[ref4-1-len] == m->data[pos-1-len]) len++;
                m->active = 1; m->mpos = ref4; m->mlen = len;
            }
            m->tab4[h4] = pos;
        }
    }
}

/* ── SSE ───────────────────────────────────────────────────────── */

#define SSE_CTXS 4096
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
    int c = ctx % SSE_CTXS;
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
    /* Count-based rate: fast adaptation for new contexts */
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

#define N_MODELS 44

typedef struct {
    smap_t o0, o1, o2, o3, o4, o5, o6, o7;
    smap_t word, sparse13, sparse14, sparse24;
    smap_t charclass, o13; /* nibble slot reused for o13 */
    smap_t indirect, o2_word, o11; /* gap15 slot reused for o11 */
    smap_t o9, o12; /* delta/o1_nibble slots reused */
    smap_t o8;              /* order-8 */
    smap_t word2;           /* word order-2 (two consecutive words) */
    smap_t o14;             /* sparse024 slot reused for o14 */
    smap_t word_cc;         /* word hash × char class */
    smap_t o1_cc;           /* order-1 × char class sequence */
    smap_t word_len;        /* word hash × word length */
    smap_t prevword_byte;   /* prev word hash × current byte */
    smap_t upper2;          /* upper nibble order-2 */
    smap_t word3;           /* word order-3 (three consecutive words) */
    smap_t word4;           /* word order-4 */
    smap_t run;             /* byte × run length context */
    smap_t o10;             /* o4_cc slot reused for o10 */
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
    match_t match;
    sse_t apm;  /* second-stage APM with different context */
    sse_t apm2; /* third-stage APM with prev>>4 context */
    sse_t apm3; /* fourth-stage APM — deepest in chain */
    mixer_t mx1[4096], mx2[64], mx3[8], mx4[1024], mx5[512], mx6[128], mx7[64];
    float lr;
    uint8_t prev[14];
    uint32_t word_hash;
    uint32_t prev_word_hash; /* hash of previous word */
    uint32_t prev2_word_hash; /* hash of word before previous */
    uint32_t prev3_word_hash; /* hash of 3 words ago */
    uint8_t word_length;     /* current word length */
    uint8_t run_length;      /* consecutive same byte count */
    uint8_t partial;
    uint8_t *ictx;
    uint8_t *ictx2;  /* word-indirect */
    uint8_t *ictx3;  /* o3-indirect */
    uint32_t ictx_size;
    uint32_t ictx2_size;
    uint32_t ictx3_size;
    uint16_t *ictx5;
    uint32_t ictx5_size;
    uint32_t total_bits; /* total bits processed */
    uint32_t line_pos;      /* position within current line */
    uint32_t last_line_len;
    uint32_t run_len;
    uint8_t run_class;
    uint32_t sent_pos;  /* distance from last .!? */ /* length of previous line */
    uint16_t last_apm_p; /* stored for chained APM update */
} cm_t;

static void cm_init(cm_t *cm, const uint8_t *data, size_t data_size) {
    memset(cm, 0, sizeof(cm_t));
    /* Scale table sizes: ~1.5GB budget at log=25, ~100MB at log=22 */
    /* 26 smaps at 1<<25 × 4B = 26×128MB = 3.3GB — too much for most systems */
    /* Use 1<<23 as a safe default, bump to 1<<25 only for small files */
    int hi_log = 21; /* high-order models (o2-o14) — default for large files */
    int lo_log = 20; /* word/misc models */
    if (data_size <= 256*1024) { hi_log = 24; lo_log = 23; }
    else if (data_size <= 2*1024*1024) { hi_log = 23; lo_log = 22; } /* ~700MB */
    else if (data_size <= 16*1024*1024) { hi_log = 22; lo_log = 21; } /* ~180MB */
    smap_init(&cm->o0, 512);
    smap_init(&cm->o1, 256*256);
    smap_init(&cm->o2, 1<<hi_log);
    smap_init(&cm->o3, 1<<hi_log);
    smap_init(&cm->o4, 1<<hi_log);
    smap_init(&cm->o5, 1<<hi_log);
    smap_init(&cm->o6, 1<<hi_log);
    smap_init(&cm->o7, 1<<hi_log);
    smap_init(&cm->word, 1<<lo_log);
    smap_init(&cm->sparse13, 1<<lo_log);
    smap_init(&cm->sparse14, 1<<lo_log);
    smap_init(&cm->sparse24, 1<<lo_log);
    smap_init(&cm->charclass, 1<<lo_log);
    smap_init(&cm->o13, 1<<hi_log); cm->o13.rate_n = 400;
    smap_init(&cm->indirect, 1<<lo_log);
    smap_init(&cm->o2_word, 1<<lo_log);
    smap_init(&cm->o11, 1<<hi_log); cm->o11.rate_n = 400;
    smap_init(&cm->o9, 1<<hi_log); cm->o9.rate_n = 400;
    smap_init(&cm->o12, 1<<hi_log); cm->o12.rate_n = 400;
    smap_init(&cm->o8, 1<<hi_log); cm->o8.rate_n = 400;
    smap_init(&cm->word2, 1<<lo_log);
    smap_init(&cm->o14, 1<<hi_log); cm->o14.rate_n = 400;
    smap_init(&cm->word_cc, 1<<lo_log);
    smap_init(&cm->o1_cc, 1<<lo_log);
    smap_init(&cm->word_len, 1<<lo_log);
    smap_init(&cm->prevword_byte, 1<<lo_log);
    smap_init(&cm->upper2, 1<<lo_log);
    smap_init(&cm->word3, 1<<lo_log);
    smap_init(&cm->word4, 1<<lo_log);
    smap_init(&cm->run, 1<<lo_log);
    smap_init(&cm->o10, 1<<hi_log); cm->o10.rate_n = 400;
    smap_init(&cm->cc_seq3, 1<<lo_log);
    smap_init(&cm->word_boundary, 1<<lo_log); cm->word_boundary.rate_n = 400;
    smap_init(&cm->wind, 1<<lo_log);
    smap_init(&cm->o3ind, 1<<lo_log);
    smap_init(&cm->colmod, 1<<lo_log);
    smap_init(&cm->colmod2, 1<<lo_log);
    smap_init(&cm->colmod3, 1<<lo_log); smap_init(&cm->colmod4, 1<<lo_log); smap_init(&cm->colmod5, 1<<lo_log); smap_init(&cm->wlenmod, 1<<lo_log);
    smap_init(&cm->sentmod, 1<<lo_log);
    smap_init(&cm->wind2, 1<<lo_log);
    match_init(&cm->match, data);
    sse_init(&cm->apm);
    sse_init(&cm->apm2);
    sse_init(&cm->apm3);
    for (int i = 0; i < 4096; i++) mixer_init(&cm->mx1[i], N_MODELS);
    for (int i = 0; i < 64; i++) mixer_init(&cm->mx2[i], N_MODELS);
    for (int i = 0; i < 8; i++) mixer_init(&cm->mx3[i], N_MODELS);
    for (int i = 0; i < 1024; i++) mixer_init(&cm->mx4[i], N_MODELS);
    for (int i = 0; i < 512; i++) mixer_init(&cm->mx5[i], N_MODELS);
    for (int i = 0; i < 128; i++) mixer_init(&cm->mx6[i], N_MODELS);
    for (int i = 0; i < 64; i++) mixer_init(&cm->mx7[i], N_MODELS);
    cm->lr = 0.012f;
    cm->partial = 1;
    cm->ictx_size = 1 << 22;
    cm->ictx = (uint8_t*)calloc(cm->ictx_size, 1);
    cm->ictx2_size = 1 << 22;
    cm->ictx2 = (uint8_t*)calloc(cm->ictx2_size, 1);
    cm->ictx3_size = 1 << 22;
    cm->ictx3 = (uint8_t*)calloc(cm->ictx3_size, 1);
    cm->ictx5_size = 1 << 22;
    cm->ictx5 = (uint16_t*)calloc(cm->ictx5_size, sizeof(uint16_t));
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
    smap_free(&cm->upper2); smap_free(&cm->o10); smap_free(&cm->word3); smap_free(&cm->word4); smap_free(&cm->run);
    smap_free(&cm->cc_seq3);
    smap_free(&cm->word_boundary);
    smap_free(&cm->wind);
    smap_free(&cm->o3ind); smap_free(&cm->colmod); smap_free(&cm->colmod2); smap_free(&cm->colmod3); smap_free(&cm->colmod4); smap_free(&cm->colmod5); smap_free(&cm->wlenmod);
    match_free(&cm->match);
    if (cm->ictx) free(cm->ictx);
    if (cm->ictx2) free(cm->ictx2);
    if (cm->ictx3) free(cm->ictx3);
    if (cm->ictx5) free(cm->ictx5);
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
    /* order-11: hash of prev 11 bytes */
    ctx[16] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<24|p[9]<<16|p[10]<<8)) ^ par;
    /* order-9: hash of prev 9 bytes */
    ctx[17] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<8)) ^ par;
    /* order-12: hash of prev 12 bytes */
    ctx[18] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<24|p[9]<<16|p[10]<<8|p[11])) ^ par;
    /* Order-8 */
    ctx[19] = h32(h32(h0123) ^ h32(((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7])) ^ par;
    /* Word order-2 (current word × previous word) */
    ctx[20] = h32(cm->word_hash ^ cm->prev_word_hash) ^ par;
    /* Sparse: bytes 0,2,4 */
    /* order-14 */
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
    /* Order-4 × char class */
    /* order-10: hash of prev 10 bytes */
    ctx[27] = h32(h32(h0123 ^ ((uint32_t)p[4]<<24|p[5]<<16|p[6]<<8|p[7])) ^ ((uint32_t)p[8]<<16|p[9]<<8)) ^ par;
    ctx[29] = h32(cm->word_hash ^ cm->prev_word_hash ^ cm->prev2_word_hash) ^ par;
    ctx[30] = h32(cm->word_hash ^ cm->prev_word_hash ^ cm->prev2_word_hash ^ cm->prev3_word_hash) ^ par;
    /* char-class 4-gram: 4 consecutive char classes × current byte */
    ctx[31] = h32(((uint32_t)char_class(p[0])<<18)|((uint32_t)char_class(p[1])<<15)|
                  ((uint32_t)char_class(p[2])<<12)|((uint32_t)char_class(p[3])<<9)|
                  ((uint32_t)p[0]<<1)) ^ par;
    /* word boundary: transition type × recent context */
    { int wb = (char_class(p[0]) != char_class(p[1])) ? 1 : 0;
      int wb2 = (char_class(p[1]) != char_class(p[2])) ? 1 : 0;
      ctx[32] = h32(((uint32_t)wb<<17)|((uint32_t)wb2<<16)|((uint32_t)p[0]<<8)|p[1]) ^ par;
    }
    /* word-indirect: word_hash → predicted next byte */
    { uint32_t wh = cm->word_hash & (cm->ictx2_size - 1);
      ctx[33] = h32(((uint32_t)cm->ictx2[wh] << 11) | ((uint32_t)char_class(p[0]) << 8) | par);
    }
    /* o3-indirect: h(p[0],p[1],p[2]) → predicted next byte */
    { uint32_t o3h = h32(((uint32_t)p[2]<<16)|((uint32_t)p[1]<<8)|p[0]) & (cm->ictx3_size - 1);
      ctx[34] = h32(((uint32_t)cm->ictx3[o3h] << 8) | par);
    }
    /* Column model: position within line */
    ctx[35] = h32(((uint32_t)(cm->line_pos & 0xFF) << 11) | ((uint32_t)char_class(p[0]) << 8) | par);
    ctx[36] = h32(((uint32_t)(cm->line_pos & 0xFF) << 16) | ((uint32_t)cm->last_line_len << 8) | par);
    ctx[37] = h32(((uint32_t)(cm->line_pos & 0xFF) << 16) | ((uint32_t)p[0] << 8) | par);
    ctx[38] = h32(((uint32_t)(cm->line_pos & 0xFF) << 8) | par);
    ctx[39] = h32(((uint32_t)(cm->line_pos & 0xFF) << 14) | ((uint32_t)char_class(p[0]) << 11) | ((uint32_t)char_class(p[1]) << 8) | par);
    ctx[40] = h32(((uint32_t)(cm->run_len & 31) << 11) | ((uint32_t)cm->run_class << 8) | par);
    { uint32_t sp = cm->sent_pos;
      int sp_bucket = (sp < 4) ? sp : (sp < 16) ? 4 + (sp>>2) : (sp < 64) ? 8 + (sp>>4) : 12;
      ctx[41] = h32(((uint32_t)sp_bucket << 12) | ((uint32_t)p[0] << 4) | par);
    }
    { uint32_t wh5 = cm->word_hash & (cm->ictx5_size - 1);
      uint16_t w2 = cm->ictx5[wh5];
      ctx[42] = h32(((uint32_t)w2 << 8) | par);
    }
}

static uint16_t cm_predict(cm_t *cm, uint32_t pos, int bp, float *str) {
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
    preds[28] = smap_get(&cm->word3, ctx[29]);
    preds[29] = smap_get(&cm->word4, ctx[30]);
    uint32_t run_ctx = h32(((uint32_t)cm->prev[0] << 8) | cm->run_length) ^ cm->partial;
    preds[30] = smap_get(&cm->run, run_ctx);
    preds[31] = match_predict(&cm->match, pos, bp);
    preds[32] = smap_get(&cm->cc_seq3, ctx[31]);
    preds[33] = smap_get(&cm->word_boundary, ctx[32]);
    preds[34] = smap_get(&cm->wind, ctx[33]);
    preds[35] = smap_get(&cm->o3ind, ctx[34]);
    preds[36] = smap_get(&cm->colmod, ctx[35]);
    preds[37] = smap_get(&cm->colmod2, ctx[36]);
    preds[38] = smap_get(&cm->colmod3, ctx[37]);
    preds[39] = smap_get(&cm->colmod4, ctx[38]);
    preds[40] = smap_get(&cm->colmod5, ctx[39]);
    preds[41] = smap_get(&cm->wlenmod, ctx[40]);
    preds[42] = smap_get(&cm->sentmod, ctx[41]);
    preds[43] = smap_get(&cm->wind2, ctx[42]);
    
    for (int i = 0; i < N_MODELS; i++) {
        if (preds[i] == PROB_HALF) str[i] = 0.0f;
        else str[i] = stretch((float)preds[i] / PROB_MAX);
    }
    
    float m1 = mixer_mix(&cm->mx1[(cm->prev[0] << 4) | (cm->prev[1] >> 6 << 3) | bp], str);
    float m2 = mixer_mix(&cm->mx2[(char_class(cm->prev[0]) << 3) | bp], str);
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
    int mx6_ctx = (mlen_bucket << 5) | (char_class(cm->prev[0]) << 3) | bp;
    float m6 = mixer_mix(&cm->mx6[mx6_ctx], str);
    int mx7_ctx = ((cm->line_pos < 16 ? 0 : cm->line_pos < 40 ? 1 : cm->line_pos < 72 ? 2 : 3) << 4) | (bp << 1) | (cm->match.active ? 1 : 0);
    float m7 = mixer_mix(&cm->mx7[mx7_ctx], str);
    float mixed = squash((stretch(m1)*5 + stretch(m2) + stretch(m3) + stretch(m4)*2 + stretch(m5) + stretch(m6)*4 + stretch(m7)*2) / 16.0f);
    
    uint16_t mp = (uint16_t)(mixed * PROB_MAX);
    if (mp < 1) mp = 1; if (mp > PROB_MAX-1) mp = PROB_MAX-1;
    
    int sse_ctx = ((cm->prev[0] >> 4) << 8 | (cm->prev[1] >> 6) << 6 | (cm->partial & 0xF) << 3 | bp) & (SSE_CTXS-1);
    
    /* APM: second SSE with different context (match state) */
    int apm_ctx = ((cm->match.active ? 1 : 0) << 11 | (cm->prev[0] >> 5) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 7)) & (SSE_CTXS-1);
    uint16_t apm_p = sse_map(&cm->apm, apm_ctx, mp);
    if (apm_p < 1) apm_p = 1; if (apm_p > PROB_MAX-1) apm_p = PROB_MAX-1;
    cm->last_apm_p = apm_p;
    
    /* Blend: 0 SSE + 5 APM + 27 mixer = 32 */
    int apm2_ctx = ((cm->prev[0] >> 4) << 7 | (cm->partial & 0xF) << 3 | bp) & (SSE_CTXS-1);
    uint16_t apm2_p = sse_map(&cm->apm2, apm2_ctx, apm_p);
    if (apm2_p < 1) apm2_p = 1; if (apm2_p > PROB_MAX-1) apm2_p = PROB_MAX-1;
    int apm3_ctx = ((cm->prev[0] >> 3) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 6)) & (SSE_CTXS-1);
    uint16_t apm3_p = sse_map(&cm->apm3, apm3_ctx, apm2_p);
    if (apm3_p < 1) apm3_p = 1; if (apm3_p > PROB_MAX-1) apm3_p = PROB_MAX-1;
    uint16_t final;
    if (cm->match.active) {
        final = (apm_p * 1 + apm2_p * 1 + apm3_p * 1 + mp * 29) / 32;
    } else {
        final = (apm_p * 2 + apm2_p * 2 + apm3_p * 2 + mp * 26) / 32;
    }
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
    smap_update(&cm->word3, ctx[29], bit);
    smap_update(&cm->word4, ctx[30], bit);
    { uint32_t run_ctx = h32(((uint32_t)cm->prev[0] << 8) | cm->run_length) ^ cm->partial;
      smap_update(&cm->run, run_ctx, bit); }
    smap_update(&cm->cc_seq3, ctx[31], bit);
    smap_update(&cm->word_boundary, ctx[32], bit);
    smap_update(&cm->wind, ctx[33], bit);
    smap_update(&cm->o3ind, ctx[34], bit);
    smap_update(&cm->colmod, ctx[35], bit);
    smap_update(&cm->colmod2, ctx[36], bit);
    smap_update(&cm->colmod3, ctx[37], bit);
    smap_update(&cm->colmod4, ctx[38], bit);
    smap_update(&cm->colmod5, ctx[39], bit);
    smap_update(&cm->wlenmod, ctx[40], bit);
    smap_update(&cm->sentmod, ctx[41], bit);
    smap_update(&cm->wind2, ctx[42], bit);
    
    /* Adaptive mixer learning rate: fast early, slow later */
    /* Smooth exponential decay: lr = 0.05 / (1 + total_bits/20000) */
    float lr = 0.002f + 0.024f / (1.0f + (float)cm->total_bits / 4000.0f);
    if (lr < 0.002f) lr = 0.002f;
    mixer_learn(&cm->mx1[(cm->prev[0] << 4) | (cm->prev[1] >> 6 << 3) | bp], str, bit, lr);
    mixer_learn(&cm->mx2[(char_class(cm->prev[0]) << 3) | bp], str, bit, lr);
    mixer_learn(&cm->mx3[bp], str, bit, lr);
    {
        int mx4_ctx = (((cm->prev[0] >> 4) << 5) | ((cm->prev[1] >> 4) << 1) | bp/4) & 1023;
        mixer_learn(&cm->mx4[mx4_ctx], str, bit, lr);
        int mx5_ctx = (cm->word_hash ^ (cm->word_hash >> 9)) & 511;
        mixer_learn(&cm->mx5[mx5_ctx], str, bit, lr);
        int mlen_bucket = 0;
        if (cm->match.active) {
            int ml = cm->match.mlen > 256 ? 256 : (int)cm->match.mlen;
            mlen_bucket = (ml < 5) ? 1 : (ml < 17) ? 2 : 3;
        }
        int mx6_ctx = (mlen_bucket << 5) | (char_class(cm->prev[0]) << 3) | bp;
        mixer_learn(&cm->mx6[mx6_ctx], str, bit, lr * 0.5f);
        int mx7_ctx = ((cm->line_pos < 16 ? 0 : cm->line_pos < 40 ? 1 : cm->line_pos < 72 ? 2 : 3) << 4) | (bp << 1) | (cm->match.active ? 1 : 0);
        mixer_learn(&cm->mx7[mx7_ctx], str, bit, lr * 0.5f);
    }
    cm->total_bits++;
    sse_update(&cm->apm, ((cm->match.active ? 1 : 0) << 11 | (cm->prev[0] >> 5) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 7)) & (SSE_CTXS-1), mp, bit);
    sse_update(&cm->apm2, ((cm->prev[0] >> 4) << 7 | (cm->partial & 0xF) << 3 | bp) & (SSE_CTXS-1), mp, bit);
    sse_update(&cm->apm3, ((cm->prev[0] >> 3) << 8 | (cm->partial & 0xF) << 4 | bp << 1 | (cm->prev[1] >> 6)) & (SSE_CTXS-1), mp, bit);
    
    cm->partial = (cm->partial << 1) | bit;
}

static void cm_byte_done(cm_t *cm, uint8_t byte) {
    { uint8_t cc = char_class(byte);
      if (cc == cm->run_class) cm->run_len++;
      else { cm->run_len = 1; cm->run_class = cc; }
    }
    if (byte == 10) { cm->last_line_len = cm->line_pos & 0xFF; cm->line_pos = 0; }
    else { cm->line_pos++; }
    uint32_t o2h = h32(((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx_size - 1);
    cm->ictx[o2h] = byte;
    cm->sent_pos++;
    if (byte == '.' || byte == '!' || byte == '?') cm->sent_pos = 0;
    { uint32_t wh2 = cm->word_hash & (cm->ictx2_size - 1); cm->ictx2[wh2] = byte; }
    { uint32_t wh5 = cm->word_hash & (cm->ictx5_size - 1); cm->ictx5[wh5] = (uint16_t)((cm->prev[0] << 8) | byte); }
    { uint32_t o3h2 = h32(((uint32_t)cm->prev[2]<<16)|((uint32_t)cm->prev[1]<<8)|cm->prev[0]) & (cm->ictx3_size - 1); cm->ictx3[o3h2] = byte; }
    
    /* Update run length */
    if (byte == cm->prev[0] && cm->run_length < 255)
        cm->run_length++;
    else
        cm->run_length = 1;
    
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

static size_t cm_compress(uint8_t *dst, size_t cap,
                          const uint8_t *src, size_t size) {
    if (cap < 8) return 0;
    dst[0]=size&0xFF; dst[1]=(size>>8)&0xFF;
    dst[2]=(size>>16)&0xFF; dst[3]=(size>>24)&0xFF;
    
    cm_t *cmp = (cm_t*)calloc(1, sizeof(cm_t)); cm_init(cmp, src, size);
    #define cm (*cmp)
    rcenc_t rc; rcenc_init(&rc, dst+4, cap-4);
    float str[N_MODELS];
    
    for (size_t i = 0; i < size; i++) {
        match_update(&cm.match, (uint32_t)i);
        cm.partial = 1;
        uint8_t byte = src[i];
        
        for (int bit = 7; bit >= 0; bit--) {
            int b = (byte >> bit) & 1;
            int bp = 7 - bit;
            
            uint16_t prob = cm_predict(&cm, (uint32_t)i, bp, str);
            rcenc_bit(&rc, prob, b);
            
            float em1 = mixer_mix(&cm.mx1[(cm.prev[0] << 3) | bp], str);
            float em2 = mixer_mix(&cm.mx2[(char_class(cm.prev[0]) << 3) | bp], str);
            float em3 = mixer_mix(&cm.mx3[bp], str);
            int emx4 = (((cm.prev[0] >> 4) << 5) | ((cm.prev[1] >> 4) << 1) | bp/4) & 1023;
            float em4 = mixer_mix(&cm.mx4[emx4], str);
            int emx5 = (cm.word_hash ^ (cm.word_hash >> 8)) & 255;
            float em5 = mixer_mix(&cm.mx5[emx5], str);
            float mixed = (em1 + em2 + em3 + em4 + em5) * 0.2f;
            uint16_t mp = (uint16_t)(mixed * PROB_MAX);
            if (mp < 1) mp = 1; if (mp > PROB_MAX-1) mp = PROB_MAX-1;
            
            cm_update(&cm, (uint32_t)i, bp, b, str, mp);
        }
        cm_byte_done(&cm, byte);
    }
    
    size_t comp = rcenc_flush(&rc) + 4;
    cm_free(&cm);
    #undef cm
    free(cmp);
    return comp;
}

static size_t cm_decompress(uint8_t *dst, size_t cap,
                            const uint8_t *src, size_t src_size) {
    if (src_size < 4) return 0;
    size_t orig = (size_t)src[0] | ((size_t)src[1]<<8) |
                  ((size_t)src[2]<<16) | ((size_t)src[3]<<24);
    if (orig > cap) return 0;
    
    cm_t *cmp = (cm_t*)calloc(1, sizeof(cm_t)); cm_init(cmp, dst, orig);
    #define cm (*cmp)
    rcdec_t rc; rcdec_init(&rc, src+4, src_size-4);
    float str[N_MODELS];
    
    for (size_t i = 0; i < orig; i++) {
        if (i > 0) match_update(&cm.match, (uint32_t)i);
        cm.partial = 1;
        uint8_t byte = 0;
        
        for (int bit = 7; bit >= 0; bit--) {
            int bp = 7 - bit;
            uint16_t prob = cm_predict(&cm, (uint32_t)i, bp, str);
            int b = rcdec_bit(&rc, prob);
            
            float dm1 = mixer_mix(&cm.mx1[(cm.prev[0] << 3) | bp], str);
            float dm2 = mixer_mix(&cm.mx2[(char_class(cm.prev[0]) << 3) | bp], str);
            float dm3 = mixer_mix(&cm.mx3[bp], str);
            int dmx4 = (((cm.prev[0] >> 4) << 5) | ((cm.prev[1] >> 4) << 1) | bp/4) & 1023;
            float dm4 = mixer_mix(&cm.mx4[dmx4], str);
            int dmx5 = (cm.word_hash ^ (cm.word_hash >> 8)) & 255;
            float dm5 = mixer_mix(&cm.mx5[dmx5], str);
            float mixed = (dm1 + dm2 + dm3 + dm4 + dm5) * 0.2f;
            uint16_t mp = (uint16_t)(mixed * PROB_MAX);
            if (mp < 1) mp = 1; if (mp > PROB_MAX-1) mp = PROB_MAX-1;
            
            cm_update(&cm, (uint32_t)i, bp, b, str, mp);
            byte = (byte << 1) | b;
        }
        dst[i] = byte;
        cm_byte_done(&cm, byte);
    }
    
    cm_free(&cm);
    #undef cm
    free(cmp);
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
    
    printf("=== CM v3 (StateMap) ===\n");
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
    
    printf("Baselines: CM-v2 44943 | MCX-L12 43154 (3.52x) | bzip2 43207 | PAQ8 ~40000\n");
    
    free(data); free(comp); free(dec);
    return ok ? 0 : 1;
}
