/**
 * @file lz24.c
 * @brief LZ77 with 24-bit offsets and hash chains for high-ratio text compression.
 *
 * Key improvements over LZ16:
 *  - 24-bit offsets → 16MB window (vs 64KB)
 *  - Hash chains for multi-candidate match finding
 *  - Lazy + 2-step lazy evaluation
 *  - Optimized for text data (where long-distance repeats are common)
 *
 * Block types: 0xAC (LZ24+FSE), 0xAD (LZ24 raw)
 */

#include "mcx_lz.h"
#include <string.h>
#include <stdlib.h>

#define LZ24_MAX_OFFSET    (1 << 24)  /* 16MB window */
#define LZ24_HASH_LOG      18         /* 256K buckets */
#define LZ24_HASH_SIZE     (1 << LZ24_HASH_LOG)
#define LZ24_CHAIN_LEN     64         /* Max chain depth per search */

/* ── Helpers ──────────────────────────────────────────────────── */

static inline uint32_t lz24_read32(const uint8_t* p) {
    uint32_t v; memcpy(&v, p, 4); return v;
}

static inline uint32_t lz24_hash4(const uint8_t* p) {
    return (lz24_read32(p) * 2654435761u) >> (32 - LZ24_HASH_LOG);
}

static inline size_t lz24_count_match(const uint8_t* p1, const uint8_t* p2, size_t max_len) {
    size_t len = 0;
    while (len + 8 <= max_len) {
        uint64_t v1, v2;
        memcpy(&v1, p1 + len, 8);
        memcpy(&v2, p2 + len, 8);
        if (v1 != v2) {
            return len + (__builtin_ctzll(v1 ^ v2) >> 3);
        }
        len += 8;
    }
    while (len < max_len && p1[len] == p2[len]) len++;
    return len;
}

static inline void lz24_write24(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
}

