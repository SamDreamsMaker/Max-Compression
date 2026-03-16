/**
 * @file babel_stride.c
 * @brief Babel Stride-Delta Transform implementation.
 *
 * Format:
 *   [4: magic "BSDL"]
 *   [4: original_size LE]
 *   [2: stride LE]
 *   [data: delta-encoded bytes]
 *
 * Detection: test strides 1-32 + common larger strides.
 * Select stride that gives best H0 reduction (minimum 0.3 bits/byte improvement).
 */

#include "babel_stride.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ── Helpers ───────────────────────────────────────────────────── */

static inline void write_u32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static inline uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_u16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

static inline uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static double entropy_h0_sample(const uint8_t* data, size_t n) {
    if (n == 0) return 8.0;
    size_t freq[256] = {0};
    for (size_t i = 0; i < n; i++) freq[data[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / n;
        h -= p * log2(p);
    }
    return h;
}

/* ── Stride detection ──────────────────────────────────────────── */

int mcx_babel_stride_detect(const uint8_t* src, size_t src_size) {
    if (src_size < 64) return 0;

    /* Sample size: first 64KB for speed */
    size_t sample_size = src_size > 65536 ? 65536 : src_size;
    double h0_orig = entropy_h0_sample(src, sample_size);

    uint8_t* tmp = (uint8_t*)malloc(sample_size);
    if (!tmp) return 0;

    int best_stride = 0;
    double best_h0 = h0_orig;
    double min_improvement = 0.3; /* at least 0.3 bits/byte improvement */

    /* Test strides 1-32 */
    for (int s = 1; s <= 32; s++) {
        for (size_t i = 0; i < sample_size; i++) {
            if (i < (size_t)s)
                tmp[i] = src[i];
            else
                tmp[i] = src[i] - src[i - s];
        }
        double h = entropy_h0_sample(tmp + s, sample_size - s);
        if (h < best_h0 - min_improvement) {
            best_h0 = h;
            best_stride = s;
        }
    }

    /* Test common larger strides (common record sizes) */
    int extra[] = { 36, 40, 48, 52, 64, 72, 80, 96, 100, 128, 160, 200, 256 };
    int nextra = sizeof(extra) / sizeof(extra[0]);
    for (int ei = 0; ei < nextra; ei++) {
        int s = extra[ei];
        if ((size_t)s >= sample_size / 4) continue;
        for (size_t i = 0; i < sample_size; i++) {
            if (i < (size_t)s)
                tmp[i] = src[i];
            else
                tmp[i] = src[i] - src[i - s];
        }
        double h = entropy_h0_sample(tmp + s, sample_size - s);
        if (h < best_h0 - min_improvement) {
            best_h0 = h;
            best_stride = s;
        }
    }

    free(tmp);
    return best_stride;
}

/* ── Forward transform ─────────────────────────────────────────── */

size_t mcx_babel_stride_forward(uint8_t* dst, size_t dst_cap,
                                 const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 64) return 0;

    int stride = mcx_babel_stride_detect(src, src_size);
    if (stride == 0) return 0;

    size_t out_size = 10 + src_size;
    if (dst_cap < out_size) return 0;

    /* Header */
    memcpy(dst, "BSDL", 4);
    write_u32(dst + 4, (uint32_t)src_size);
    write_u16(dst + 8, (uint16_t)stride);

    /* Apply delta with detected stride */
    for (size_t i = 0; i < src_size; i++) {
        if (i < (size_t)stride)
            dst[10 + i] = src[i];
        else
            dst[10 + i] = src[i] - src[i - stride];
    }

    return out_size;
}

/* ── Inverse transform ─────────────────────────────────────────── */

size_t mcx_babel_stride_inverse(uint8_t* dst, size_t dst_cap,
                                 const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 10) return 0;
    if (memcmp(src, "BSDL", 4) != 0) return 0;

    uint32_t orig_size = read_u32(src + 4);
    uint16_t stride = read_u16(src + 8);

    if (dst_cap < orig_size) return 0;
    if (src_size < 10 + orig_size) return 0;
    if (stride == 0 || stride > 256) return 0;

    for (size_t i = 0; i < orig_size; i++) {
        if (i < stride)
            dst[i] = src[10 + i];
        else
            dst[i] = src[10 + i] + dst[i - stride];
    }

    return orig_size;
}
