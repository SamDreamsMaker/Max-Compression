/**
 * @file cm.h
 * @brief Context Mixing compression engine for MaxCompression
 * 
 * PAQ-style context mixing with:
 * - 30+ context models (order-0 through order-8, word, sparse, match, etc.)
 * - Secondary Symbol Estimation (SSE)
 * - Multiple mixer contexts (adapt mixing weights per situation)
 * - Bit-level arithmetic coding
 * 
 * Targets: MCX L28+ (maximum compression, slower speed)
 */

#ifndef MCX_CM_H
#define MCX_CM_H

#include <stdint.h>
#include <stddef.h>

/* ── Configuration ──────────────────────────────────────────────── */

#define CM_PROB_BITS   12
#define CM_PROB_MAX    (1 << CM_PROB_BITS)   /* 4096 */
#define CM_PROB_HALF   (CM_PROB_MAX / 2)

#define CM_MAX_MODELS  40
#define CM_MAX_MIXERS  8

/* Model sizes (number of contexts) */
#define CM_O0_SIZE     (256)              /* 256 partial byte states */
#define CM_O1_SIZE     (256 * 256)        /* 64K */
#define CM_O2_SIZE     (1 << 16)          /* 64K (hashed) */
#define CM_O3_SIZE     (1 << 18)          /* 256K */
#define CM_O4_SIZE     (1 << 20)          /* 1M */
#define CM_O5_SIZE     (1 << 21)          /* 2M */
#define CM_O6_SIZE     (1 << 22)          /* 4M */
#define CM_O8_SIZE     (1 << 22)          /* 4M (hashed) */
#define CM_WORD_SIZE   (1 << 20)          /* 1M */
#define CM_MATCH_SIZE  (1 << 20)          /* 1M entry hash table */
#define CM_SPARSE_SIZE (1 << 18)          /* 256K */
#define CM_SSE_SIZE    (32 * 256)         /* 32 contexts × 256 prob buckets */

/* ── State map ──────────────────────────────────────────────────── */
/* Maps a context to a bit prediction probability */

typedef struct {
    uint16_t* prob;     /* Probability table */
    size_t size;        /* Number of entries */
    int rate;           /* Adaptation rate (shift bits) */
} cm_statemap_t;

/* ── Match model ────────────────────────────────────────────────── */

typedef struct {
    uint32_t* table;    /* Hash → position mapping */
    size_t table_size;
    size_t match_pos;   /* Current match position */
    size_t match_len;   /* Current match length */
    int active;
} cm_match_t;

/* ── SSE (Secondary Symbol Estimation) ──────────────────────────── */
/* Maps (context, probability) → better probability */

typedef struct {
    uint16_t table[32][256]; /* [context][prob_bucket] → adjusted prob */
} cm_sse_t;

/* ── Mixer ──────────────────────────────────────────────────────── */
/* Logistic mixer with per-context weights */

typedef struct {
    float* weights;     /* [n_mixer_contexts × n_models] */
    int n_models;
    int n_contexts;
    float learning_rate;
} cm_mixer_t;

/* ── Arithmetic coder ───────────────────────────────────────────── */

typedef struct {
    uint32_t lo, hi;
    uint8_t* buf;
    size_t buf_pos;
    size_t buf_cap;
} cm_rc_t;

/* ── Main CM state ──────────────────────────────────────────────── */

typedef struct {
    /* Models */
    cm_statemap_t models[CM_MAX_MODELS];
    int n_models;
    
    /* Match model */
    cm_match_t match;
    
    /* SSE */
    cm_sse_t sse;
    
    /* Mixer */
    cm_mixer_t mixer;
    
    /* Range coder */
    cm_rc_t rc;
    
    /* Context state */
    uint32_t ctx[CM_MAX_MODELS]; /* Current context for each model */
    uint8_t prev[8];             /* Previous bytes (circular) */
    uint32_t word_hash;          /* Rolling word hash */
    uint32_t line_hash;          /* Rolling line hash */
    uint8_t partial;             /* Partial byte being coded */
    int bit_pos;                 /* Current bit position in byte */
    
    /* Input data (for match model) */
    const uint8_t* data;
    size_t data_size;
    size_t pos;                  /* Current byte position */
} cm_state_t;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * Compress data using context mixing.
 * @return Compressed size, or 0 on error
 */
size_t mcx_cm_compress(uint8_t* dst, size_t dst_cap,
                       const uint8_t* src, size_t src_size);

/**
 * Decompress CM-compressed data.
 * @return Decompressed size, or 0 on error
 */
size_t mcx_cm_decompress(uint8_t* dst, size_t dst_cap,
                         const uint8_t* src, size_t src_size);

#endif /* MCX_CM_H */
