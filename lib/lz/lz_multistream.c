#include "compat.h"
/**
 * @file lz_multistream.c
 * @brief Multi-stream LZ77 + FSE entropy coder — Phase 4 (Repcode stack).
 *
 * Separates LZ77 output into 4 independent FSE-compressed streams:
 *
 *   Stream L    : raw literal bytes
 *   Stream LL   : literal-length varint bytes
 *   Stream ML   : match-length varint bytes
 *   Stream OFVAR: offset / repcode bytes (variable-length per match)
 *
 * OFVAR encoding (one entry per actual match, NOT per sentinel):
 *   b < 3         → repcode b → use rep[b] as the offset
 *   3 ≤ b < 255   → small offset: off = b - 2  (encodes offsets 1..252)
 *   b == 255      → large offset: next 2 bytes LE = raw offset
 *
 * Repcode stack (last 3 offsets used, initialised to {1, 4, 8}):
 *   repcode 0: use rep[0]; stack unchanged
 *   repcode 1: use rep[1]; stack = {rep[1], rep[0], rep[2]}
 *   repcode 2: use rep[2]; stack = {rep[2], rep[0], rep[1]}
 *   new offset: stack = {off, rep[0], rep[1]}
 *
 * Varint encoding for LL and ML:
 *   write_varint(v) → floor(v/255) copies of 0xFF then (v % 255).
 *   read_varint: accumulate bytes until one is < 255.
 *
 * ML sentinel: ml_code=0 means end-of-block (no match after this literal run).
 *   Real match → ml_code = (match_len - MSMATCH + 1) ≥ 1.
 *
 * Block format (magic 0xAD):
 *   [1]  0xAD
 *   [4]  orig_size   (uint32 LE)
 *   [4]  num_seqs    (uint32 LE)  — includes sentinel; num_matches = num_seqs-1
 *   [4]  lit_bytes   (uint32 LE)
 *   then 4 stream records, each:
 *     [1]  flag  (0=raw, 1=FSE-compressed)
 *     [4]  size  (uint32 LE, bytes of payload)
 *     [size] payload
 *   Streams in order: L, LL, ML, OFVAR.
 */

#include "mcx_lz.h"
#include "../entropy/mcx_fse.h"
#include "../simd/mcx_simd.h"
#include <string.h>
#include <stdlib.h>

/* ── Configuration ──────────────────────────────────────────── */
#define MSMATCH       MCX_LZ_MIN_MATCH
#define MSHASHLOG     MCX_LZ_HASH_LOG
#define MSHASHSIZE    MCX_LZ_HASH_SIZE
#define MSMAX_OFF     MCX_LZ_MAX_OFFSET
#define MS_MAGIC      0xADu
#define MS_PF_DIST    16  /* prefetch distance in bytes (hides ~12-cycle L2 miss) */

/* ── Helpers ─────────────────────────────────────────────────── */
static inline uint32_t ms_read32(const uint8_t* p) {
    uint32_t v; memcpy(&v, p, 4); return v;
}
/* ms_hash / ms_hash2 removed — use mcx_simd_hash_dual() from mcx_simd.h */

static inline size_t ms_match_len(const uint8_t* a, const uint8_t* b, size_t max) {
    size_t n = 0;
    while (n + 8 <= max) {
        uint64_t va, vb;
        memcpy(&va, a + n, 8); memcpy(&vb, b + n, 8);
        if (va != vb) {
#ifdef _MSC_VER
            unsigned long idx;
            _BitScanForward64(&idx, va ^ vb);
            return n + (idx >> 3);
#else
            return n + (mcx_ctzll(va ^ vb) >> 3);
#endif
        }
        n += 8;
    }
    while (n < max && a[n] == b[n]) n++;
    return n;
}

/* ── Varint encoding ─────────────────────────────────────────── */
static inline size_t ms_put_varint(uint8_t* buf, uint32_t val) {
    size_t n = 0;
    while (val >= 255) { buf[n++] = 255; val -= 255; }
    buf[n++] = (uint8_t)val;
    return n;
}
static inline uint32_t ms_get_varint(const uint8_t** pp, const uint8_t* end) {
    uint32_t val = 0; uint8_t b;
    do {
        if (*pp >= end) return val;
        b = *(*pp)++;
        val += b;
    } while (b == 255);
    return val;
}

