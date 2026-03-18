/**
 * @file delta.c
 * @brief Delta encoding — stores differences between consecutive bytes.
 *
 * Extremely effective for numeric/time-series data where consecutive
 * values are close. Inspired by how DNA stores variations as SNPs.
 */

#include "preprocess.h"

void mcx_delta_encode(uint8_t* data, size_t size)
{
    if (data == NULL || size <= 1) return;

    /* Process in reverse to allow in-place operation */
    for (size_t i = size - 1; i >= 1; i--) {
        data[i] = data[i] - data[i - 1];
    }
    /* data[0] stays as the reference value */
}

void mcx_delta_decode(uint8_t* data, size_t size)
{
    if (data == NULL || size <= 1) return;

    /* Reconstruct from cumulative sum */
    for (size_t i = 1; i < size; i++) {
        data[i] = data[i] + data[i - 1];
    }
}

/* ─── Move-to-Front ──────────────────────────────────────────────────── */

void mcx_mtf_encode(uint8_t* data, size_t size)
{
    if (data == NULL || size == 0) return;

    /* Initialize symbol table with identity mapping and reverse lookup.
     * table[pos] = symbol at position pos
     * rtable[symbol] = position of symbol
     * This makes the find step O(1) instead of O(256). */
    uint8_t table[256];
    uint8_t rtable[256];
    for (int i = 0; i < 256; i++) {
        table[i] = (uint8_t)i;
        rtable[i] = (uint8_t)i;
    }

    for (size_t i = 0; i < size; i++) {
        uint8_t sym = data[i];
        uint8_t pos = rtable[sym]; /* O(1) lookup */

        /* Output position */
        data[i] = pos;

        /* Move symbol to front: shift table[0..pos-1] right by 1 */
        if (pos > 0) {
            /* Update reverse lookup for shifted symbols */
            for (uint8_t j = 0; j < pos; j++) {
                rtable[table[j]]++;
            }
            memmove(table + 1, table, pos);
            table[0] = sym;
            rtable[sym] = 0;
        }
    }
}

void mcx_mtf_decode(uint8_t* data, size_t size)
{
    if (data == NULL || size == 0) return;

    uint8_t table[256];
    for (int i = 0; i < 256; i++) {
        table[i] = (uint8_t)i;
    }

    for (size_t i = 0; i < size; i++) {
        uint8_t pos = data[i];
        uint8_t sym = table[pos];

        /* Output original symbol */
        data[i] = sym;

        /* Optimized move-to-front: use memmove for bulk shift */
        if (pos > 0) {
            memmove(table + 1, table, pos);
        }
        table[0] = sym;
    }
}
