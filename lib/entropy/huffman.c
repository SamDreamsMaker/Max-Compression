/**
 * @file huffman.c
 * @brief Huffman coding — variable-length prefix codes.
 *
 * Classic entropy coder. Will serve as fallback and comparison baseline
 * until ANS is implemented. Huffman is simpler but limited to
 * integer bit lengths per symbol (vs ANS which can do fractional bits).
 */

#include "entropy.h"

/* ─── Huffman Tree Node ──────────────────────────────────────────────── */

typedef struct {
    uint32_t freq;
    int16_t  left;    /* child index or -1 */
    int16_t  right;   /* child index or -1 */
    int16_t  symbol;  /* symbol or -1 for internal nodes */
} huff_node_t;

/* ─── Build Huffman Tree ─────────────────────────────────────────────── */

int mcx_huffman_build(mcx_huffman_table_t* table)
{
    if (table == NULL) return -1;

    /* Count active symbols */
    table->num_symbols = 0;
    for (int i = 0; i < 256; i++) {
        if (table->freq[i] > 0) table->num_symbols++;
    }

    if (table->num_symbols == 0) return -1;

    /* Special case: single symbol */
    if (table->num_symbols == 1) {
        for (int i = 0; i < 256; i++) {
            if (table->freq[i] > 0) {
                table->code[i] = 0;
                table->bits[i] = 1;
            } else {
                table->code[i] = 0;
                table->bits[i] = 0;
            }
        }
        return 0;
    }

    /* Build tree using array-based priority queue (simple approach) */
    huff_node_t nodes[511]; /* max 256 leaves + 255 internal */
    int node_count = 0;
    int active[511];
    int active_count = 0;

    /* Create leaf nodes */
    for (int i = 0; i < 256; i++) {
        if (table->freq[i] > 0) {
            nodes[node_count].freq   = table->freq[i];
            nodes[node_count].left   = -1;
            nodes[node_count].right  = -1;
            nodes[node_count].symbol = (int16_t)i;
            active[active_count++] = node_count;
            node_count++;
        }
    }

    /* Build tree bottom-up: merge two lowest-frequency nodes */
    while (active_count > 1) {
        /* Find two smallest */
        int min1 = 0, min2 = 1;
        if (nodes[active[min2]].freq < nodes[active[min1]].freq) {
            int tmp = min1; min1 = min2; min2 = tmp;
        }

        for (int i = 2; i < active_count; i++) {
            if (nodes[active[i]].freq < nodes[active[min1]].freq) {
                min2 = min1;
                min1 = i;
            } else if (nodes[active[i]].freq < nodes[active[min2]].freq) {
                min2 = i;
            }
        }

        /* Create internal node */
        int left  = active[min1];
        int right = active[min2];
        nodes[node_count].freq   = nodes[left].freq + nodes[right].freq;
        nodes[node_count].left   = (int16_t)left;
        nodes[node_count].right  = (int16_t)right;
        nodes[node_count].symbol = -1;

        /* Remove the two nodes, add the new one */
        /* Remove higher index first to preserve lower index */
        int hi = (min1 > min2) ? min1 : min2;
        int lo = (min1 < min2) ? min1 : min2;
        active[hi] = active[--active_count];
        if (lo < active_count) {
            active[lo] = active[--active_count];
        } else {
            active_count--;
        }
        active[active_count++] = node_count;
        node_count++;
    }

    /* Traverse tree to extract codes */
    memset(table->code, 0, sizeof(table->code));
    memset(table->bits, 0, sizeof(table->bits));

    /* Iterative traversal using a stack */
    struct { int node; uint16_t code; uint8_t depth; } stack[512];
    int sp = 0;
    stack[sp].node  = active[0];
    stack[sp].code  = 0;
    stack[sp].depth = 0;
    sp++;

    while (sp > 0) {
        sp--;
        int      n = stack[sp].node;
        uint16_t c = stack[sp].code;
        uint8_t  d = stack[sp].depth;

        if (nodes[n].symbol >= 0) {
            /* Leaf node */
            int s = nodes[n].symbol;
            table->code[s] = c;
            table->bits[s] = (d == 0) ? 1 : d;
        } else {
            /* Internal node — push children */
            if (d < MCX_HUFFMAN_MAX_BITS) {
                stack[sp].node  = nodes[n].left;
                stack[sp].code  = (uint16_t)(c << 1);
                stack[sp].depth = d + 1;
                sp++;
                stack[sp].node  = nodes[n].right;
                stack[sp].code  = (uint16_t)((c << 1) | 1);
                stack[sp].depth = d + 1;
                sp++;
            }
        }
    }

    return 0;
}

/* ─── Huffman Compress ───────────────────────────────────────────────── */