/* ── OFVAR encoding ──────────────────────────────────────────── */
/* Returns bytes written (1 or 3). */
static inline size_t ms_put_ofvar(uint8_t* buf, size_t n, int rc, uint32_t off) {
    if (rc >= 0) {
        buf[n] = (uint8_t)rc;   /* repcode 0, 1, or 2 */
        return 1;
    }
    if (off <= 252) {
        buf[n] = (uint8_t)(off + 2);   /* range 3..254 → offsets 1..252 */
        return 1;
    }
    buf[n]   = 255;
    buf[n+1] = (uint8_t)(off & 0xFF);
    buf[n+2] = (uint8_t)(off >> 8);
    return 3;
}

/* Repcode stack helpers */
static inline void rep_update_new(uint32_t rep[3], uint32_t off) {
    rep[2] = rep[1]; rep[1] = rep[0]; rep[0] = off;
}
static inline void rep_update_rc(uint32_t rep[3], int rc) {
    if (rc == 0) return;  /* rep[0] already at front */
    if (rc == 1) { uint32_t t = rep[1]; rep[1] = rep[0]; rep[0] = t; }
    else          { uint32_t t = rep[2]; rep[2] = rep[1]; rep[1] = rep[0]; rep[0] = t; }
}

/* ── Store / load a stream ──────────────────────────────────── */
static size_t ms_store_stream(uint8_t* dst, size_t dst_cap,
                               const uint8_t* data, size_t data_size)
{
    if (dst_cap < 5) return 0;
    if (data_size == 0) {
        dst[0] = 0; uint32_t z = 0; memcpy(dst + 1, &z, 4); return 5;
    }
    size_t avail = dst_cap - 5;
    size_t fse   = mcx_fse_compress(dst + 5, avail, data, data_size);
    if (fse > 0) {
        dst[0] = 1;
        uint32_t s = (uint32_t)fse; memcpy(dst + 1, &s, 4);
        return 5 + fse;
    }
    if (5 + data_size > dst_cap) return 0;
    dst[0] = 0;
    uint32_t s = (uint32_t)data_size; memcpy(dst + 1, &s, 4);
    memcpy(dst + 5, data, data_size);
    return 5 + data_size;
}

