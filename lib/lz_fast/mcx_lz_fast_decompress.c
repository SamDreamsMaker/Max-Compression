#include "mcx_lz_fast.h"
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define HAS_AVX2 1
#else
#define HAS_AVX2 0
#endif

/**
 * SIMD 32-byte branchless copy — only safe if src+32 and dst+32 are in bounds.
 * Used for over-copy with exact advance-by-n semantics (wild-copy pattern).
 */
static inline void wild_copy_32(uint8_t* dst, const uint8_t* src, size_t len) {
#if HAS_AVX2
    /* Branchless strategy: process 32-byte chunks, then handle tail with memcpy.
       The over-read is harmless when the caller reserved 32 extra bytes. */
    size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(src + i));
        _mm256_storeu_si256((__m256i*)(dst + i), v);
    }
    if (i < len) memcpy(dst + i, src + i, len - i);
#else
    memcpy(dst, src, len);
#endif
}

/**
 * Branchless match copy supporting overlapping offsets.
 * For offset >= 16, uses SIMD chunked copy.
 * For offset < 16, falls back to byte-by-byte (safe for RLE-like patterns).
 */
static inline void match_copy(uint8_t* op, const uint8_t* match, size_t len, uint16_t offset) {
    if (offset >= 16) {
#if HAS_AVX2
        size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            __m128i v = _mm_loadu_si128((const __m128i*)(match + i));
            _mm_storeu_si128((__m128i*)(op + i), v);
        }
        if (i < len) memcpy(op + i, match + i, len - i);
#else
        memcpy(op, match, len);
#endif
    } else {
        /* Short offset: overlapping copy, must be byte-by-byte */
        for (size_t i = 0; i < len; i++) op[i] = match[i];
    }
}

size_t mcx_lz_fast_decompress(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size) {
    if (src_size == 0) return 0;

    const uint8_t* ip = src;
    const uint8_t* const iend = src + src_size;

    uint8_t* op = dst;
    uint8_t* const oend = dst + dst_cap - 32; /* Reserve for wild-copy padding */

    while (ip < iend) {
        if (op > oend) return 0; /* Output overflow */
        if (ip + 4 > iend) return 0; /* Token truncated */

        /* Token Format: [lit_len:u8] [match_len:u8] [offset:u16] */
        uint8_t  lit_len   = *ip++;
        uint8_t  match_len = *ip++;
        uint16_t offset    = 0;
        memcpy(&offset, ip, 2);
        ip += 2;

        /* Literal copy — exact, no over-copy corruption */
        if (lit_len > 0) {
            if (ip + lit_len > iend) return 0;
            wild_copy_32(op, ip, lit_len);
            ip += lit_len;
            op += lit_len;
        }

        /* Match copy */
        if (match_len > 0) {
            if (offset == 0 || op - dst < offset) return 0; /* Bad offset */
            match_copy(op, op - offset, match_len, offset);
            op += match_len;
        }
    }

    return (size_t)(op - dst);
}
