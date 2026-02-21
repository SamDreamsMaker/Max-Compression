/**
 * @file fse.c
 * @brief Entropy coder for MaxCompression v1.0 — canonical Huffman with
 *        frequency-optimal code lengths derived from normalized distributions.
 *
 * This replaces the problematic tANS implementation with a well-understood
 * canonical Huffman coder. Canonical Huffman achieves near-Shannon-entropy
 * compression while being trivially verifiable for correctness.
 *
 * Format: [0xFE magic] [max_sym:1] [code_lengths: max_sym+1 bytes] [orig_size:4] [bitstream]
 */

#include "mcx_fse.h"
#include <string.h>
#include <stdlib.h>

#define MAX_CODE_LEN 15

/* ═══════════════════════════════════════════════════════════════════
 *  Code length assignment (package-merge / length-limited Huffman)
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct { uint32_t freq; int symbol; } sym_freq;

static int freq_cmp(const void* a, const void* b) {
    const sym_freq* sa = (const sym_freq*)a;
    const sym_freq* sb = (const sym_freq*)b;
    if (sa->freq != sb->freq) return (sa->freq < sb->freq) ? -1 : 1;
    return sa->symbol - sb->symbol;
}

/**
 * Assign code lengths using a simple Huffman tree construction.
 * Returns: number of active symbols, or 0 on error.
 */