size_t mcx_huffman_compress(uint8_t* dst, size_t dst_cap,
                            const uint8_t* src, size_t src_size,
                            mcx_huffman_table_t* table)
{
    if (dst == NULL || src == NULL || src_size == 0) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }

    /* Build frequency table if not provided */
    mcx_huffman_table_t local_table;
    if (table == NULL) {
        table = &local_table;
        memset(table->freq, 0, sizeof(table->freq));
        for (size_t i = 0; i < src_size; i++) {
            table->freq[src[i]]++;
        }
        if (mcx_huffman_build(table) != 0) {
            return MCX_ERROR(MCX_ERR_GENERIC);
        }
    }

    /* Write header: frequency table (simplified: 256 x uint32_t)
     * + original size (uint32_t) */
    size_t header_size = 256 * 4 + 4;
    if (dst_cap < header_size) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);

    /* Write original size */
    uint32_t orig_size = (uint32_t)src_size;
    memcpy(dst, &orig_size, 4);

    /* Write frequencies */
    memcpy(dst + 4, table->freq, 256 * 4);

    /* Write encoded bits */
    size_t di = header_size;
    uint32_t bit_buffer = 0;
    int bit_count = 0;

    for (size_t i = 0; i < src_size; i++) {
        uint16_t code = table->code[src[i]];
        uint8_t  nbits = table->bits[src[i]];

        /* Add bits to buffer (MSB first) */
        bit_buffer = (bit_buffer << nbits) | code;
        bit_count += nbits;

        /* Flush complete bytes */
        while (bit_count >= 8) {
            bit_count -= 8;
            if (di >= dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
            dst[di++] = (uint8_t)(bit_buffer >> bit_count);
        }
    }

    /* Flush remaining bits */
    if (bit_count > 0) {
        if (di >= dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        dst[di++] = (uint8_t)(bit_buffer << (8 - bit_count));
    }

    return di;
}

/* ─── Huffman Decompress (table-based fast decoder) ──────────────────── */

/*
 * Table-based Huffman decoder: uses a HUFF_FAST_BITS-wide lookup table
 * for O(1) decoding of short codes. Codes longer than HUFF_FAST_BITS
 * fall back to a secondary tree walk via subtable entries.
 *
 * Each table entry packs: symbol (low 16 bits) + code length (high 16 bits).
 * For entries where code length > HUFF_FAST_BITS, the entry stores
 * a tree-node index (symbol field = node index, length = 0xFF sentinel)
 * and we finish decoding via the tree.
 */
#define HUFF_FAST_BITS   9
#define HUFF_FAST_SIZE   (1 << HUFF_FAST_BITS)  /* 512 entries */
#define HUFF_ENTRY(sym, len) (((uint32_t)(len) << 16) | (uint16_t)(sym))
#define HUFF_ENTRY_SYM(e)   ((uint16_t)((e) & 0xFFFF))
#define HUFF_ENTRY_LEN(e)   ((uint8_t)((e) >> 16))
#define HUFF_SENTINEL        0xFF  /* length sentinel: need tree walk */

size_t mcx_huffman_decompress(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size)
{
    if (dst == NULL || src == NULL) return MCX_ERROR(MCX_ERR_GENERIC);

    size_t header_size = 256 * 4 + 4;
    if (src_size < header_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);

    /* Read original size */
    uint32_t orig_size;
    memcpy(&orig_size, src, 4);

    if ((size_t)orig_size > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);

    /* Read frequencies and rebuild table */
    mcx_huffman_table_t table;
    memcpy(table.freq, src + 4, 256 * 4);
    if (mcx_huffman_build(&table) != 0) {
        return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    }

    /* Reconstruct the tree (needed for building fast table + fallback) */
    huff_node_t nodes[511];
    int node_count = 0;
    int active[511];
    int active_count = 0;

    for (int i = 0; i < 256; i++) {
        if (table.freq[i] > 0) {
            nodes[node_count].freq   = table.freq[i];
            nodes[node_count].left   = -1;
            nodes[node_count].right  = -1;
            nodes[node_count].symbol = (int16_t)i;
            active[active_count++] = node_count;
            node_count++;
        }
    }

    while (active_count > 1) {
        int min1 = 0, min2 = 1;
        if (nodes[active[min2]].freq < nodes[active[min1]].freq) {
            int tmp = min1; min1 = min2; min2 = tmp;
        }
        for (int i = 2; i < active_count; i++) {
            if (nodes[active[i]].freq < nodes[active[min1]].freq) {
                min2 = min1; min1 = i;
            } else if (nodes[active[i]].freq < nodes[active[min2]].freq) {
                min2 = i;
            }
        }
        int left = active[min1], right = active[min2];
        nodes[node_count].freq   = nodes[left].freq + nodes[right].freq;
        nodes[node_count].left   = (int16_t)left;
        nodes[node_count].right  = (int16_t)right;
        nodes[node_count].symbol = -1;

        int hi = (min1 > min2) ? min1 : min2;
        int lo = (min1 < min2) ? min1 : min2;
        active[hi] = active[--active_count];
        if (lo < active_count) active[lo] = active[--active_count];
        else active_count--;
        active[active_count++] = node_count;
        node_count++;
    }

    int root = active[0];

    /* Build fast lookup table from canonical codes */
    uint32_t fast_table[HUFF_FAST_SIZE];
    memset(fast_table, 0, sizeof(fast_table));

    for (int sym = 0; sym < 256; sym++) {
        uint8_t nbits = table.bits[sym];
        if (nbits == 0) continue;

        if (nbits <= HUFF_FAST_BITS) {
            /* Fill all table entries for this code (pad remaining bits) */
            uint16_t code = table.code[sym];
            int pad_bits = HUFF_FAST_BITS - nbits;
            uint32_t base_idx = (uint32_t)code << pad_bits;
            uint32_t count = 1u << pad_bits;
            for (uint32_t j = 0; j < count; j++) {
                fast_table[base_idx + j] = HUFF_ENTRY(sym, nbits);
            }
        }
    }

    /* Mark entries for long codes: walk tree HUFF_FAST_BITS deep, store node index */
    for (uint32_t idx = 0; idx < HUFF_FAST_SIZE; idx++) {
        if (HUFF_ENTRY_LEN(fast_table[idx]) != 0) continue;
        /* This prefix doesn't map to a short code — walk the tree */
        int node = root;
        for (int b = HUFF_FAST_BITS - 1; b >= 0 && nodes[node].symbol < 0; b--) {
            int bit = (idx >> b) & 1;
            node = bit ? nodes[node].right : nodes[node].left;
            if (node < 0) break;
        }
        if (node >= 0 && nodes[node].symbol >= 0) {
            /* Resolved within HUFF_FAST_BITS (shouldn't normally happen, but handle it) */
            fast_table[idx] = HUFF_ENTRY(nodes[node].symbol, HUFF_FAST_BITS);
        } else if (node >= 0) {
            /* Store tree-node index for fallback; use sentinel length */
            fast_table[idx] = HUFF_ENTRY(node, HUFF_SENTINEL);
        }
    }

    /* Decode bit stream using fast table + bit buffer */
    size_t si = header_size;
    size_t di = 0;

    /* 64-bit buffer: accumulate bits from source bytes, MSB-first.
     * With 64 bits we can hold up to 56 bits of data, enough for
     * ~6 symbols between refills (vs ~3 with 32-bit). */
    uint64_t bit_buf = 0;
    int      bit_cnt = 0;

    /* Refill macro: fill buffer to at least 56 bits when possible */
    #define HUFF_REFILL() do { \
        while (bit_cnt <= 56 && si < src_size) { \
            bit_buf = (bit_buf << 8) | src[si++]; \
            bit_cnt += 8; \
        } \
    } while (0)

    /* Decode one symbol from fast table; returns 0 on success, -1 on error */
    #define HUFF_DECODE_ONE() do { \
        uint64_t idx_ = (bit_buf >> (bit_cnt - HUFF_FAST_BITS)) & (HUFF_FAST_SIZE - 1); \
        if (bit_cnt < HUFF_FAST_BITS) \
            idx_ = (bit_buf << (HUFF_FAST_BITS - bit_cnt)) & (HUFF_FAST_SIZE - 1); \
        uint32_t e_ = fast_table[idx_]; \
        uint8_t  l_ = HUFF_ENTRY_LEN(e_); \
        if (l_ != 0 && l_ != HUFF_SENTINEL) { \
            dst[di++] = (uint8_t)HUFF_ENTRY_SYM(e_); \
            bit_cnt -= l_; \
        } else if (l_ == HUFF_SENTINEL) { \
            bit_cnt -= HUFF_FAST_BITS; \
            int node_ = (int)HUFF_ENTRY_SYM(e_); \
            while (nodes[node_].symbol < 0) { \
                HUFF_REFILL(); \
                if (bit_cnt < 1) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); \
                int bit_ = (bit_buf >> (bit_cnt - 1)) & 1; \
                bit_cnt--; \
                node_ = bit_ ? nodes[node_].right : nodes[node_].left; \
                if (node_ < 0) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); \
            } \
            dst[di++] = (uint8_t)nodes[node_].symbol; \
        } else { \
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); \
        } \
    } while (0)

    /* Unrolled 2-symbol-per-iteration fast path.
     * 64-bit refill guarantees 56+ bits in buffer; with max 15-bit codes,
     * 2 symbols consume at most 30 bits before next refill. */
    while (di + 1 < orig_size) {
        HUFF_REFILL();
        if (bit_cnt < 2) break;
        HUFF_DECODE_ONE();
        /* Second symbol: enough bits remain for most codes */
        if (di < orig_size && bit_cnt >= 1) {
            HUFF_DECODE_ONE();
        }
    }
    /* Tail: one remaining symbol */
    while (di < orig_size) {
        HUFF_REFILL();
        if (bit_cnt < 1) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        HUFF_DECODE_ONE();
    }
    #undef HUFF_DECODE_ONE

    #undef HUFF_REFILL
    return orig_size;
}
