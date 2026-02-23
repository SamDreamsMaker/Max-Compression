#ifndef MCX_LZ_FAST_H
#define MCX_LZ_FAST_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file mcx_lz_fast.h
 * @brief Prototype for Ultra-Speed Mathematical LZ Match Finding and Parsing
 *
 * This variant relies strictly on theoretical bounds (AVX2 Vectorized Hashing,
 * Cuckoo Replacement, and Branchless SIMD copy logic) to attempt to reach
 * > 2GB/s compression bounds without entropy coding overhead.
 */

/* Vectorized Hash constraints */
#define FAST_DICT_SIZE_BITS 15
#define FAST_DICT_SIZE (1U << FAST_DICT_SIZE_BITS)
#define FAST_DICT_MASK (FAST_DICT_SIZE - 1)

#define FAST_MIN_MATCH 4
#define FAST_MAX_MATCH 255 /* Simplified branchless length offset */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t dict[FAST_DICT_SIZE];
} mcx_lz_fast_ctx;

/**
 * @brief Zero-initializes the vectorized mathematical hashing context
 */
void mcx_lz_fast_init(mcx_lz_fast_ctx* ctx);

/**
 * @brief Branchless/pure mathematical vectorized LZ pass
 *
 * Uses interleaved unrolled bounds to bypass branch prediction flushes.
 * Entropy coding is disabled in this function to benchmark pure
 * L1/L2 cache dictionary lookup throughput vs LZ4.
 *
 * @return the compressed size mapped to the mathematical tokens.
 */
size_t mcx_lz_fast_compress(uint8_t* dst, size_t dst_cap,
                            const uint8_t* src, size_t src_size,
                            mcx_lz_fast_ctx* ctx);

size_t mcx_lz_fast_decompress(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif // MCX_LZ_FAST_H
