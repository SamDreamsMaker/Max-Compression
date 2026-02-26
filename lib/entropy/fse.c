/**
 * @file fse.c
 * @brief Finite State Entropy (tANS) coder — MaxCompression v1.0.
 *
 * Algorithm: table-based ANS (FSE) by Yann Collet / Jarosław Duda.
 *
 * State domains:
 *   Encoder state  : [L, 2L)   where L = TABLE_SIZE = 1024
 *   Decoder state  : [0, L-1]  (dec_table index)
 *
 * Bit-stream layout (standard FSE):
 *   - Symbols are encoded BACKWARD (sym[n-1] first, sym[0] last).
 *   - Bits are written FORWARD (LSB-first, bytes left→right).
 *   - A sentinel bit (=1) is appended at the end before flushing.
 *   - The decoder reads bytes from the RIGHT end, bits MSB-first.
 *   - brd_init() locates the sentinel and skips it.
 *
 * Magic bytes:
 *   0xFF  RLE (single symbol)
 *   0xFE  legacy Huffman (decode-only, old format)
 *   0xFD  tANS 1-stream (single state, used for very small inputs)
 *   0xFC  tANS 4-stream interleaved (default for n >= 4)
 *
 * 4-stream header (0xFC):
 *   [1]  0xFC
 *   [1]  max_symbol
 *   [2*(max_symbol+1)] norm_freq LE uint16
 *   [4]  orig_size uint32 LE
 *   [16] dec_init[0..3] uint32 LE each  (final_enc_state[l] − L)
 *   [N]  bitstream with sentinel
 *
 * 4-stream encoding: sym[i] → state[i % 4] (round-robin).
 * 4-stream decoding: issue 4 independent table lookups simultaneously
 *   → overlaps memory latency via ILP, giving 2-4x speedup over 1-stream.
 */

#include "mcx_fse.h"
#include <string.h>
#include <stdlib.h>

#define TS   MCX_FSE_TABLE_SIZE   /* 1024 */
#define RLOG MCX_FSE_LOG          /* 10   */

/* ─── floor(log2(v)), v > 0 ──────────────────────────────────── */
static inline int highbit32(uint32_t v) {
    int b = 0;
    while (v > 1) { v >>= 1; b++; }
    return b;
}

/* Position of highest set bit counting from 0 = LSB. Returns -1 if v == 0. */
static inline int highbit64(uint64_t v) {
    if (v == 0) return -1;
    int b = 0; uint64_t u = v;
    while (u > 1) { u >>= 1; b++; }
    return b;
}

/* ═══════════════════════════════════════════════════════════════
 *  1.  Normalize frequencies → sum = TABLE_SIZE
 * ═══════════════════════════════════════════════════════════════ */
