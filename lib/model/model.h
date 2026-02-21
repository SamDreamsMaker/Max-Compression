/**
 * @file model.h
 * @brief Predictive modeling — Integer context models for CM-rANS.
 *
 * Stage 2 of the MaxCompression pipeline.
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  The key insight of Context Modeling:                               │
 * │                                                                     │
 * │  Instead of one global frequency table, maintain 256 separate       │
 * │  tables — one per preceding byte (the "context").                  │
 * │                                                                     │
 * │  Example: after seeing 'h', the letter 'e' is very probable        │
 * │  (as in "the", "he", "she"). After 'q', only 'u' is likely.        │
 * │                                                                     │
 * │  This turns order-1 byte prediction into MUCH better compression    │
 * │  by letting the entropy coder use context-specific probabilities.   │
 * └─────────────────────────────────────────────────────────────────────┘
 */

#ifndef MCX_MODEL_H
#define MCX_MODEL_H

#include "../internal.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  Integer Frequency Model (Order-0)
 *
 *  Maintains byte frequency counts for entropy encoding.
 *  Uses integer arithmetic — compatible with rANS normalization.
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Order-0 frequency model with integer counts.
 * Frequencies start at 1 for Laplace smoothing (no zero probabilities).
 */
typedef struct {
    uint32_t freq[256];    /* Raw frequency counts (minimum 1) */
    uint32_t total;        /* Sum of all frequencies */
} mcx_freq_model_t;

/** Initialize with uniform distribution (Laplace smoothing: freq[i] = 1). */
void mcx_freq_model_init(mcx_freq_model_t* model);

/** Update the model after observing a byte. */
void mcx_freq_model_update(mcx_freq_model_t* model, uint8_t byte);

/** Get the normalized integer probability of a byte (scaled to denom). */
uint32_t mcx_freq_model_norm_prob(const mcx_freq_model_t* model,
                                  uint8_t byte, uint32_t denom);

/* ═══════════════════════════════════════════════════════════════════════
 *  Context Model (Order-1)
 *
 *  256 independent frequency tables, one per preceding byte.
 *  The previous byte is the "context" that selects which table to use.
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Order-1 context model — 256 × 256 = 65536 bytes of state.
 * Uses the previous byte as context to select a frequency table.
 */
typedef struct {
    mcx_freq_model_t contexts[256]; /* One model per context byte */
} mcx_context1_model_t;

/** Initialize all 256 context tables with uniform distribution. */
void mcx_context1_init(mcx_context1_model_t* model);

/** Update the model: observed 'byte' in context 'ctx'. */
void mcx_context1_update(mcx_context1_model_t* model, uint8_t ctx, uint8_t byte);

/** Get normalized frequency table for a given context (for rANS). */
void mcx_context1_get_freqs(const mcx_context1_model_t* model,
                             uint8_t ctx, uint32_t* out_freq, uint32_t denom);

/* ═══════════════════════════════════════════════════════════════════════
 *  CM-rANS: Context-Modeled ANS Compression
 *
 *  Combines order-1 context modeling with rANS entropy coding.
 *  Each byte is encoded using the probability table predicted by
 *  the preceding byte. This catches repetitive patterns that
 *  static frequency tables miss.
 *
 *  Compressed format:
 *    [4 bytes]   Original size
 *    [65536 × 2 bytes] Context frequency tables (256 contexts × 256 bytes × 2)
 *    [4 bytes]   Final rANS state
 *    [variable]  Encoded byte stream
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Compress data using order-1 context-modeled rANS.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output capacity
 * @param src       Input data
 * @param src_size  Input size
 * @return          Compressed size, or error
 */
size_t mcx_cmrans_compress(uint8_t* dst, size_t dst_cap,
                           const uint8_t* src, size_t src_size);

/**
 * Decompress CM-rANS encoded data.
 *
 * @param dst       Output buffer
 * @param dst_cap   Output capacity
 * @param src       Compressed data
 * @param src_size  Compressed data size
 * @return          Decompressed size, or error
 */
size_t mcx_cmrans_decompress(uint8_t* dst, size_t dst_cap,
                             const uint8_t* src, size_t src_size);

#endif /* MCX_MODEL_H */
