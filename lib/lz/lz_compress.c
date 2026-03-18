#include "compat.h"
/**
 * @file lz_compress.c
 * @brief LZ77 compressor v1.1 — Upgraded with:
 *  - Heap-allocated hash table (256K entries, supports large inputs)
 *  - Lazy match evaluation (for HC mode)
 *  - Improved hash function with secondary probe
 *  - Hash chain heads for collision resolution
 */

#include "mcx_lz.h"
#include <string.h>
#include <stdlib.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

static inline uint32_t lz_read32(const uint8_t* p) {
    uint32_t v; memcpy(&v, p, 4); return v;
}

static inline void lz_write16(uint8_t* p, uint16_t v) {
    memcpy(p, &v, 2);
}

/* Knuth multiplicative hash */
static inline uint32_t lz_hash4(const uint8_t* p, int hash_log) {
    return (lz_read32(p) * 2654435761u) >> (32 - hash_log);
}

/* Secondary hash for double-probe */
static inline uint32_t lz_hash4_alt(const uint8_t* p, int hash_log) {
    return (lz_read32(p) * 2246822519u) >> (32 - hash_log);
}

/* Fast match length comparison with 8-byte stride */
static inline size_t lz_count_match(const uint8_t* p1, const uint8_t* p2, size_t max_len) {
    size_t len = 0;
    while (len + 8 <= max_len) {
        uint64_t v1, v2;
        memcpy(&v1, p1 + len, 8);
        memcpy(&v2, p2 + len, 8);
        if (v1 != v2) {
#ifdef _MSC_VER
            unsigned long idx;
            _BitScanForward64(&idx, v1 ^ v2);
            return len + (idx >> 3);
#else
            return len + (mcx_ctzll(v1 ^ v2) >> 3);
#endif
        }
        len += 8;
    }
    while (len < max_len && p1[len] == p2[len]) len++;
    return len;
}

static inline uint8_t* lz_write_ext_len(uint8_t* op, size_t length) {
    while (length >= 255) { *op++ = 255; length -= 255; }
    *op++ = (uint8_t)length;
    return op;
}

/* ── Encode one sequence ──────────────────────────────────────────── */

static inline uint8_t* lz_write_sequence(
    uint8_t* op, uint8_t* op_end,
    const uint8_t* literals, size_t lit_len,
    uint16_t offset, size_t match_len)
{
    size_t token_match = match_len - MCX_LZ_MIN_MATCH;
    size_t lit_ext = lit_len >= 15 ? (lit_len - 15) / 255 + 1 : 0;
    size_t match_ext = token_match >= 15 ? (token_match - 15) / 255 + 1 : 0;
    size_t needed = 1 + lit_ext + lit_len + 2 + match_ext;
    if (op + needed > op_end) return NULL;

    /* Token */
    uint8_t* token_ptr = op++;
    uint8_t token_lit = (lit_len >= 15) ? 15 : (uint8_t)lit_len;
    uint8_t token_mtch = (token_match >= 15) ? 15 : (uint8_t)token_match;
    *token_ptr = (token_lit << 4) | token_mtch;

    /* Extended literal length */
    if (lit_len >= 15) op = lz_write_ext_len(op, lit_len - 15);

    /* Literals */
    memcpy(op, literals, lit_len);
    op += lit_len;

    /* Offset */
    lz_write16(op, offset);
    op += 2;

    /* Extended match length */
    if (token_match >= 15) op = lz_write_ext_len(op, token_match - 15);

    return op;
}

/* ── Write final literal run ──────────────────────────────────────── */