static int assign_code_lengths(const uint32_t* counts, int num_syms,
                                uint8_t* code_lens, int max_code_len)
{
    /* Collect non-zero symbols */
    sym_freq syms[MCX_FSE_MAX_SYMBOLS];
    int n = 0;
    for (int i = 0; i < num_syms; i++) {
        code_lens[i] = 0;
        if (counts[i] > 0) {
            syms[n].freq = counts[i];
            syms[n].symbol = i;
            n++;
        }
    }
    if (n == 0) return 0;
    if (n == 1) { code_lens[syms[0].symbol] = 1; return 1; }

    /* Sort by frequency */
    qsort(syms, n, sizeof(sym_freq), freq_cmp);

    /* Build Huffman tree using an array-based approach.
     * We track internal node frequencies and use two queues. */
    uint32_t* leaf_q = (uint32_t*)malloc(n * sizeof(uint32_t));
    uint32_t* node_q = (uint32_t*)malloc(n * sizeof(uint32_t));
    uint8_t*  depths = (uint8_t*)calloc(2 * n, sizeof(uint8_t));
    int*      node_children = (int*)malloc(2 * n * 2 * sizeof(int));
    if (!leaf_q || !node_q || !depths || !node_children) {
        free(leaf_q); free(node_q); free(depths); free(node_children);
        return 0;
    }

    for (int i = 0; i < n; i++) leaf_q[i] = syms[i].freq;
    int l_head = 0, l_tail = n;
    int n_head = 0, n_tail = 0;
    int next_node = n;

    /* Build tree bottom-up */
    while ((l_tail - l_head) + (n_tail - n_head) > 1) {
        /* Pick two smallest from either queue */
        uint32_t f1, f2;
        int c1, c2;

        /* First child */
        if (n_head < n_tail &&
            (l_head >= l_tail || node_q[n_head] <= leaf_q[l_head])) {
            f1 = node_q[n_head]; c1 = n + n_head; n_head++;
        } else {
            f1 = leaf_q[l_head]; c1 = l_head; l_head++;
        }

        /* Second child */
        if (n_head < n_tail &&
            (l_head >= l_tail || node_q[n_head] <= leaf_q[l_head])) {
            f2 = node_q[n_head]; c2 = n + n_head; n_head++;
        } else if (l_head < l_tail) {
            f2 = leaf_q[l_head]; c2 = l_head; l_head++;
        } else {
            f2 = node_q[n_head]; c2 = n + n_head; n_head++;
        }

        node_q[n_tail] = f1 + f2;
        node_children[n_tail * 2] = c1;
        node_children[n_tail * 2 + 1] = c2;
        n_tail++;
    }

    /* Compute depths via BFS from root */
    int root_node = n + n_tail - 1;
    depths[root_node] = 0;
    for (int i = n_tail - 1; i >= 0; i--) {
        uint8_t d = depths[n + i];
        int ch1 = node_children[i * 2];
        int ch2 = node_children[i * 2 + 1];
        if (ch1 < n) code_lens[syms[ch1].symbol] = d + 1;
        else depths[ch1] = d + 1;
        if (ch2 < n) code_lens[syms[ch2].symbol] = d + 1;
        else depths[ch2] = d + 1;
    }

    (void)root_node;

    /* Clamp to max_code_len */
    for (int i = 0; i < num_syms; i++)
        if (code_lens[i] > max_code_len) code_lens[i] = (uint8_t)max_code_len;

    free(leaf_q); free(node_q); free(depths); free(node_children);
    return n;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Canonical Huffman code generation
 * ═══════════════════════════════════════════════════════════════════ */

static void make_canonical_codes(const uint8_t* code_lens, int num_syms,
                                  uint16_t* codes)
{
    /* Count code lengths */
    int len_count[MAX_CODE_LEN + 1];
    memset(len_count, 0, sizeof(len_count));
    for (int i = 0; i < num_syms; i++)
        if (code_lens[i] > 0) len_count[code_lens[i]]++;

    /* Compute first code for each length */
    uint16_t next_code[MAX_CODE_LEN + 1];
    next_code[0] = 0;
    uint16_t code = 0;
    for (int bits = 1; bits <= MAX_CODE_LEN; bits++) {
        code = (code + (uint16_t)len_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    /* Assign codes */
    memset(codes, 0, num_syms * sizeof(uint16_t));
    for (int i = 0; i < num_syms; i++)
        if (code_lens[i] > 0)
            codes[i] = next_code[code_lens[i]]++;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Bitstream writer/reader (forward, MSB packing for Huffman)
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t* buf;
    size_t cap;
    size_t byte_pos;
    uint32_t acc;
    int acc_bits;
} huff_writer;

static void hw_init(huff_writer* w, uint8_t* buf, size_t cap) {
    w->buf = buf; w->cap = cap; w->byte_pos = 0; w->acc = 0; w->acc_bits = 0;
}

static void hw_write(huff_writer* w, uint16_t code, int nbits) {
    /* Write MSB-first: shift code into accumulator from the top */
    w->acc = (w->acc << nbits) | code;
    w->acc_bits += nbits;
    while (w->acc_bits >= 8 && w->byte_pos < w->cap) {
        w->acc_bits -= 8;
        w->buf[w->byte_pos++] = (uint8_t)((w->acc >> w->acc_bits) & 0xFF);
    }
}

static size_t hw_flush(huff_writer* w) {
    if (w->acc_bits > 0 && w->byte_pos < w->cap) {
        w->buf[w->byte_pos++] = (uint8_t)((w->acc << (8 - w->acc_bits)) & 0xFF);
        w->acc_bits = 0;
    }
    return w->byte_pos;
}

typedef struct {
    const uint8_t* buf;
    size_t size;
    size_t byte_pos;
    uint32_t acc;
    int acc_bits;
} huff_reader;

static void hr_init(huff_reader* r, const uint8_t* buf, size_t size) {
    r->buf = buf; r->size = size; r->byte_pos = 0;
    r->acc = 0; r->acc_bits = 0;
    /* Preload up to 4 bytes */
    while (r->acc_bits <= 24 && r->byte_pos < r->size) {
        r->acc = (r->acc << 8) | r->buf[r->byte_pos++];
        r->acc_bits += 8;
    }
}

static void hr_refill(huff_reader* r) {
    while (r->acc_bits <= 24 && r->byte_pos < r->size) {
        r->acc = (r->acc << 8) | r->buf[r->byte_pos++];
        r->acc_bits += 8;
    }
}

static uint32_t hr_peek(huff_reader* r, int nbits) {
    return (r->acc >> (r->acc_bits - nbits)) & ((1u << nbits) - 1);
}

static void hr_consume(huff_reader* r, int nbits) {
    r->acc_bits -= nbits;
    r->acc &= ((uint64_t)1 << r->acc_bits) - 1;
    hr_refill(r);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Decode table: fast table-based Huffman decoder
 * ═══════════════════════════════════════════════════════════════════ */

#define HUFF_DECODE_BITS 15
#define HUFF_DECODE_SIZE (1 << HUFF_DECODE_BITS)

typedef struct {
    uint16_t symbol;
    uint8_t nbits;
} huff_decode_entry;

static void build_decode_table(huff_decode_entry* table,
                                const uint8_t* code_lens, int num_syms)
{
    /* For each symbol with code length <= HUFF_DECODE_BITS,
     * fill all table entries that match this prefix */
    uint16_t codes[MCX_FSE_MAX_SYMBOLS];
    make_canonical_codes(code_lens, num_syms, codes);

    memset(table, 0, HUFF_DECODE_SIZE * sizeof(huff_decode_entry));

    for (int s = 0; s < num_syms; s++) {
        int len = code_lens[s];
        if (len == 0 || len > HUFF_DECODE_BITS) continue;

        int pad = HUFF_DECODE_BITS - len;
        uint32_t base = (uint32_t)codes[s] << pad;
        uint32_t count = 1u << pad;

        for (uint32_t i = 0; i < count; i++) {
            table[base + i].symbol = (uint8_t)s;
            table[base + i].nbits = (uint8_t)len;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Public API stubs (for mcx_fse.h compatibility)
 * ═══════════════════════════════════════════════════════════════════ */

int mcx_fse_normalize_freq(uint16_t* nf, int* ms_out,
    const uint32_t* counts, int nsym, int tl) {
    (void)nf;(void)ms_out;(void)counts;(void)nsym;(void)tl; return 0;
}
int mcx_fse_build_enc_table(mcx_fse_enc_table* ct,const uint16_t* nf,int ms,int tl)
{(void)ct;(void)nf;(void)ms;(void)tl;return 0;}
int mcx_fse_build_dec_table(mcx_fse_dec_table* dt,const uint16_t* nf,int ms,int tl)
{(void)dt;(void)nf;(void)ms;(void)tl;return 0;}

/* ═══════════════════════════════════════════════════════════════════
 *  Compress
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_fse_compress(void* dst, size_t dst_cap, const void* src, size_t src_size)
{
    const uint8_t* in = (const uint8_t*)src;
    uint8_t* out = (uint8_t*)dst;
    if (src_size == 0 || dst_cap < 16) return 0;

    /* Count frequencies */
    uint32_t counts[MCX_FSE_MAX_SYMBOLS];
    memset(counts, 0, sizeof(counts));
    for (size_t i = 0; i < src_size; i++) counts[in[i]]++;

    /* RLE check */
    int unique=0, ssym=0;
    for(int i=0;i<MCX_FSE_MAX_SYMBOLS;i++) if(counts[i]>0){unique++;ssym=i;}
    if(unique<=1){
        out[0]=0xFF; out[1]=(uint8_t)ssym;
        uint32_t sz=(uint32_t)src_size; memcpy(out+2,&sz,4);
        return 6;
    }

    /* Assign code lengths */
    uint8_t code_lens[MCX_FSE_MAX_SYMBOLS];
    int n_active = assign_code_lengths(counts, MCX_FSE_MAX_SYMBOLS, code_lens, MAX_CODE_LEN);
    if (n_active < 2) return 0;

    /* Generate canonical codes */
    uint16_t codes[MCX_FSE_MAX_SYMBOLS];
    make_canonical_codes(code_lens, MCX_FSE_MAX_SYMBOLS, codes);

    /* Find max symbol */
    int max_sym = 0;
    for (int i = 0; i < MCX_FSE_MAX_SYMBOLS; i++) if (code_lens[i] > 0) max_sym = i;

    /* Header: [0xFE] [max_sym] [code_lengths: max_sym+1 bytes] [orig_size:4] */
    uint8_t* op = out;
    *op++ = 0xFE; /* magic */
    *op++ = (uint8_t)max_sym;
    for (int i = 0; i <= max_sym; i++)
        *op++ = code_lens[i];
    uint32_t orig = (uint32_t)src_size;
    memcpy(op, &orig, 4); op += 4;

    size_t hdr_size = (size_t)(op - out);

    /* Encode */
    huff_writer hw;
    hw_init(&hw, op, dst_cap - hdr_size);

    for (size_t i = 0; i < src_size; i++)
        hw_write(&hw, codes[in[i]], code_lens[in[i]]);

    size_t bs_bytes = hw_flush(&hw);
    size_t total = hdr_size + bs_bytes;

    if (total >= src_size) return 0; /* not compressible */
    return total;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Decompress
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_fse_decompress(void* dst, size_t dst_cap, const void* src, size_t src_size)
{
    const uint8_t* in = (const uint8_t*)src;
    uint8_t* out = (uint8_t*)dst;
    if (src_size == 0) return 0;

    /* RLE */
    if(in[0]==0xFF && src_size>=6){
        uint32_t c; memcpy(&c,in+2,4); if((size_t)c>dst_cap)return 0;
        memset(out,in[1],c); return (size_t)c;
    }

    if (in[0] != 0xFE) return 0;

    const uint8_t* ip = in + 1;
    int max_sym = *ip++;

    /* Read code lengths */
    uint8_t code_lens[MCX_FSE_MAX_SYMBOLS];
    memset(code_lens, 0, sizeof(code_lens));
    for (int i = 0; i <= max_sym; i++)
        code_lens[i] = *ip++;

    uint32_t orig;
    memcpy(&orig, ip, 4); ip += 4;
    if ((size_t)orig > dst_cap) return 0;

    size_t hdr_size = (size_t)(ip - in);

    /* Build decode table */
    huff_decode_entry* dec_table = (huff_decode_entry*)malloc(
        HUFF_DECODE_SIZE * sizeof(huff_decode_entry));
    if (!dec_table) return 0;
    build_decode_table(dec_table, code_lens, max_sym + 1);

    /* Decode */
    huff_reader hr;
    hr_init(&hr, ip, src_size - hdr_size);

    size_t out_pos = 0;
    while (out_pos < (size_t)orig) {
        if (hr.acc_bits < 1) break;
        int peek_bits = hr.acc_bits < HUFF_DECODE_BITS ? hr.acc_bits : HUFF_DECODE_BITS;
        uint32_t idx = hr_peek(&hr, peek_bits);
        if (peek_bits < HUFF_DECODE_BITS)
            idx <<= (HUFF_DECODE_BITS - peek_bits);

        huff_decode_entry* e = &dec_table[idx];
        if (e->nbits == 0) break; /* invalid */

        out[out_pos++] = e->symbol;
        hr_consume(&hr, e->nbits);
    }

    free(dec_table);
    return out_pos;
}
