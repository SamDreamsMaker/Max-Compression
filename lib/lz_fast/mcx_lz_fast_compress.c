#include "mcx_lz_fast.h"
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define HAS_AVX2 1
#else
#define HAS_AVX2 0
#endif

// Knuth Multiplicative Hash Constants
#define HASH_MUL 2654435761U
#define HASH_SHIFT (32 - FAST_DICT_SIZE_BITS)

void mcx_lz_fast_init(mcx_lz_fast_ctx* ctx) {
    memset(ctx->dict, 0, sizeof(ctx->dict));
}

// SIMD 16-byte wild copy (Branchless)
static inline void mcx_wild_copy_16(uint8_t** op, const uint8_t** ip) {
#if HAS_AVX2
    __m128i v = _mm_loadu_si128((const __m128i*)*ip);
    _mm_storeu_si128((__m128i*)*op, v);
#else
    memcpy(*op, *ip, 16);
#endif
    *op += 16;
    *ip += 16;
}

// Mathematical scalar sequence hash
static inline uint32_t hash_4(const uint8_t* p) {
    uint32_t val;
    memcpy(&val, p, 4);
    return (val * HASH_MUL) >> HASH_SHIFT;
}

size_t mcx_lz_fast_compress(uint8_t* dst, size_t dst_cap,
                            const uint8_t* src, size_t src_size,
                            mcx_lz_fast_ctx* ctx) {
    if (src_size < 16) return 0; // Not compressible at high speeds

    const uint8_t* ip = src;
    const uint8_t* const iend = src + src_size - 16;
    const uint8_t* anchor = ip;

    uint8_t* op = dst;
    uint8_t* const oend = dst + dst_cap;

    // Advance 1 to avoid matching offset 0
    ip++;

    while (ip < iend) {
#if HAS_AVX2
        // AVX2 Parallel Hashing across 4 offsets simultaneously
        // This calculates hash(ip), hash(ip+1), hash(ip+2), hash(ip+3)
        uint32_t v0, v1, v2, v3;
        memcpy(&v0, ip + 0, 4);
        memcpy(&v1, ip + 1, 4);
        memcpy(&v2, ip + 2, 4);
        memcpy(&v3, ip + 3, 4);

        __m128i vals = _mm_setr_epi32(v0, v1, v2, v3);
        __m128i mul = _mm_set1_epi32(HASH_MUL);
        __m128i hashes = _mm_mullo_epi32(vals, mul);
        hashes = _mm_srli_epi32(hashes, HASH_SHIFT);

        // Extract hashed indices
        uint32_t h[4];
        _mm_storeu_si128((__m128i*)h, hashes);

        const uint8_t* match = NULL;
        uint32_t offset = 0;
        int match_idx = -1;

        // Cuckoo/Parallel evaluation: Check all 4 hashes instantly
        for (int i = 0; i < 4; i++) {
            uint32_t m_pos = ctx->dict[h[i]];
            if (m_pos > 0 && (ip + i - (src + m_pos)) < 65535) {
                if (memcmp(ip + i, src + m_pos, 4) == 0) {
                    match = src + m_pos;
                    match_idx = i;
                    break;
                }
            }
        }

        if (match_idx == -1) {
            // Update table mathematically and slide window by 4
            ctx->dict[h[0]] = (uint32_t)(ip + 0 - src);
            ctx->dict[h[1]] = (uint32_t)(ip + 1 - src);
            ctx->dict[h[2]] = (uint32_t)(ip + 2 - src);
            ctx->dict[h[3]] = (uint32_t)(ip + 3 - src);
            ip += 4;
            continue;
        }

        // We found a match at ip + match_idx!
        ip += match_idx;
        offset = (uint32_t)(ip - match);
#else
        // Scalar Fallback
        uint32_t h = hash_4(ip);
        uint32_t m_pos = ctx->dict[h];
        ctx->dict[h] = (uint32_t)(ip - src);

        uint32_t offset = (uint32_t)(ip - (src + m_pos));
        if (m_pos == 0 || offset >= 65535 || memcmp(ip, src + m_pos, 4) != 0) {
            ip++;
            continue;
        }
        const uint8_t* match = src + m_pos;
#endif

        // Vectorized Mathematical Length evaluation
        uint32_t match_len = 4;
        while (ip + match_len < iend && ip[match_len] == match[match_len]) {
            match_len++;
            if (match_len >= FAST_MAX_MATCH) break; // Cap to keep token formats branchless
        }

        // Output Sequence
        uint32_t lit_len = (uint32_t)(ip - anchor);
        if (op + lit_len + 5 > oend) return 0; // Overflow

        // Token Format: [8b lit_len_flag] [8b match_len] [16b offset] [literals...]
        // Branchless limits: lit_len < 255.
        // If lit_len >= 255 we will output multiple tokens to avoid complex header parsing
        while (lit_len >= 255) {
            *op++ = 255;
            *op++ = 0; // 0 match len
            *(uint16_t*)op = 0; op += 2;
            memcpy(op, anchor, 255);
            op += 255;
            anchor += 255;
            lit_len -= 255;
        }

        *op++ = (uint8_t)lit_len;
        *op++ = (uint8_t)match_len;
        *(uint16_t*)op = (uint16_t)offset;
        op += 2;

        memcpy(op, anchor, lit_len);
        op += lit_len;

        ip += match_len;
        anchor = ip;
    }

    // Flush remaining literals
    uint32_t lit_len = (uint32_t)(src_size - (anchor - src));
    while (lit_len > 0) {
        uint32_t chunk = lit_len >= 255 ? 255 : lit_len;
        *op++ = (uint8_t)chunk;
        *op++ = 0; // 0 match len
        *(uint16_t*)op = 0; op += 2; // 0 offset
        memcpy(op, anchor, chunk);
        op += chunk;
        anchor += chunk;
        lit_len -= chunk;
    }

    return (size_t)(op - dst);
}
