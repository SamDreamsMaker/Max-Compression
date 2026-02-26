/**
 * @file mcx_simd.h
 * @brief Portable SIMD acceleration layer for MaxCompression v1.0.
 *
 * Provides:
 *  - Wild-copy (16-byte chunks): for literal and non-overlapping match copies
 *  - Match-copy with overlap handling (offset < 16)
 *  - Dual-hash computation (SSE4.1): compute h1 and h2 in one vector multiply
 *  - Cache-line prefetch macro for match-finder lookahead
 */

#ifndef MCX_SIMD_H
#define MCX_SIMD_H

#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* ── Platform detection ────────────────────────────────────────── */

#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
    #define MCX_SIMD_AVX2 1
    #include <immintrin.h>
#elif defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64)))
    #define MCX_SIMD_SSE2 1
    #include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define MCX_SIMD_NEON 1
    #include <arm_neon.h>
#endif

/* SSE4.1: MSVC x64 allows _mm_mullo_epi32 without extra compiler flags;
 * GCC/Clang require -msse4.1 (detected via __SSE4_1__). */
#if defined(MCX_SIMD_SSE2) && (defined(_MSC_VER) || defined(__SSE4_1__))
    #define MCX_SIMD_SSE41 1
    #include <smmintrin.h>
#endif

/* ── Cache-line prefetch (read, T0 = L1 hint) ─────────────────── */
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
    #include <xmmintrin.h>
    #define MCX_PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
    #define MCX_PREFETCH(addr) __builtin_prefetch((const void*)(addr), 0, 1)
#else
    #define MCX_PREFETCH(addr) ((void)(addr))
#endif

/* ── Dual hash: compute h1=(v*mul1)>>s and h2=(v*mul2)>>s together ──
 * Uses one SSE4.1 mullo_epi32 when available, otherwise scalar.
 * mul1 = 2654435761 (Knuth golden ratio), mul2 = 2246822519 (FNV-adj). */
static inline void mcx_simd_hash_dual(uint32_t v, int log,
                                       uint32_t* h1, uint32_t* h2)
{
#if defined(MCX_SIMD_SSE41)
    /* Lane 0 = mul1, lane 1 = mul2 */
    __m128i muls = _mm_set_epi32(0, 0, (int)2246822519u, (int)2654435761u);
    __m128i vv   = _mm_set1_epi32((int)v);
    __m128i r    = _mm_mullo_epi32(vv, muls);
    r = _mm_srli_epi32(r, 32 - log);
    *h1 = (uint32_t)_mm_extract_epi32(r, 0);
    *h2 = (uint32_t)_mm_extract_epi32(r, 1);
#else
    *h1 = (v * 2654435761u) >> (32 - log);
    *h2 = (v * 2246822519u) >> (32 - log);
#endif
}

/* ── Wild-copy: copy >= len bytes in 16-byte chunks ─────────────── */

static inline void mcx_simd_copy16(uint8_t* dst, const uint8_t* src, size_t len)
{
#if defined(MCX_SIMD_SSE2)
    size_t i = 0;
    while (i < len) {
        __m128i v = _mm_loadu_si128((const __m128i*)(src + i));
        _mm_storeu_si128((__m128i*)(dst + i), v);
        i += 16;
    }
#elif defined(MCX_SIMD_NEON)
    size_t i = 0;
    while (i < len) {
        uint8x16_t v = vld1q_u8(src + i);
        vst1q_u8(dst + i, v);
        i += 16;
    }
#else
    size_t i = 0;
    while (i < len) {
        memcpy(dst + i, src + i, 16);
        i += 16;
    }
#endif
}

/* ── Match-copy: handles overlapping copies (offset < length) ──── */

static inline void mcx_simd_match_copy(uint8_t* dst, size_t offset, size_t length)
{
    const uint8_t* src = dst - offset;

    if (offset >= 16) {
        mcx_simd_copy16(dst, src, length);
    } else if (offset == 1) {
        memset(dst, *src, length);
    } else if (offset == 0) {
        return;
    } else {
        size_t i;
        for (i = 0; i < length && i < offset; i++) {
            dst[i] = src[i];
        }
        while (i < length) {
            size_t chunk = length - i;
            if (chunk > i) chunk = i;
            memcpy(dst + i, dst, chunk);
            i += chunk;
        }
    }
}

#endif /* MCX_SIMD_H */