int mcx_fse_normalize_freq(uint16_t* nf, int* ms_out,
                            const uint32_t* counts, int nsym, int tlog)
{
    const int tsz = 1 << tlog;
    uint64_t total = 0;
    int max_sym = -1;

    for (int i = 0; i < nsym; i++) {
        total += counts[i];
        if (counts[i] > 0) max_sym = i;
    }
    if (total == 0 || max_sym < 0) return -1;

    uint32_t sum = 0;
    int largest = -1; uint32_t lc = 0;

    for (int i = 0; i <= max_sym; i++) {
        nf[i] = 0;
        if (!counts[i]) continue;
        uint32_t f = (uint32_t)(((uint64_t)counts[i] * tsz) / total);
        if (f == 0) f = 1;
        nf[i] = (uint16_t)f;
        sum += f;
        if (counts[i] > lc) { lc = counts[i]; largest = i; }
    }

    if (largest < 0) return -1;
    if (sum <= (uint32_t)tsz)
        nf[largest] += (uint16_t)(tsz - sum);
    else {
        uint32_t ex = sum - (uint32_t)tsz;
        nf[largest] = (nf[largest] > ex + 1) ? nf[largest] - (uint16_t)ex : 1;
    }

    /* Verify */
    sum = 0;
    for (int i = 0; i <= max_sym; i++) sum += nf[i];
    if (sum != (uint32_t)tsz) return -1;

    if (ms_out) *ms_out = max_sym;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  2.  Spread function (same for enc and dec)
 * ═══════════════════════════════════════════════════════════════ */
static void build_spread(uint8_t* spread, const uint16_t* nf,
                          int max_sym, int tlog)
{
    const int tsz  = 1 << tlog;
    const int step = (tsz >> 1) + (tsz >> 3) + 3;   /* 643 for tsz=1024 */
    const int mask = tsz - 1;
    int pos = 0;
    for (int s = 0; s <= max_sym; s++)
        for (int j = 0; j < (int)nf[s]; j++) {
            spread[pos] = (uint8_t)s;
            pos = (pos + step) & mask;
        }
}

/* ═══════════════════════════════════════════════════════════════
 *  3.  Decode table
 *   dec_table[x] → { symbol, nb_bits, new_state_base }
 *   full new state = new_state_base + bits_from_stream
 * ═══════════════════════════════════════════════════════════════ */
int mcx_fse_build_dec_table(mcx_fse_dec_table* dt, const uint16_t* nf,
                             int max_sym, int tlog)
{
    const int tsz = 1 << tlog;
    uint8_t spread[TS];
    build_spread(spread, nf, max_sym, tlog);

    int rank[MCX_FSE_MAX_SYMBOLS] = {0};
    for (int x = 0; x < tsz; x++) {
        int s = spread[x];
        int k = rank[s]++;
        uint32_t e = (uint32_t)(k + nf[s]);          /* e ∈ [nf[s], 2·nf[s]) */
        int nb = tlog - highbit32(e);
        dt->table[x].symbol    = (uint8_t)s;
        dt->table[x].nb_bits   = (uint8_t)nb;
        dt->table[x].new_state = (uint16_t)((e << nb) - tsz);
    }
    dt->max_symbol = max_sym;
    dt->table_log  = tlog;
    for (int i = 0; i <= max_sym; i++) dt->norm_freq[i] = nf[i];
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  4.  Encode table
 *   stateTable[cumul[s]+rank_k] = spread_pos_k + L   (stored in .base)
 * ═══════════════════════════════════════════════════════════════ */
int mcx_fse_build_enc_table(mcx_fse_enc_table* ct, const uint16_t* nf,
                             int max_sym, int tlog)
{
    const int tsz = 1 << tlog;
    uint8_t spread[TS];
    build_spread(spread, nf, max_sym, tlog);

    uint32_t cumul[MCX_FSE_MAX_SYMBOLS + 1];
    cumul[0] = 0;
    for (int i = 0; i <= max_sym; i++) cumul[i+1] = cumul[i] + nf[i];

    int rank[MCX_FSE_MAX_SYMBOLS] = {0};
    for (int x = 0; x < tsz; x++) {
        int s = spread[x];
        int k = rank[s]++;
        ct->table[cumul[s] + k].base = (uint32_t)(x + tsz);
    }
    ct->max_symbol = max_sym;
    ct->table_log  = tlog;
    for (int i = 0; i <= max_sym; i++) ct->norm_freq[i] = nf[i];
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Forward bit writer  (LSB-first, bytes left→right)
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
    uint8_t* p;
    uint8_t* end;
    uint64_t acc;
    int      bits;
} bwx_t;

static inline void bwx_init(bwx_t* w, uint8_t* buf, size_t cap) {
    w->p = buf; w->end = buf + cap; w->acc = 0; w->bits = 0;
}
static inline int bwx_put(bwx_t* w, uint32_t val, int nb) {
    if (!nb) return 0;
    w->acc |= (uint64_t)(val & ((1u << nb) - 1)) << w->bits;
    w->bits += nb;
    while (w->bits >= 8) {
        if (w->p >= w->end) return -1;
        *w->p++ = (uint8_t)(w->acc & 0xFF);
        w->acc >>= 8; w->bits -= 8;
    }
    return 0;
}
static inline size_t bwx_done(bwx_t* w, uint8_t* base) {
    if (w->bits > 0 && w->p < w->end)
        *w->p++ = (uint8_t)(w->acc & 0xFF);
    return (size_t)(w->p - base);
}

/* ═══════════════════════════════════════════════════════════════
 *  Backward bit reader  (bytes right→left, bits MSB-first)
 *
 *  Encoder writes LSB-first left→right. Reading bytes right→left
 *  and extracting from the MSB of the 64-bit accumulator gives the
 *  original value back:
 *    val = b0 | b1<<1 | … | b_{n-1}<<(n-1)
 *    stored LSB-first; reading MSB-first gives b_{n-1},…,b0
 *    → acc>>(64-nb) = b_{n-1}·2^{n-1}+…+b0 = val  ✓
 *
 *  Sentinel bit (=1) terminates the encoder's output; brd_init()
 *  finds it at the top of the loaded window and skips it.
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
    const uint8_t* buf;
    size_t         remaining;   /* bytes not yet loaded (left side) */
    uint64_t       acc;         /* valid bits in HIGH positions */
    int            bits;
} brd_t;

static inline void brd_refill(brd_t* r) {
    while (r->bits <= 56 && r->remaining > 0) {
        r->remaining--;
        /* Put next left-side byte just below existing valid bits */
        r->acc |= (uint64_t)r->buf[r->remaining] << (64 - r->bits - 8);
        r->bits += 8;
    }
}

static int brd_init(brd_t* r, const uint8_t* buf, size_t sz) {
    r->buf = buf; r->remaining = sz; r->acc = 0; r->bits = 0;
    brd_refill(r);
    if (r->acc == 0) return -1;
    int hb   = highbit64(r->acc);
    int skip = (63 - hb) + 1;    /* leading zeros + the sentinel bit */
    if (skip > r->bits) return -1;
    r->acc <<= skip; r->bits -= skip;
    if (r->bits < 32) brd_refill(r);
    return 0;
}

static inline uint32_t brd_read(brd_t* r, int nb) {
    if (nb == 0) return 0;
    uint32_t val = (uint32_t)(r->acc >> (64 - nb));
    r->acc <<= nb; r->bits -= nb;
    if (r->bits < 32) brd_refill(r);
    return val;
}

/* ═══════════════════════════════════════════════════════════════
 *  Internal: build stateTable used by both encode paths
 * ═══════════════════════════════════════════════════════════════ */
static uint32_t* build_state_table(const uint16_t* nf, int max_sym,
                                    uint32_t* cumul_out)
{
    uint32_t* st = (uint32_t*)malloc(TS * sizeof(uint32_t));
    if (!st) return NULL;
    cumul_out[0] = 0;
    for (int i = 0; i <= max_sym; i++) cumul_out[i+1] = cumul_out[i] + nf[i];
    uint8_t spread[TS];
    build_spread(spread, nf, max_sym, RLOG);
    int rank[MCX_FSE_MAX_SYMBOLS] = {0};
    for (int x = 0; x < TS; x++) {
        int s = spread[x]; int k = rank[s]++;
        st[cumul_out[s] + k] = (uint32_t)(x + TS);
    }
    return st;
}

/* ─── Encode one symbol into state[lane] ─────────────────────── */
static inline int encode_sym(bwx_t* bw, uint32_t* pstate,
                              int s, uint32_t freq,
                              const uint32_t* cumul,
                              const uint32_t* stateTable)
{
    uint32_t st = *pstate;
    int nb = 0; uint32_t tmp = st;
    while (tmp >= (freq << 1)) { nb++; tmp >>= 1; }
    if (bwx_put(bw, st & ((1u << nb) - 1), nb) != 0) return -1;
    *pstate = stateTable[cumul[s] + (tmp - freq)];
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  5.  Compress  (4-stream interleaved, magic 0xFC)
 * ═══════════════════════════════════════════════════════════════ */
size_t mcx_fse_compress(void* dst, size_t dst_cap,
                         const void* src, size_t src_size)
{
    const uint8_t* in  = (const uint8_t*)src;
    uint8_t*       out = (uint8_t*)dst;

    if (src_size == 0 || dst_cap < 24) return 0;

    /* Count */
    uint32_t counts[MCX_FSE_MAX_SYMBOLS] = {0};
    for (size_t i = 0; i < src_size; i++) counts[in[i]]++;

    /* RLE */
    int unique = 0, ssym = 0;
    for (int i = 0; i < MCX_FSE_MAX_SYMBOLS; i++)
        if (counts[i]) { unique++; ssym = i; }
    if (unique <= 1) {
        if (dst_cap < 6) return 0;
        out[0] = 0xFF; out[1] = (uint8_t)ssym;
        uint32_t sz = (uint32_t)src_size;
        memcpy(out + 2, &sz, 4);
        return 6;
    }

    /* Normalize */
    uint16_t nf[MCX_FSE_MAX_SYMBOLS] = {0};
    int max_sym = -1;
    if (mcx_fse_normalize_freq(nf, &max_sym, counts, MCX_FSE_MAX_SYMBOLS, RLOG) != 0)
        return 0;

    /* Build stateTable */
    uint32_t cumul[MCX_FSE_MAX_SYMBOLS + 1];
    uint32_t* stateTable = build_state_table(nf, max_sym, cumul);
    if (!stateTable) return 0;

    /* Header: [0xFC][max_sym][nf…][orig_size][dec_init[4]] */
    size_t hdr_size = 1 + 1 + (size_t)(max_sym + 1) * 2 + 4 + 4 * 4;
    if (dst_cap < hdr_size + 8) { free(stateTable); return 0; }

    uint8_t* op = out;
    *op++ = 0xFC;
    *op++ = (uint8_t)max_sym;
    for (int i = 0; i <= max_sym; i++) {
        op[0] = (uint8_t)(nf[i] & 0xFF); op[1] = (uint8_t)(nf[i] >> 8); op += 2;
    }
    uint32_t orig32 = (uint32_t)src_size;
    memcpy(op, &orig32, 4); op += 4;
    uint8_t* dec_init_slot = op; op += 16;  /* 4 × uint32 filled after encoding */

    /* Encode backward; 4 states interleaved round-robin */
    size_t tmp_cap = src_size * 2 + 256;
    uint8_t* tmp = (uint8_t*)malloc(tmp_cap);
    if (!tmp) { free(stateTable); return 0; }

    bwx_t bw;
    bwx_init(&bw, tmp, tmp_cap);

    uint32_t states[4] = {(uint32_t)TS, (uint32_t)TS,
                           (uint32_t)TS, (uint32_t)TS};

    for (size_t i = src_size; i > 0; ) {
        i--;
        int lane = (int)(i & 3);
        int s    = (int)in[i];
        if (encode_sym(&bw, &states[lane], s, nf[s], cumul, stateTable) != 0) {
            free(tmp); free(stateTable); return 0;
        }
    }
    free(stateTable);

    /* Sentinel bit */
    bwx_put(&bw, 1, 1);
    size_t bs_size = bwx_done(&bw, tmp);

    /* Store 4 dec_init values */
    for (int l = 0; l < 4; l++) {
        uint32_t di = states[l] - (uint32_t)TS;
        memcpy(dec_init_slot + l * 4, &di, 4);
    }

    size_t total = hdr_size + bs_size;
    if (total >= src_size || total > dst_cap) { free(tmp); return 0; }

    memcpy(op, tmp, bs_size);
    free(tmp);
    return total;
}

/* ═══════════════════════════════════════════════════════════════
 *  6.  Decompress  (handles 0xFC 4-stream, 0xFD 1-stream, 0xFF RLE)
 * ═══════════════════════════════════════════════════════════════ */
size_t mcx_fse_decompress(void* dst, size_t dst_cap,
                           const void* src, size_t src_size)
{
    const uint8_t* in  = (const uint8_t*)src;
    uint8_t*       out = (uint8_t*)dst;

    if (src_size == 0) return 0;

    /* RLE */
    if (in[0] == 0xFF && src_size >= 6) {
        uint32_t c; memcpy(&c, in + 2, 4);
        if ((size_t)c > dst_cap) return 0;
        memset(out, in[1], c);
        return (size_t)c;
    }

    /* Legacy Huffman */
    if (in[0] == 0xFE) goto huffman_decode;

    /* ── tANS 1-stream (0xFD) ─────────────────────────────── */
    if (in[0] == 0xFD)
    {
        const uint8_t* ip = in + 1;
        if (src_size < 10) return 0;
        int max_sym = (int)*ip++;

        uint16_t nf[MCX_FSE_MAX_SYMBOLS] = {0};
        for (int i = 0; i <= max_sym; i++) {
            nf[i] = (uint16_t)(ip[0] | ((uint16_t)ip[1] << 8)); ip += 2;
        }
        uint32_t orig32; memcpy(&orig32, ip, 4); ip += 4;
        size_t orig_size = (size_t)orig32;
        if (orig_size > dst_cap) return 0;

        uint32_t dec_init; memcpy(&dec_init, ip, 4); ip += 4;
        if (dec_init >= (uint32_t)TS) return 0;

        size_t bs_size = src_size - (size_t)(ip - in);

        mcx_fse_dec_table* dt = (mcx_fse_dec_table*)malloc(sizeof(mcx_fse_dec_table));
        if (!dt) return 0;
        if (mcx_fse_build_dec_table(dt, nf, max_sym, RLOG) != 0) { free(dt); return 0; }

        brd_t br;
        if (brd_init(&br, ip, bs_size) != 0) { free(dt); return 0; }

        uint32_t state = dec_init;
        size_t pos = 0;
        while (pos < orig_size) {
            if (state >= (uint32_t)TS) { pos = 0; break; }
            const mcx_fse_dec_entry* e = &dt->table[state];
            out[pos++] = e->symbol;
            state = (uint32_t)e->new_state + brd_read(&br, e->nb_bits);
        }
        free(dt);
        return pos;
    }

    /* ── tANS 4-stream interleaved (0xFC) ─────────────────── */
    if (in[0] != 0xFC) return 0;
    {
        const uint8_t* ip = in + 1;
        if (src_size < 22) return 0;
        int max_sym = (int)*ip++;

        uint16_t nf[MCX_FSE_MAX_SYMBOLS] = {0};
        for (int i = 0; i <= max_sym; i++) {
            nf[i] = (uint16_t)(ip[0] | ((uint16_t)ip[1] << 8)); ip += 2;
        }
        uint32_t orig32; memcpy(&orig32, ip, 4); ip += 4;
        size_t orig_size = (size_t)orig32;
        if (orig_size > dst_cap) return 0;

        uint32_t dec_init[4];
        for (int l = 0; l < 4; l++) {
            memcpy(&dec_init[l], ip, 4); ip += 4;
            if (dec_init[l] >= (uint32_t)TS) return 0;
        }

        size_t bs_size = src_size - (size_t)(ip - in);

        mcx_fse_dec_table* dt = (mcx_fse_dec_table*)malloc(sizeof(mcx_fse_dec_table));
        if (!dt) return 0;
        if (mcx_fse_build_dec_table(dt, nf, max_sym, RLOG) != 0) { free(dt); return 0; }

        brd_t br;
        if (brd_init(&br, ip, bs_size) != 0) { free(dt); return 0; }

        uint32_t states[4] = {dec_init[0], dec_init[1], dec_init[2], dec_init[3]};
        size_t pos = 0;

        /* Main 4-symbol unrolled loop — ILP: 4 independent table lookups */
        size_t nblocks = orig_size >> 2;   /* orig_size / 4 */
        for (size_t b = 0; b < nblocks; b++, pos += 4) {
            /* Issue 4 independent loads from dec_table (OOO CPU can pipeline) */
            const mcx_fse_dec_entry* e0 = &dt->table[states[0]];
            const mcx_fse_dec_entry* e1 = &dt->table[states[1]];
            const mcx_fse_dec_entry* e2 = &dt->table[states[2]];
            const mcx_fse_dec_entry* e3 = &dt->table[states[3]];
            /* Extract symbols before state transitions */
            out[pos+0] = e0->symbol;
            out[pos+1] = e1->symbol;
            out[pos+2] = e2->symbol;
            out[pos+3] = e3->symbol;
            /* Update states (bit reads are sequential in the shared stream) */
            states[0] = (uint32_t)e0->new_state + brd_read(&br, e0->nb_bits);
            states[1] = (uint32_t)e1->new_state + brd_read(&br, e1->nb_bits);
            states[2] = (uint32_t)e2->new_state + brd_read(&br, e2->nb_bits);
            states[3] = (uint32_t)e3->new_state + brd_read(&br, e3->nb_bits);
        }

        /* Tail: 0-3 remaining symbols */
        while (pos < orig_size) {
            int lane = (int)(pos & 3);
            if (states[lane] >= (uint32_t)TS) { pos = 0; break; }
            const mcx_fse_dec_entry* e = &dt->table[states[lane]];
            out[pos++] = e->symbol;
            states[lane] = (uint32_t)e->new_state + brd_read(&br, e->nb_bits);
        }

        free(dt);
        return pos;
    }

huffman_decode:
    {
#define HB 15
#define HS (1 << HB)
        typedef struct { uint8_t sym, nb; } hde;

        const uint8_t* ip = in + 1;
        int mx = *ip++;

        uint8_t cl[MCX_FSE_MAX_SYMBOLS] = {0};
        for (int i = 0; i <= mx; i++) cl[i] = *ip++;

        uint32_t o32; memcpy(&o32, ip, 4); ip += 4;
        size_t orig = (size_t)o32;
        if (orig > dst_cap) return 0;

        int lc[HB + 1] = {0};
        for (int i = 0; i <= mx; i++) if (cl[i]) lc[cl[i]]++;
        uint16_t nc[HB + 1]; nc[0] = 0;
        uint16_t code = 0;
        for (int b = 1; b <= HB; b++) { code = (code + (uint16_t)lc[b-1]) << 1; nc[b] = code; }
        uint16_t codes[MCX_FSE_MAX_SYMBOLS] = {0};
        for (int i = 0; i <= mx; i++) if (cl[i]) codes[i] = nc[cl[i]]++;

        hde* tbl = (hde*)calloc(HS, sizeof(hde));
        if (!tbl) return 0;
        for (int s = 0; s <= mx; s++) {
            int len = cl[s]; if (!len || len > HB) continue;
            int pad = HB - len; uint32_t base = (uint32_t)codes[s] << pad;
            for (uint32_t j = 0; j < (1u << pad); j++) {
                tbl[base+j].sym = (uint8_t)s; tbl[base+j].nb = (uint8_t)len;
            }
        }

        size_t hdr = (size_t)(ip - in);
        const uint8_t* bs = ip; size_t bsz = src_size - hdr;
        uint32_t acc = 0; int ab = 0; size_t bp = 0;
        while (ab <= 24 && bp < bsz) { acc = (acc << 8) | bs[bp++]; ab += 8; }

        size_t opos = 0;
        while (opos < orig) {
            if (ab < 1) break;
            int pk = ab < HB ? ab : HB;
            uint32_t idx = (acc >> (ab - pk)) & ((1u << pk) - 1);
            if (pk < HB) idx <<= (HB - pk);
            hde* e = &tbl[idx];
            if (!e->nb) break;
            out[opos++] = e->sym;
            ab -= e->nb; acc &= (1u << ab) - 1;
            while (ab <= 24 && bp < bsz) { acc = (acc << 8) | bs[bp++]; ab += 8; }
        }
        free(tbl);
        return opos;
#undef HB
#undef HS
    }
}
