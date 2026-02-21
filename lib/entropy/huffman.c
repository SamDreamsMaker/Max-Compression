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

/* ─── Huffman Decompress ─────────────────────────────────────────────── */

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

    /* Build decode lookup: for each possible prefix, find the symbol */
    /* Simple bit-by-bit decoding using the tree (slow but correct) */
    /* TODO: Replace with table-based fast decoder */

    /* Reconstruct the tree for decoding */
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

    /* Decode bit stream */
    size_t si = header_size;
    size_t di = 0;
    int bit_pos = 7;

    while (di < orig_size && si < src_size) {
        int node = root;

        while (nodes[node].symbol < 0) {
            if (si >= src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);

            int bit = (src[si] >> bit_pos) & 1;
            bit_pos--;
            if (bit_pos < 0) { bit_pos = 7; si++; }

            node = bit ? nodes[node].right : nodes[node].left;
            if (node < 0) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }

        dst[di++] = (uint8_t)nodes[node].symbol;
    }

    return orig_size;
}