static size_t ms_load_stream(uint8_t* dst, size_t dst_cap,
                              const uint8_t** pp, const uint8_t* end)
{
    if (*pp + 5 > end) return 0;
    uint8_t  flag = *(*pp)++;
    uint32_t sz;  memcpy(&sz, *pp, 4); *pp += 4;
    if (*pp + sz > end) return 0;
    if (sz == 0) return 0;
    const uint8_t* payload = *pp; *pp += sz;
    if (flag == 0) {
        if (sz > dst_cap) return 0;
        memcpy(dst, payload, sz);
        return sz;
    } else {
        return mcx_fse_decompress(dst, dst_cap, payload, sz);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Collect LZ77 sequences → 4 separate byte streams.
 * ═══════════════════════════════════════════════════════════════ */
static int collect_sequences(
    const uint8_t* src, size_t src_size,
    uint8_t* lits,    size_t* lit_n,
    uint8_t* ll_buf,  size_t* ll_n,
    uint8_t* ml_buf,  size_t* ml_n,
    uint8_t* ofvar,   size_t* ofvar_n,
    size_t*  num_seqs)
{
    *lit_n = *ll_n = *ml_n = *ofvar_n = *num_seqs = 0;

    if (src_size < (size_t)(MSMATCH + MCX_LZ_LAST_LITERALS)) {
        memcpy(lits, src, src_size);
        *lit_n    = src_size;
        *ll_n     = ms_put_varint(ll_buf, (uint32_t)src_size);
        *ml_n     = ms_put_varint(ml_buf, 0);   /* sentinel ml_code=0 */
        /* OFVAR: 0 entries (no match) */
        *num_seqs = 1;
        return 1;
    }

    uint32_t* ht1 = (uint32_t*)calloc(MSHASHSIZE, sizeof(uint32_t));
    uint32_t* ht2 = (uint32_t*)calloc(MSHASHSIZE, sizeof(uint32_t));
    if (!ht1 || !ht2) { free(ht1); free(ht2); return 0; }

    uint32_t rep[3] = {1, 4, 8};   /* repcode stack */

    const uint8_t* ip     = src + 1;
    const uint8_t* anchor = src;
    const uint8_t* iend   = src + src_size;
    const uint8_t* ilimit = iend - MCX_LZ_LAST_LITERALS;
    const uint8_t* mlimit = iend - MSMATCH;

    while (ip < mlimit) {
        /* Compute both hashes simultaneously via SSE4.1 (or scalar fallback) */
        uint32_t h1, h2;
        mcx_simd_hash_dual(ms_read32(ip), MSHASHLOG, &h1, &h2);

        /* Prefetch ht1/ht2 entries MS_PF_DIST bytes ahead to hide L2 latency */
        if (ip + MS_PF_DIST < mlimit) {
            uint32_t pf1, pf2;
            mcx_simd_hash_dual(ms_read32(ip + MS_PF_DIST), MSHASHLOG, &pf1, &pf2);
            MCX_PREFETCH(&ht1[pf1]);
            MCX_PREFETCH(&ht2[pf2]);
        }

        /* Load both candidates before updating (avoid reading our own write) */
        uint32_t p1 = ht1[h1];
        uint32_t p2 = ht2[h2];

        /* Update both tables unconditionally for maximum coverage */
        ht1[h1] = (uint32_t)(ip - src);
        ht2[h2] = (uint32_t)(ip - src);

        /* Evaluate primary probe */
        const uint8_t* best_ref = NULL;
        size_t         best_off = 0;
        size_t         mlen     = 0;

        {
            size_t off = (size_t)(ip - src) - p1;
            if (off > 0 && off <= MSMAX_OFF) {
                const uint8_t* ref = src + p1;
                if (ms_read32(ref) == ms_read32(ip)) {
                    size_t maxm = (size_t)(ilimit - ip);
                    size_t len = MSMATCH + ms_match_len(ip + MSMATCH,
                                                         ref + MSMATCH,
                                                         maxm - MSMATCH);
                    best_ref = ref; best_off = off; mlen = len;
                }
            }
        }

        /* Evaluate secondary probe — prefer longer match; break ties by offset */
        {
            size_t off = (size_t)(ip - src) - p2;
            if (off > 0 && off <= MSMAX_OFF) {
                const uint8_t* ref = src + p2;
                if (ms_read32(ref) == ms_read32(ip)) {
                    size_t maxm = (size_t)(ilimit - ip);
                    size_t len = MSMATCH + ms_match_len(ip + MSMATCH,
                                                         ref + MSMATCH,
                                                         maxm - MSMATCH);
                    if (best_ref == NULL || len > mlen ||
                        (len == mlen && off < best_off)) {
                        best_ref = ref; best_off = off; mlen = len;
                    }
                }
            }
        }

        if (mlen < (size_t)MSMATCH) { ip++; continue; }

        /* Use selected match */
        size_t off = best_off;

        /* Emit one sequence */
        size_t lit_len = (size_t)(ip - anchor);
        memcpy(lits + *lit_n, anchor, lit_len);
        *lit_n += lit_len;
        *ll_n  += ms_put_varint(ll_buf + *ll_n, (uint32_t)lit_len);
        *ml_n  += ms_put_varint(ml_buf + *ml_n, (uint32_t)(mlen - MSMATCH + 1));

        /* Check for repcode */
        int rc = -1;
        if (off == (size_t)rep[0])      rc = 0;
        else if (off == (size_t)rep[1]) rc = 1;
        else if (off == (size_t)rep[2]) rc = 2;

        *ofvar_n += ms_put_ofvar(ofvar, *ofvar_n, rc, (uint32_t)off);

        if (rc >= 0) rep_update_rc(rep, rc);
        else         rep_update_new(rep, (uint32_t)off);

        (*num_seqs)++;

        ip    += mlen;
        anchor = ip;

        if (ip < mlimit) {
            uint32_t nh1, nh2;
            mcx_simd_hash_dual(ms_read32(ip), MSHASHLOG, &nh1, &nh2);
            ht1[nh1] = (uint32_t)(ip - src);
            ht2[nh2] = (uint32_t)(ip - src);
        }
    }

    /* Trailing literals — sentinel sequence (ml_code = 0) */
    size_t tail = (size_t)(iend - anchor);
    memcpy(lits + *lit_n, anchor, tail);
    *lit_n += tail;
    *ll_n  += ms_put_varint(ll_buf + *ll_n, (uint32_t)tail);
    *ml_n  += ms_put_varint(ml_buf + *ml_n, 0);   /* sentinel */
    /* OFVAR: no entry for sentinel */
    (*num_seqs)++;

    free(ht1); free(ht2);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 *  Public compress
 * ═══════════════════════════════════════════════════════════════ */
size_t mcx_lzfse_compress(void* dst, size_t dst_cap,
                            const void* src, size_t src_size)
{
    const uint8_t* in  = (const uint8_t*)src;
    uint8_t*       out = (uint8_t*)dst;

    if (src_size == 0) return 0;
    if (dst_cap < 13 + 4 * 5) return 0;

    size_t max_seqs  = src_size / MSMATCH + 4;
    /* OFVAR worst case: 3 bytes per match (all large offsets) */
    size_t ofvar_cap = max_seqs * 3 + 16;

    uint8_t* lits   = (uint8_t*)malloc(src_size + 16);
    uint8_t* ll_buf = (uint8_t*)malloc(src_size + max_seqs + 16);
    uint8_t* ml_buf = (uint8_t*)malloc(src_size + max_seqs + 16);
    uint8_t* ofvar  = (uint8_t*)malloc(ofvar_cap);

    if (!lits || !ll_buf || !ml_buf || !ofvar) {
        free(lits); free(ll_buf); free(ml_buf); free(ofvar);
        return 0;
    }

    size_t lit_n, ll_n, ml_n, ofvar_n, num_seqs;
    if (!collect_sequences(in, src_size, lits, &lit_n,
                            ll_buf, &ll_n, ml_buf, &ml_n,
                            ofvar, &ofvar_n, &num_seqs)) {
        free(lits); free(ll_buf); free(ml_buf); free(ofvar);
        return 0;
    }

    /* Write header */
    uint8_t* op = out;
    *op++ = MS_MAGIC;
    uint32_t v;
    v = (uint32_t)src_size;  memcpy(op, &v, 4); op += 4;
    v = (uint32_t)num_seqs;  memcpy(op, &v, 4); op += 4;
    v = (uint32_t)lit_n;     memcpy(op, &v, 4); op += 4;

    /* Write 4 stream records: L, LL, ML, OFVAR */
    size_t rem = dst_cap - (size_t)(op - out);
    size_t w;

    w = ms_store_stream(op, rem, lits,   lit_n);   if (!w) goto fail; op += w; rem -= w;
    w = ms_store_stream(op, rem, ll_buf, ll_n);    if (!w) goto fail; op += w; rem -= w;
    w = ms_store_stream(op, rem, ml_buf, ml_n);    if (!w) goto fail; op += w; rem -= w;
    w = ms_store_stream(op, rem, ofvar,  ofvar_n); if (!w) goto fail; op += w;

    free(lits); free(ll_buf); free(ml_buf); free(ofvar);
    return (size_t)(op - out);

fail:
    free(lits); free(ll_buf); free(ml_buf); free(ofvar);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Public decompress
 * ═══════════════════════════════════════════════════════════════ */
size_t mcx_lzfse_decompress(void* dst, size_t dst_cap,
                              const void* src, size_t src_size)
{
    const uint8_t* in  = (const uint8_t*)src;
    uint8_t*       out = (uint8_t*)dst;

    if (src_size < 13) return 0;
    if (in[0] != MS_MAGIC) return 0;

    const uint8_t* ip = in + 1;
    uint32_t orig32;    memcpy(&orig32,    ip, 4); ip += 4;
    uint32_t num_seqs;  memcpy(&num_seqs,  ip, 4); ip += 4;
    uint32_t lit_bytes; memcpy(&lit_bytes, ip, 4); ip += 4;

    if ((size_t)orig32 > dst_cap) return 0;
    if (num_seqs == 0) return 0;

    size_t lit_cap  = (size_t)lit_bytes + 16;
    size_t match_total = (orig32 >= lit_bytes) ? (orig32 - lit_bytes) : 0;
    size_t ll_cap   = (size_t)lit_bytes / 255 + (size_t)num_seqs * 2 + 32;
    size_t ml_cap   = match_total / 255 + (size_t)num_seqs * 2 + 32;
    /* OFVAR: num_seqs-1 matches, each at most 3 bytes */
    size_t ofvar_cap = (size_t)num_seqs * 3 + 16;

    uint8_t* lits   = (uint8_t*)malloc(lit_cap);
    uint8_t* ll_buf = (uint8_t*)malloc(ll_cap);
    uint8_t* ml_buf = (uint8_t*)malloc(ml_cap);
    uint8_t* ofvar  = (uint8_t*)malloc(ofvar_cap);

    if (!lits || !ll_buf || !ml_buf || !ofvar) {
        free(lits); free(ll_buf); free(ml_buf); free(ofvar);
        return 0;
    }

    const uint8_t* end    = in + src_size;
    size_t lit_n   = ms_load_stream(lits,   lit_cap,   &ip, end);
    size_t ll_n    = ms_load_stream(ll_buf, ll_cap,    &ip, end);
    size_t ml_n    = ms_load_stream(ml_buf, ml_cap,    &ip, end);
    size_t ofvar_n = ms_load_stream(ofvar,  ofvar_cap, &ip, end);

    if ((lit_n == 0 && lit_bytes > 0) || ll_n == 0 || ml_n == 0) goto fail;
    /* ofvar can be 0 only when there are no matches (num_seqs==1) */
    if (ofvar_n == 0 && num_seqs > 1) goto fail;
    (void)lit_n;

    /* Replay sequences */
    uint32_t rep[3] = {1, 4, 8};  /* repcode stack — must mirror encoder */

    uint8_t*       op      = out;
    uint8_t*       oend    = out + orig32;
    const uint8_t* lit_ptr = lits;
    const uint8_t* ll_ptr  = ll_buf;
    const uint8_t* ml_ptr  = ml_buf;
    const uint8_t* ll_end  = ll_buf + ll_n;
    const uint8_t* ml_end  = ml_buf + ml_n;
    const uint8_t* ofv_ptr = ofvar;
    const uint8_t* ofv_end = ofvar + ofvar_n;

    for (uint32_t seq = 0; seq < num_seqs; seq++) {
        /* Literal run */
        uint32_t lit_len = ms_get_varint(&ll_ptr, ll_end);
        if (op + lit_len > oend) goto fail;
        if (lit_ptr + lit_len > lits + lit_n) goto fail;
        memcpy(op, lit_ptr, lit_len);
        op      += lit_len;
        lit_ptr += lit_len;

        /* ML: 0 = end-of-block sentinel */
        uint32_t ml_code = ms_get_varint(&ml_ptr, ml_end);
        if (ml_code == 0) break;

        uint32_t match_len = ml_code + (uint32_t)MSMATCH - 1;

        /* Decode OFVAR */
        if (ofv_ptr >= ofv_end) goto fail;
        uint8_t  b   = *ofv_ptr++;
        uint32_t offset;

        if (b < 3) {
            /* Repcode */
            int rc = (int)b;
            offset = rep[rc];
            rep_update_rc(rep, rc);
        } else if (b < 255) {
            /* Small offset: off = b - 2 */
            offset = (uint32_t)(b - 2);
            rep_update_new(rep, offset);
        } else {
            /* Large offset: next 2 bytes LE */
            if (ofv_ptr + 2 > ofv_end) goto fail;
            offset  = (uint32_t)ofv_ptr[0] | ((uint32_t)ofv_ptr[1] << 8);
            ofv_ptr += 2;
            rep_update_new(rep, offset);
        }

        if (offset == 0) goto fail;
        if (op + match_len > oend) goto fail;
        const uint8_t* ref = op - offset;
        if (ref < out) goto fail;

        /* Match copy — byte-by-byte for short offsets (RLE), bulk for long */
        uint8_t* mend = op + match_len;
        if (offset >= 8 && mend + 8 <= oend + 8) {
            do {
                uint64_t vv; memcpy(&vv, ref, 8);
                memcpy(op, &vv, 8);
                op += 8; ref += 8;
            } while (op < mend);
            op = mend;
        } else {
            while (op < mend) *op++ = *ref++;
        }
    }

    size_t result = (size_t)(op - out);
    free(lits); free(ll_buf); free(ml_buf); free(ofvar);
    return result;

fail:
    free(lits); free(ll_buf); free(ml_buf); free(ofvar);
    return 0;
}
