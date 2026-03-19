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

/* ─── Sorted Integer Delta ────────────────────────────────────────────── */

static inline uint16_t read16le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline void write16le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}
static inline uint32_t read32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void write32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int mcx_sorted_int_detect(const uint8_t* data, size_t size)
{
    if (!data || size < 64) return 0;

    /* Sample up to 4KB from the start */
    size_t sample = size < 4096 ? size : 4096;

    /* Try 16-bit LE sorted detection first (prefer tighter width) */
    if (size >= 4 && (size % 2) == 0) {
        size_t n16 = sample / 2;
        if (n16 >= 4) {
            size_t sorted = 0;
            uint16_t prev = read16le(data);
            for (size_t i = 1; i < n16; i++) {
                uint16_t cur = read16le(data + i * 2);
                if (cur >= prev) sorted++;
                prev = cur;
            }
            double ratio = (double)sorted / (double)(n16 - 1);
            if (ratio >= 0.85) return 2;
        }
    }

    /* Try 32-bit LE sorted detection */
    if (size >= 8 && (size % 4) == 0) {
        size_t n32 = sample / 4;
        if (n32 >= 4) {
            size_t sorted = 0;
            uint32_t prev = read32le(data);
            for (size_t i = 1; i < n32; i++) {
                uint32_t cur = read32le(data + i * 4);
                if (cur >= prev) sorted++;
                prev = cur;
            }
            double ratio = (double)sorted / (double)(n32 - 1);
            if (ratio >= 0.85) return 4;
        }
    }

    return 0;
}

void mcx_int_delta_encode(uint8_t* data, size_t size, int width)
{
    if (!data || size < (size_t)(width * 2)) return;

    if (width == 4) {
        size_t n = size / 4;
        /* Process in reverse for in-place */
        for (size_t i = n - 1; i >= 1; i--) {
            uint32_t cur  = read32le(data + i * 4);
            uint32_t prev = read32le(data + (i - 1) * 4);
            write32le(data + i * 4, cur - prev);
        }
    } else if (width == 2) {
        size_t n = size / 2;
        for (size_t i = n - 1; i >= 1; i--) {
            uint16_t cur  = read16le(data + i * 2);
            uint16_t prev = read16le(data + (i - 1) * 2);
            write16le(data + i * 2, cur - prev);
        }
    }
}

void mcx_int_delta_decode(uint8_t* data, size_t size, int width)
{
    if (!data || size < (size_t)(width * 2)) return;

    if (width == 4) {
        size_t n = size / 4;
        for (size_t i = 1; i < n; i++) {
            uint32_t prev  = read32le(data + (i - 1) * 4);
            uint32_t delta = read32le(data + i * 4);
            write32le(data + i * 4, prev + delta);
        }
    } else if (width == 2) {
        size_t n = size / 2;
        for (size_t i = 1; i < n; i++) {
            uint16_t prev  = read16le(data + (i - 1) * 2);
            uint16_t delta = read16le(data + i * 2);
            write16le(data + i * 2, prev + delta);
        }
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
