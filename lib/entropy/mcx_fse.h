/**
 * @file mcx_fse.h
 * @brief Finite State Entropy (tANS) coder for MaxCompression v1.0.
 *
 * Implementation of table-based Asymmetric Numeral Systems (tANS),
 * the same entropy coding family used by Zstandard's FSE.
 *
 * Key design:
 *  - Table size: 1 << MCX_FSE_LOG (1024 states)
 *  - Normalized frequency distribution from histogram
 *  - Encoding is done backward, decoding forward
 *  - Supports up to 256 symbols
 */

#ifndef MCX_FSE_H
#define MCX_FSE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCX_FSE_LOG          10       /* Table size = 1024 states */
#define MCX_FSE_TABLE_SIZE   (1 << MCX_FSE_LOG)
#define MCX_FSE_MAX_SYMBOLS  256

/* ── Encoding table entry ──────────────────────────────────────── */
typedef struct {
    uint16_t delta_nb_bits;  /* number of bits to output */
    uint16_t delta_find_state; /* delta to add after bit output */
    uint32_t base;           /* base value for state calculation */
} mcx_fse_enc_entry;

/* ── Decoding table entry ──────────────────────────────────────── */
typedef struct {
    uint8_t  symbol;         /* decoded symbol */
    uint8_t  nb_bits;        /* number of bits to consume */
    uint16_t new_state;      /* next state after consuming bits */
} mcx_fse_dec_entry;

/* ── FSE Tables ────────────────────────────────────────────────── */
typedef struct {
    mcx_fse_enc_entry table[MCX_FSE_TABLE_SIZE];
    int16_t           symbol_tt[MCX_FSE_MAX_SYMBOLS]; /* symbol transformation table */
    uint16_t          norm_freq[MCX_FSE_MAX_SYMBOLS];  /* normalized frequencies */
    int               max_symbol;
    int               table_log;
} mcx_fse_enc_table;

typedef struct {
    mcx_fse_dec_entry table[MCX_FSE_TABLE_SIZE];
    uint16_t          norm_freq[MCX_FSE_MAX_SYMBOLS];
    int               max_symbol;
    int               table_log;
} mcx_fse_dec_table;

/**
 * Build normalized frequency table from raw counts.
 * Distributes `table_size` total probability units across symbols proportionally.
 */
int mcx_fse_normalize_freq(
    uint16_t*       norm_freq,
    int*            max_symbol_out,
    const uint32_t* counts,
    int             num_symbols,
    int             table_log);

/**
 * Build encoding table from normalized frequencies.
 */
int mcx_fse_build_enc_table(mcx_fse_enc_table* ct, const uint16_t* norm_freq, int max_symbol, int table_log);

/**
 * Build decoding table from normalized frequencies.
 */
int mcx_fse_build_dec_table(mcx_fse_dec_table* dt, const uint16_t* norm_freq, int max_symbol, int table_log);

/**
 * Compress a byte buffer using FSE.
 * Writes: [header: norm_freq table] [compressed bitstream]
 *
 * @return compressed size, or 0 on failure
 */
size_t mcx_fse_compress(void* dst, size_t dst_cap, const void* src, size_t src_size);

/**
 * Decompress an FSE-compressed buffer.
 *
 * @return decompressed size, or 0 on failure
 */
size_t mcx_fse_decompress(void* dst, size_t dst_cap, const void* src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif /* MCX_FSE_H */
