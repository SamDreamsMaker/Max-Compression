#ifndef MCX_LZ_FAST_H
#define MCX_LZ_FAST_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file mcx_lz_fast.h
 * @brief Phase J — Dual-Probe Cuckoo Hash + LZ4-Compatible Token Format
 *
 * Phase J improvements over Phase I:
 *  1. Two independent hash tables (Cuckoo) for 2x match density.
 *  2. LZ4-compatible [u4+u4|u16] token format (3 bytes vs 4 bytes/token).
 *  3. Native C bench harness to measure true hardware throughput.
 *
 * Token format (same as LZ4 frame format):
 *   [token:u8] where upper-4 = lit_len_code, lower-4 = match_len_code
 *   if lit_len_code  == 15: extra varint bytes until byte < 255
 *   [offset: u16 little-endian]
 *   if match_len_code == 15: extra varint bytes until byte < 255
 *   [literals: lit_len bytes]
 */

/* Dictionary sizing — 16-bit: fits in L1 cache on most CPUs */
#define FAST_DICT_SIZE_BITS 16
#define FAST_DICT_SIZE (1U << FAST_DICT_SIZE_BITS)

#define FAST_MIN_MATCH   4
#define FAST_MAX_OFFSET  65535

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Dual-probe Cuckoo context.
 * dict[0] uses primary hash (Knuth multiplicative).
 * dict[1] uses secondary hash (FNV-adjacent).
 * Separate arrays keep each probe on independent cache lines.
 */
typedef struct {
    uint32_t dict[2][FAST_DICT_SIZE];
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
