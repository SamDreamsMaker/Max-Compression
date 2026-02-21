/**
 * @file mcx_simd.h
 * @brief Portable SIMD acceleration layer for MaxCompression v1.0.
 *
 * Provides optimized memory operations for the decompression hot path:
 *  - Wild-copy (16-byte chunks): for literal and non-overlapping match copies
 *  - Match-copy with overlap handling (offset < 16)
 *
 * Compile-time detection:
 *  - x86-64 with AVX2/SSE2: use _mm_loadu/_mm_storeu intrinsics
 *  - ARM with NEON: use vld1q/vst1q intrinsics
 *  - Fallback: portable memcpy-based implementation
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

/* ── Wild-copy: copy ≥ len bytes in 16-byte chunks ─────────────── */

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
    /* Portable fallback */
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
        /* Non-overlapping: fast SIMD copy */
        mcx_simd_copy16(dst, src, length);
    } else if (offset == 1) {
        /* RLE: repeat single byte */
        memset(dst, *src, length);
    } else if (offset == 0) {
        return; /* invalid, but don't crash */
    } else {
        /* Overlapping: repeat the offset pattern until we have 16+ bytes,
         * then switch to SIMD wild-copy for the rest */
        size_t i;
        /* Build up at least `offset` bytes to create the repeating pattern */
        for (i = 0; i < length && i < offset; i++) {
            dst[i] = src[i];
        }
        /* Now dst[0..offset-1] contains the pattern; copy in offset-sized chunks */
        while (i < length) {
            size_t chunk = length - i;
            if (chunk > i) chunk = i;  /* can't copy more than what we've built */
            memcpy(dst + i, dst, chunk); /* copies from already-written output */
            i += chunk;
        }
    }
}

#endif /* MCX_SIMD_H */
