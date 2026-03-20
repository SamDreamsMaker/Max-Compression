/**
 * @file mcx_lz.h
 * @brief LZ77 dictionary-matching engine for MaxCompression v1.0.
 *
 * v1.1 upgrades:
 *  - Hash table size configurable (18-bit = 256K for better match finding)
 *  - Heap-allocated hash table (no more stack overflow on large tables)
 *  - Offsets up to 24-bit (16MB window) for structured data
 *  - Lazy match evaluation for better ratios
 *  - Huffman post-processing integration point
 *
 * Token format (per sequence):
 *  [token: 1 byte] [ext_lit_len: 0+] [literals] [offset: 2-3 bytes] [ext_match_len: 0+]
 *
 *  token high nibble = literal_length (0-15, 15 = extended)
 *  token low  nibble = match_length - MIN_MATCH (0-15, 15 = extended)
 *
 *  Offset encoding:
 *    If offset <= 65535: 2 bytes (little-endian)
 *    If offset > 65535:  3 bytes (little-endian, flag in token reserved bit)
 *
 * Format constraints:
 *  - Last 5 bytes are always literals
 *  - Minimum match length: 4 bytes
 */

#ifndef MCX_LZ_H
#define MCX_LZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ─────────────────────────────────────────────── */

#define MCX_LZ_MIN_MATCH       4
#define MCX_LZ_LAST_LITERALS   5
#define MCX_LZ_HASH_LOG       18     /* Default 256K entries */
#define MCX_LZ_HASH_LOG_HC    20     /* 1M entries for HC modes (L4+) */
#define MCX_LZ_HASH_SIZE      (1 << MCX_LZ_HASH_LOG)
#define MCX_LZ_MAX_OFFSET     65535  /* 16-bit offset for compatibility */
#define MCX_LZ_SKIP_TRIGGER    6

/* ── Compression levels ────────────────────────────────────────── */

#define MCX_LZ_LEVEL_FAST     1  /* Greedy, accel=4, hash_log=16 */
#define MCX_LZ_LEVEL_DEFAULT  5  /* Greedy, accel=1, hash_log=18 */
#define MCX_LZ_LEVEL_HIGH     9  /* Lazy evaluation, hash_log=18, best ratio */

/* ── LZ24: Extended 24-bit offset variant (16MB window) ───────── */
size_t mcx_lz24_compress_bound(size_t src_size);
size_t mcx_lz24_compress(void* dst, size_t dst_cap, const void* src, size_t src_size);
size_t mcx_lz24_decompress(void* dst, size_t dst_cap, const void* src, size_t src_size, size_t original_size);

/* ── Public API ────────────────────────────────────────────────── */

size_t mcx_lz_compress_bound(size_t src_size);

size_t mcx_lz_compress(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size,
    int         accel
);

/**
 * Lazy LZ77 compression with dual-probe hash (no chains).
 * Checks ip+1 (and optionally ip+2 when lazy_depth>=2) for a longer
 * match before emitting. Faster than HC but better ratio than pure
 * greedy. L2 uses lazy_depth=1, L3 uses lazy_depth=2.
 */
size_t mcx_lz_compress_lazy(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size,
    int         accel,
    int         lazy_depth
);

/**
 * High-ratio LZ77 compression with hash-chain match finding.
 * Slower than mcx_lz_compress but finds better matches.
 */
size_t mcx_lz_compress_hc(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size,
    int         level
);

size_t mcx_lz_decompress(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size,
    size_t      original_size
);

/**
 * Multi-stream LZ77 + FSE compressor (Phase 3).
 * Encodes literals, lit-lengths, match-lengths, and offsets into
 * separate FSE-compressed streams for better entropy modeling.
 * @return compressed size, or 0 on failure
 */
size_t mcx_lzfse_compress(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size
);

/**
 * Multi-stream LZ77 + FSE decompressor (Phase 3).
 * @return decompressed size, or 0 on failure
 */
size_t mcx_lzfse_decompress(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size
);

#ifdef __cplusplus
}
#endif

#endif /* MCX_LZ_H */
