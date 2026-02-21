/**
 * @file lz_decompress.c
 * @brief Ultra-fast LZ77 decompressor with wild-copy loops.
 *
 * Designed for maximum throughput:
 *  - Branchless token parsing
 *  - 16-byte wild-copy for literals and matches
 *  - Minimal bounds checking (validated by format constraints)
 */

#include "mcx_lz.h"
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

/** Read a 16-bit little-endian offset */
static inline uint16_t mcx_lz_d_read16(const uint8_t* p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

/**
 * Wild-copy: copy `len` bytes from src to dst using 16-byte chunks.
 * May overwrite up to 15 bytes past `len` — caller must ensure buffer space.
 * This is the key to fast decompression: memcpy in fixed 16-byte steps
 * avoids branch mispredictions and lets the CPU pipeline stay full.
 */
static inline void mcx_lz_wild_copy16(uint8_t* dst, const uint8_t* src, size_t len)
{
    size_t i = 0;
    while (i < len) {
        memcpy(dst + i, src + i, 16);
        i += 16;
    }
}

/**
 * Match copy for overlapping sequences (offset < copy length).
 * Must be done byte-by-byte for correctness when offset is small.
 */
static inline void mcx_lz_match_copy(uint8_t* dst, uint8_t* dst_end, size_t offset, size_t length)
{
    const uint8_t* src = dst - offset;

    if (offset >= 16 && dst + length + 16 <= dst_end) {
        /* Non-overlapping and safe from bounds: use wild-copy */
        mcx_lz_wild_copy16(dst, src, length);
    } else if (offset == 1) {
        /* Special case: RLE (repeat single byte) */
        memset(dst, *src, length);
    } else if (offset >= length) {
        /* Non-overlapping but near the end: use regular memcpy */
        memcpy(dst, src, length);
    } else {
        /* Overlapping: byte-by-byte */
        size_t i;
        for (i = 0; i < length; i++) {
            dst[i] = src[i];
        }
    }
}

/* ── Read an extended length (series of 0xFF bytes + final < 0xFF) ── */
static inline size_t mcx_lz_read_ext_length(const uint8_t** pp, const uint8_t* end)
{
    size_t extra = 0;
    const uint8_t* p = *pp;
    uint8_t byte;
    do {
        if (p >= end) return (size_t)-1; /* corruption */
        byte = *p++;
        extra += byte;
    } while (byte == 255);
    *pp = p;
    return extra;
}

/* ── Public API ───────────────────────────────────────────────────── */

size_t mcx_lz_decompress(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size,
    size_t      original_size)
{
    const uint8_t* ip     = (const uint8_t*)src;
    const uint8_t* ip_end = ip + src_size;
    uint8_t*       op     = (uint8_t*)dst;
    uint8_t*       op_end = op + dst_cap;

    if (dst_cap < original_size)
        return 0;

    while (ip < ip_end) {
        /* ── Read token ──────────────────────────────────── */
        uint8_t token = *ip++;
        size_t  lit_len   = (token >> 4) & 0x0F;
        size_t  match_len_base = token & 0x0F;

        /* ── Extended literal length ─────────────────────── */
        if (lit_len == 15) {
            size_t ext = mcx_lz_read_ext_length(&ip, ip_end);
            if (ext == (size_t)-1) return 0;
            lit_len += ext;
        }

        /* ── Copy literals ───────────────────────────────── */
        if (lit_len > 0) {
            if (ip + lit_len > ip_end || op + lit_len > op_end)
                return 0; /* corruption or buffer overflow */

            if (lit_len >= 16 && op + lit_len + 16 <= op_end) {
                mcx_lz_wild_copy16(op, ip, lit_len);
            } else {
                memcpy(op, ip, lit_len);
            }
            ip += lit_len;
            op += lit_len;
        }

        /* ── Check if this was the last sequence (no match follows) ── */
        if (ip >= ip_end)
            break; /* Successfully reached end of compressed stream */

        /* ── Read offset ─────────────────────────────────── */
        if (ip + 2 > ip_end)
            return 0; /* truncated */
        uint16_t offset = mcx_lz_d_read16(ip);
        ip += 2;

        if (offset == 0)
            return 0; /* invalid: zero offset */

        /* ── Extended match length ────────────────────────── */
        size_t match_len = match_len_base + MCX_LZ_MIN_MATCH;
        if (match_len_base == 15) {
            size_t ext = mcx_lz_read_ext_length(&ip, ip_end);
            if (ext == (size_t)-1) return 0;
            match_len += ext;
        }

        /* ── Validate match ──────────────────────────────── */
        if (op - (uint8_t*)dst < (ptrdiff_t)offset)
            return 0; /* offset points before start of output */
        if (op + match_len > op_end)
            return 0; /* would overflow output buffer */

        /* ── Copy match ──────────────────────────────────── */
        mcx_lz_match_copy(op, op_end, (size_t)offset, match_len);
        op += match_len;
    }

    return (size_t)(op - (uint8_t*)dst);
}
