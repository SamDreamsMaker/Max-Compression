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

    /* Initialize symbol table with identity mapping */
    uint8_t table[256];
    for (int i = 0; i < 256; i++) {
        table[i] = (uint8_t)i;
    }

    for (size_t i = 0; i < size; i++) {
        uint8_t sym = data[i];
        uint8_t pos = 0;

        /* Find symbol position in table */
        while (table[pos] != sym) {
            pos++;
        }

        /* Output position */
        data[i] = pos;

        /* Move symbol to front */
        for (uint8_t j = pos; j > 0; j--) {
            table[j] = table[j - 1];
        }
        table[0] = sym;
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

        /* Move symbol to front */
        for (uint8_t j = pos; j > 0; j--) {
            table[j] = table[j - 1];
        }
        table[0] = sym;
    }
}
