/**
 * @file mcx_lz_fast_decompress.c
 * @brief Phase J — LZ4-Compatible Token Format Decompressor
 *
 * Parses the same [u4+u4 | u16 offset | varint* | literals] token stream
 * produced by mcx_lz_fast_compress. Uses SSE2 16-byte wild-copy loops for
 * maximum literal throughput, and handles overlapping matches safely.
 */

#include "mcx_lz_fast.h"
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#  include <immintrin.h>
#  define HAS_SSE2 1
#else
#  define HAS_SSE2 0
#endif

/* ── SIMD 16-byte literal wild-copy ─────────────────────────────────── */
static inline void wild_copy16(uint8_t* dst, const uint8_t* src, uint8_t* end) {
    do {
#if HAS_SSE2
        __m128i v = _mm_loadu_si128((const __m128i*)src);
        _mm_storeu_si128((__m128i*)dst, v);
#else
        memcpy(dst, src, 16);
#endif
        src += 16; dst += 16;
    } while (dst < end);
}

/* ── SIMD 16-byte match copy — offset-safe ──────────────────────────── */
static inline void match_copy16(uint8_t* dst, const uint8_t* src, uint8_t* end, uint16_t offset) {
    if (offset >= 16) {
        /* Non-overlapping or safely strided — bulk copy */
        do {
#if HAS_SSE2
            __m128i v = _mm_loadu_si128((const __m128i*)src);
            _mm_storeu_si128((__m128i*)dst, v);
#else
            memcpy(dst, src, 16);
#endif
            src += 16; dst += 16;
        } while (dst < end);
    } else {
        /* Short offset (RLE-like): must go byte by byte */
        while (dst < end) { *dst++ = *src++; }
    }
}

/* ── Main Decompressor ───────────────────────────────────────────────── */
size_t mcx_lz_fast_decompress(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size) {
    if (src_size == 0) return 0;

    const uint8_t* ip  = src;
    const uint8_t* iend = src + src_size;

    uint8_t* op    = dst;
    uint8_t* oend  = dst + dst_cap;
    /* Reserve padding for SIMD over-read/over-write */
    uint8_t* olimit = (dst_cap >= 16) ? oend - 16 : oend;

    while (ip < iend) {
        /* ── Parse token byte ───────────────────────────────────────── */
        uint8_t token = *ip++;

        /* Literal length */
        uint32_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t s;
            do { if (ip >= iend) return 0; s = *ip++; lit_len += s; } while (s == 255);
        }

        /* Copy literals */
        if (op + lit_len > oend) return 0;
        if (ip + lit_len > iend) return 0;

        if (op < olimit && lit_len >= 8) {
            wild_copy16(op, ip, op + lit_len);
        } else {
            memcpy(op, ip, lit_len);
        }
        ip += lit_len;
        op += lit_len;

        /* End of block: last token can have no match (lower nibble = 0 with no offset) */
        if (ip >= iend) break;

        /* ── Parse offset ────────────────────────────────────────────── */
        if (ip + 2 > iend) return 0;
        uint16_t offset;
        memcpy(&offset, ip, 2);
        ip += 2;
        if (offset == 0) return 0;  /* Offset 0 is invalid */

        const uint8_t* match = op - offset;
        if (match < dst) return 0;  /* Bad offset */

        /* Match length */
        uint32_t match_len = (token & 0x0F) + FAST_MIN_MATCH;
        if ((token & 0x0F) == 15) {
            uint8_t s;
            do { if (ip >= iend) return 0; s = *ip++; match_len += s; } while (s == 255);
        }

        /* Copy match */
        if (op + match_len > oend) return 0;
        uint8_t* mend = op + match_len;

        if (op < olimit) {
            match_copy16(op, match, mend, offset);
        } else {
            for (uint32_t i = 0; i < match_len; i++) op[i] = match[i];
        }
        op += match_len;
    }

    return (size_t)(op - dst);
}