static inline uint32_t lz24_read24(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline uint8_t* lz24_write_ext(uint8_t* op, size_t length) {
    while (length >= 255) { *op++ = 255; length -= 255; }
    *op++ = (uint8_t)length;
    return op;
}

/* ── Sequence encoding (3-byte offset) ────────────────────────── */

static inline uint8_t* lz24_write_seq(
    uint8_t* op, uint8_t* op_end,
    const uint8_t* lits, size_t lit_len,
    uint32_t offset, size_t match_len)
{
    size_t tm = match_len - MCX_LZ_MIN_MATCH;
    size_t le = lit_len >= 15 ? (lit_len - 15) / 255 + 1 : 0;
    size_t me = tm >= 15 ? (tm - 15) / 255 + 1 : 0;
    if (op + 1 + le + lit_len + 3 + me > op_end) return NULL;

    uint8_t* tp = op++;
    *tp = ((lit_len >= 15 ? 15 : (uint8_t)lit_len) << 4) |
           (tm >= 15 ? 15 : (uint8_t)tm);

    if (lit_len >= 15) op = lz24_write_ext(op, lit_len - 15);
    memcpy(op, lits, lit_len); op += lit_len;
    lz24_write24(op, offset); op += 3;
    if (tm >= 15) op = lz24_write_ext(op, tm - 15);
    return op;
}

static inline uint8_t* lz24_write_last(
    uint8_t* op, uint8_t* op_end,
    const uint8_t* lits, size_t lit_len)
{
    if (lit_len == 0) return op;
    size_t ext = lit_len >= 15 ? (lit_len - 15) / 255 + 1 : 0;
    if (op + 1 + ext + lit_len > op_end) return NULL;
    if (lit_len >= 15) { *op++ = 0xF0; op = lz24_write_ext(op, lit_len - 15); }
    else { *op++ = (uint8_t)(lit_len << 4); }
    memcpy(op, lits, lit_len);
    return op + lit_len;
}

/* ── Hash chain: find best match among chain candidates ───────── */

static size_t lz24_find_best_match(
    const uint8_t* src, size_t src_size,
    size_t ip_pos,
    const uint32_t* head, const uint32_t* chain,
    const uint8_t** best_ref_out)
{
    const uint8_t* ip = src + ip_pos;
    const uint8_t* match_limit = src + src_size - MCX_LZ_LAST_LITERALS;
    size_t max_match = (size_t)(match_limit - ip);
    if (max_match < MCX_LZ_MIN_MATCH) return 0;

    uint32_t h = lz24_hash4(ip);
    uint32_t pos = head[h];

    size_t best_len = MCX_LZ_MIN_MATCH - 1;
    const uint8_t* best_ref = NULL;

    int depth = LZ24_CHAIN_LEN;
    while (pos != 0 && depth-- > 0) {
        if (pos >= ip_pos) break; /* shouldn't happen */
        size_t offset = ip_pos - pos;
        if (offset >= LZ24_MAX_OFFSET) break; /* too far */

        const uint8_t* ref = src + pos;
        if (ref[best_len] == ip[best_len] && lz24_read32(ref) == lz24_read32(ip)) {
            size_t len = MCX_LZ_MIN_MATCH +
                lz24_count_match(ip + MCX_LZ_MIN_MATCH, ref + MCX_LZ_MIN_MATCH,
                                 max_match - MCX_LZ_MIN_MATCH);
            if (len > best_len) {
                best_len = len;
                best_ref = ref;
                if (len >= max_match) break; /* can't do better */
            }
        }

        uint32_t next = chain[pos];
        if (next >= pos) break; /* chain must go backward */
        pos = next;
    }

    if (best_ref) {
        *best_ref_out = best_ref;
        return best_len;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  LZ24 Compressor (hash chains + lazy evaluation)
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_lz24_compress_bound(size_t src_size) {
    return src_size + (src_size / 255) + 16 + 4 + (src_size / 4);
}

size_t mcx_lz24_compress(
    void* dst, size_t dst_cap,
    const void* src, size_t src_size)
{
    const uint8_t* in     = (const uint8_t*)src;
    const uint8_t* in_end = in + src_size;
    uint8_t* op     = (uint8_t*)dst;
    uint8_t* op_end = op + dst_cap;
    const uint8_t* mflimit = in_end - MCX_LZ_MIN_MATCH;

    if (src_size < MCX_LZ_MIN_MATCH + MCX_LZ_LAST_LITERALS) {
        op = lz24_write_last(op, op_end, in, src_size);
        return op ? (size_t)(op - (uint8_t*)dst) : 0;
    }

    /* Hash chain tables: head[hash] = most recent position,
     * chain[position] = previous position with same hash.
     * chain is indexed by position (not hash) → no chain collisions. */
    uint32_t* head  = (uint32_t*)calloc(LZ24_HASH_SIZE, sizeof(uint32_t));
    uint32_t* chain = (uint32_t*)calloc(src_size, sizeof(uint32_t));
    if (!head || !chain) { free(head); free(chain); return 0; }

    const uint8_t* anchor = in;
    size_t ip = 1;

    while (ip < (size_t)(mflimit - in)) {
        const uint8_t* best_ref = NULL;
        size_t match_len = lz24_find_best_match(in, src_size, ip, head, chain, &best_ref);

        /* Update hash chain for current position */
        uint32_t h = lz24_hash4(in + ip);
        chain[ip] = head[h];
        head[h] = (uint32_t)ip;

        if (match_len < MCX_LZ_MIN_MATCH) {
            ip++;
            continue;
        }

        /* Lazy evaluation: check ip+1 for a longer match */
        const uint8_t* best_ref2 = NULL;
        size_t match_len2 = 0;
        if (ip + 1 < (size_t)(mflimit - in)) {
            /* Update chain for ip+1 before searching */
            uint32_t h2 = lz24_hash4(in + ip + 1);
            chain[ip + 1] = head[h2];
            head[h2] = (uint32_t)(ip + 1);

            match_len2 = lz24_find_best_match(in, src_size, ip + 1, head, chain, &best_ref2);
        }

        if (match_len2 > match_len + 1) {
            /* Better match at ip+1, skip current position */
            ip++;
            match_len = match_len2;
            best_ref = best_ref2;
        }

        uint32_t offset = (uint32_t)((in + ip) - best_ref);
        op = lz24_write_seq(op, op_end, anchor, (size_t)((in + ip) - anchor),
                             offset, match_len);
        if (!op) { free(head); free(chain); return 0; }

        /* Update chain for all matched positions */
        size_t end = ip + match_len;
        for (size_t j = ip + 1; j < end && j < (size_t)(mflimit - in); j++) {
            uint32_t hj = lz24_hash4(in + j);
            chain[j] = head[hj];
            head[hj] = (uint32_t)j;
        }

        ip = end;
        anchor = in + ip;
    }

    /* Final literals */
    op = lz24_write_last(op, op_end, anchor, (size_t)(in_end - anchor));
    free(head);
    free(chain);
    return op ? (size_t)(op - (uint8_t*)dst) : 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  LZ24 Decompressor
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_lz24_decompress(
    void* dst, size_t dst_cap,
    const void* src, size_t src_size,
    size_t original_size)
{
    const uint8_t* ip     = (const uint8_t*)src;
    const uint8_t* ip_end = ip + src_size;
    uint8_t* op     = (uint8_t*)dst;
    uint8_t* op_end = op + dst_cap;
    uint8_t* op_target = op + original_size;

    while (ip < ip_end) {
        uint8_t token = *ip++;
        size_t lit_len = (size_t)(token >> 4);

        if (lit_len == 15) {
            while (ip < ip_end) {
                uint8_t ext = *ip++;
                lit_len += ext;
                if (ext < 255) break;
            }
        }

        if (ip + lit_len > ip_end || op + lit_len > op_end) return 0;
        memcpy(op, ip, lit_len);
        ip += lit_len;
        op += lit_len;

        if (op >= op_target) break;

        if (ip + 3 > ip_end) return 0;
        uint32_t offset = lz24_read24(ip);
        ip += 3;

        if (offset == 0 || offset > (size_t)(op - (uint8_t*)dst)) return 0;

        size_t match_len = (size_t)(token & 0x0F) + MCX_LZ_MIN_MATCH;
        if ((token & 0x0F) == 15) {
            while (ip < ip_end) {
                uint8_t ext = *ip++;
                match_len += ext;
                if (ext < 255) break;
            }
        }

        uint8_t* match = op - offset;
        if (match < (uint8_t*)dst || op + match_len > op_end) return 0;

        if (offset < match_len) {
            for (size_t i = 0; i < match_len; i++) op[i] = match[i];
        } else {
            memcpy(op, match, match_len);
        }
        op += match_len;
    }

    return (size_t)(op - (uint8_t*)dst);
}
