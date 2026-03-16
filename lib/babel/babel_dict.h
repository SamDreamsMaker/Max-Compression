/**
 * @file babel_dict.h
 * @brief Babel Dictionary Transform — Word/n-gram replacement preprocessing.
 *
 * Concept: Scan input, build frequency table of words (alpha sequences)
 * and byte n-grams. Replace each word with a variable-length index
 * (most frequent → shortest code). Non-word bytes pass through with
 * an escape mechanism.
 *
 * This is similar to dictionary-based text preprocessing used by
 * high-ratio compressors (e.g., zpaq, cmix text preprocessing).
 *
 * The dictionary is stored in the output header so the decoder can
 * reconstruct the original data.
 *
 * Lossless: exact roundtrip guaranteed.
 */

#ifndef MCX_BABEL_DICT_H
#define MCX_BABEL_DICT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Forward dictionary transform.
 * @return bytes written to dst, or 0 on error.
 */
size_t mcx_babel_dict_forward(uint8_t* dst, size_t dst_cap,
                               const uint8_t* src, size_t src_size);

/**
 * Inverse dictionary transform.
 * @return bytes written to dst, or 0 on error.
 */
size_t mcx_babel_dict_inverse(uint8_t* dst, size_t dst_cap,
                               const uint8_t* src, size_t src_size);

/**
 * Bound: maximum output size.
 * Worst case: every byte needs escape → 2x + header.
 */
static inline size_t mcx_babel_dict_bound(size_t src_size) {
    return src_size * 2 + 65536; /* generous bound for dictionary header */
}

#ifdef __cplusplus
}
#endif

#endif /* MCX_BABEL_DICT_H */