static inline uint8_t* lz_write_last_literals(
    uint8_t* op, uint8_t* op_end,
    const uint8_t* literals, size_t lit_len)
{
    if (lit_len == 0) return op;
    size_t ext = lit_len >= 15 ? (lit_len - 15) / 255 + 1 : 0;
    if (op + 1 + ext + lit_len > op_end) return NULL;
    if (lit_len >= 15) {
        *op++ = 0xF0;
        op = lz_write_ext_len(op, lit_len - 15);
    } else {
        *op++ = (uint8_t)(lit_len << 4);
    }
    memcpy(op, literals, lit_len);
    return op + lit_len;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Standard compressor (greedy, single/double probe)
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_lz_compress_bound(size_t src_size) {
    return src_size + (src_size / 255) + 16 + 4;
}

size_t mcx_lz_compress(
    void* dst, size_t dst_cap,
    const void* src, size_t src_size, int accel)
{
    const uint8_t* ip     = (const uint8_t*)src;
    const uint8_t* ip_end = ip + src_size;
    uint8_t* op     = (uint8_t*)dst;
    uint8_t* op_end = op + dst_cap;
    const uint8_t* match_limit = ip_end - MCX_LZ_LAST_LITERALS;
    const uint8_t* mflimit = ip_end - MCX_LZ_MIN_MATCH;

    if (accel < 1) accel = 1;

    /* Small input: all literals */
    if (src_size < MCX_LZ_MIN_MATCH + MCX_LZ_LAST_LITERALS) {
        op = lz_write_last_literals(op, op_end, ip, src_size);
        return op ? (size_t)(op - (uint8_t*)dst) : 0;
    }

    /* Scale hash table with input size for better cache usage */
    int hash_log = MCX_LZ_HASH_LOG;
    while (hash_log > 14 && (1u << hash_log) > src_size * 4)
        hash_log--;
    int hash_size = 1 << hash_log;
    uint32_t* ht = (uint32_t*)calloc(hash_size, sizeof(uint32_t));
    uint32_t* ht2 = (uint32_t*)calloc(hash_size, sizeof(uint32_t)); /* secondary */
    if (!ht || !ht2) { free(ht); free(ht2); return 0; }

    const uint8_t* anchor = ip;
    ip++;

    while (ip < mflimit) {
        uint32_t h1 = lz_hash4(ip, hash_log);
        uint32_t pos1 = ht[h1];
        ht[h1] = (uint32_t)(ip - (const uint8_t*)src);

        /* Primary probe */
        const uint8_t* ref = (const uint8_t*)src + pos1;
        size_t offset = (size_t)(ip - ref);

        if (offset > 0 && offset <= MCX_LZ_MAX_OFFSET && lz_read32(ref) == lz_read32(ip)) {
            goto found_match;
        }

        /* Secondary probe */
        uint32_t h2 = lz_hash4_alt(ip, hash_log);
        uint32_t pos2 = ht2[h2];
        ht2[h2] = (uint32_t)(ip - (const uint8_t*)src);

        ref = (const uint8_t*)src + pos2;
        offset = (size_t)(ip - ref);

        if (offset > 0 && offset <= MCX_LZ_MAX_OFFSET && lz_read32(ref) == lz_read32(ip)) {
            goto found_match;
        }

        ip += accel;
        continue;

    found_match:;
        size_t max_match = (size_t)(match_limit - ip);
        if (max_match < MCX_LZ_MIN_MATCH) max_match = MCX_LZ_MIN_MATCH;
        size_t match_len = MCX_LZ_MIN_MATCH +
            lz_count_match(ip + MCX_LZ_MIN_MATCH, ref + MCX_LZ_MIN_MATCH,
                           max_match - MCX_LZ_MIN_MATCH);

        /* Backward extension */
        while (ip > anchor && ref > (const uint8_t*)src && ip[-1] == ref[-1]) {
            ip--; ref--; match_len++;
        }

        /* Write sequence */
        op = lz_write_sequence(op, op_end, anchor, (size_t)(ip - anchor),
                               (uint16_t)offset, match_len);
        if (!op) { free(ht); free(ht2); return 0; }

        /* Update hash for positions in match (stride=2 for speed/ratio balance) */
        const uint8_t* mp = ip + 1;
        ip += match_len;
        anchor = ip;
        const uint8_t* mp_end = ip < mflimit ? ip : mflimit;
        while (mp < mp_end) {
            uint32_t mpos = (uint32_t)(mp - (const uint8_t*)src);
            ht[lz_hash4(mp, hash_log)] = mpos;
            mp += 2;
        }
    }

    /* Final literals */
    op = lz_write_last_literals(op, op_end, anchor, (size_t)(ip_end - anchor));
    free(ht);
    free(ht2);
    return op ? (size_t)(op - (uint8_t*)dst) : 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  High-compression mode (lazy match evaluation)
 *
 *  Instead of immediately encoding the first match found, we check
 *  if the NEXT position has a better (longer) match. If so, we
 *  emit ip as a literal and use the better match at ip+1.
 * ═══════════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════════
 *  HC compressor with hash chains — better match finding.
 *
 *  Uses a chain[] array where chain[pos] → previous position with
 *  the same hash. Depth-limited traversal (8 candidates max) finds
 *  the longest match within the 64KB window.
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_lz_compress_hc(
    void* dst, size_t dst_cap,
    const void* src, size_t src_size, int level)
{
    const uint8_t* ip     = (const uint8_t*)src;
    const uint8_t* ip_end = ip + src_size;
    uint8_t* op     = (uint8_t*)dst;
    uint8_t* op_end = op + dst_cap;
    const uint8_t* match_limit = ip_end - MCX_LZ_LAST_LITERALS;
    const uint8_t* mflimit = ip_end - MCX_LZ_MIN_MATCH;

    /* Scale chain depth with level: L4=4, L6=8, L9=16 */
    /* Scale chain depth with level: L4=4, L5-6=8, L7-8=16, L9=64 */
    int chain_depth = (level <= 4) ? 4 : (level <= 6) ? 8 : (level <= 8) ? 16 : 64;

    if (src_size < MCX_LZ_MIN_MATCH + MCX_LZ_LAST_LITERALS) {
        op = lz_write_last_literals(op, op_end, ip, src_size);
        return op ? (size_t)(op - (uint8_t*)dst) : 0;
    }

    int hash_log = MCX_LZ_HASH_LOG_HC;
    /* Scale down for small inputs */
    while (hash_log > 16 && (1u << hash_log) > src_size * 8)
        hash_log--;
    int hash_size = 1 << hash_log;
    uint32_t* ht = (uint32_t*)calloc(hash_size, sizeof(uint32_t));
    /* Chain array: chain[pos % window] → previous position with same hash.
     * Only need window_size entries since older positions are outside the window. */
    size_t chain_mask = MCX_LZ_MAX_OFFSET; /* 64K-1, power of 2 minus 1 */
    size_t chain_size = MCX_LZ_MAX_OFFSET + 1; /* 64K entries */
    uint32_t* chain = (uint32_t*)calloc(chain_size, sizeof(uint32_t));
    if (!ht || !chain) { free(ht); free(chain); return 0; }

    const uint8_t* anchor = ip;
    ip++;

    while (ip < mflimit) {
        uint32_t cur_pos = (uint32_t)(ip - (const uint8_t*)src);
        uint32_t h = lz_hash4(ip, hash_log);
        uint32_t prev = ht[h];

        /* Insert current position into chain */
        chain[cur_pos & chain_mask] = prev;
        ht[h] = cur_pos;

        /* Walk the chain to find the best (longest) match */
        const uint8_t* best_ref = NULL;
        size_t best_len = MCX_LZ_MIN_MATCH - 1;
        size_t best_offset = 0;

        uint32_t candidate = prev;
        for (int depth = 0; depth < chain_depth && candidate > 0; depth++) {
            size_t off = (size_t)(cur_pos - candidate);
            if (off > MCX_LZ_MAX_OFFSET) break; /* Outside window */

            const uint8_t* ref = (const uint8_t*)src + candidate;
            if (lz_read32(ref) == lz_read32(ip)) {
                size_t max_m = (size_t)(match_limit - ip);
                if (max_m < MCX_LZ_MIN_MATCH) max_m = MCX_LZ_MIN_MATCH;
                size_t ml = MCX_LZ_MIN_MATCH +
                    lz_count_match(ip + MCX_LZ_MIN_MATCH, ref + MCX_LZ_MIN_MATCH,
                                   max_m - MCX_LZ_MIN_MATCH);
                if (ml > best_len) {
                    best_len = ml;
                    best_ref = ref;
                    best_offset = off;
                }
            }
            candidate = chain[candidate & chain_mask];
        }

        if (best_len < MCX_LZ_MIN_MATCH) {
            ip++;
            continue;
        }

        /* Lazy evaluation: check if ip+1 has a longer match */
        if (ip + 1 < mflimit) {
            uint32_t next_pos = cur_pos + 1;
            uint32_t h_next = lz_hash4(ip + 1, hash_log);
            uint32_t prev_next = ht[h_next];

            /* Don't insert ip+1 yet — only peek */
            uint32_t cand2 = prev_next;
            size_t best_len2 = MCX_LZ_MIN_MATCH - 1;
            const uint8_t* best_ref2 = NULL;
            size_t best_off2 = 0;

            for (int depth = 0; depth < chain_depth && cand2 > 0; depth++) {
                size_t off2 = (size_t)(next_pos - cand2);
                if (off2 > MCX_LZ_MAX_OFFSET) break;

                const uint8_t* ref2 = (const uint8_t*)src + cand2;
                if (lz_read32(ref2) == lz_read32(ip + 1)) {
                    size_t max_m2 = (size_t)(match_limit - (ip + 1));
                    if (max_m2 < MCX_LZ_MIN_MATCH) max_m2 = MCX_LZ_MIN_MATCH;
                    size_t ml2 = MCX_LZ_MIN_MATCH +
                        lz_count_match(ip + 1 + MCX_LZ_MIN_MATCH, ref2 + MCX_LZ_MIN_MATCH,
                                       max_m2 - MCX_LZ_MIN_MATCH);
                    if (ml2 > best_len2) {
                        best_len2 = ml2;
                        best_ref2 = ref2;
                        best_off2 = off2;
                    }
                }
                cand2 = chain[cand2 & chain_mask];
            }

            if (best_len2 > best_len + 1) {
                /* Better match at ip+1 */
                ip++;
                /* Insert the skipped position */
                chain[(cur_pos + 1) & chain_mask] = ht[h_next];
                ht[h_next] = cur_pos + 1;
                best_len = best_len2;
                best_ref = best_ref2;
                best_offset = best_off2;
                cur_pos++;
            }
        }

        /* Backward extension: extend match into literal run */
        {
            const uint8_t* bref = (const uint8_t*)src + ((uint32_t)(ip - (const uint8_t*)src) - best_offset);
            while (ip > anchor && bref > (const uint8_t*)src &&
                   ip[-1] == bref[-1]) {
                ip--;
                bref--;
                best_len++;
            }
        }

        /* Write sequence */
        op = lz_write_sequence(op, op_end, anchor, (size_t)(ip - anchor),
                               (uint16_t)best_offset, best_len);
        if (!op) { free(ht); free(chain); return 0; }

        ip += best_len;
        anchor = ip;

        /* Insert positions we skipped over into the chain */
        if (ip < mflimit) {
            for (const uint8_t* fill = ip - 2; fill < ip && fill > (const uint8_t*)src; fill++) {
                uint32_t fpos = (uint32_t)(fill - (const uint8_t*)src);
                uint32_t fh = lz_hash4(fill, hash_log);
                chain[fpos & chain_mask] = ht[fh];
                ht[fh] = fpos;
            }
        }
    }

    op = lz_write_last_literals(op, op_end, anchor, (size_t)(ip_end - anchor));
    free(ht);
    free(chain);
    return op ? (size_t)(op - (uint8_t*)dst) : 0;
}
