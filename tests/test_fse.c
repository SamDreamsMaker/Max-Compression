/**
 * @file test_fse.c
 * @brief Round-trip unit tests for the FSE/tANS entropy coder.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../lib/entropy/mcx_fse.h"

static int test_roundtrip(const char* name, const uint8_t* data, size_t size)
{
    size_t comp_cap = size + 1024;
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec  = (uint8_t*)malloc(size + 16);

    if (!comp || !dec) {
        printf("  [FAIL] %s — alloc failed\n", name);
        free(comp); free(dec);
        return 1;
    }

    size_t comp_size = mcx_fse_compress(comp, comp_cap, data, size);
    if (comp_size == 0) {
        /* FSE returns 0 if data is incompressible — that's OK, not a failure */
        printf("  [SKIP] %-30s  %8zu (incompressible by FSE)\n", name, size);
        free(comp); free(dec);
        return 0;
    }

    double ratio = (double)size / (double)comp_size;

    size_t dec_size = mcx_fse_decompress(dec, size, comp, comp_size);

    if (dec_size != size) {
        printf("  [FAIL] %s — dec size %zu != orig %zu\n", name, dec_size, size);
        free(comp); free(dec);
        return 1;
    }

    if (memcmp(data, dec, size) != 0) {
        for (size_t i = 0; i < size; i++) {
            if (data[i] != dec[i]) {
                printf("  [FAIL] %s — mismatch at byte %zu: expected 0x%02X got 0x%02X\n",
                       name, i, data[i], dec[i]);
                break;
            }
        }
        free(comp); free(dec);
        return 1;
    }

    printf("  [PASS] %-30s  %8zu -> %8zu  (%.2fx)\n", name, size, comp_size, ratio);
    free(comp); free(dec);
    return 0;
}

int main(void)
{
    int failures = 0;
    printf("=== MaxCompression FSE/tANS Entropy Tests ===\n\n");

    /* 1. All same byte (RLE case) */
    {
        size_t n = 10000;
        uint8_t* d = (uint8_t*)malloc(n);
        memset(d, 'A', n);
        failures += test_roundtrip("10K all 'A' (RLE)", d, n);
        free(d);
    }

    /* 2. Two symbols (biased) */
    {
        size_t n = 10000;
        uint8_t* d = (uint8_t*)malloc(n);
        srand(42);
        for (size_t i = 0; i < n; i++)
            d[i] = (rand() % 10 < 8) ? 'a' : 'b'; /* 80% a, 20% b */
        failures += test_roundtrip("10K biased (80/20)", d, n);
        free(d);
    }

    /* 3. English text */
    {
        const char* text =
            "The quick brown fox jumps over the lazy dog. "
            "This sentence is a pangram containing every letter of the English alphabet. "
            "Compression algorithms work best on data with predictable statistical patterns. "
            "The Finite State Entropy coder approaches Shannon entropy limits using table-based ANS.";
        failures += test_roundtrip("English text", (const uint8_t*)text, strlen(text));
    }

    /* 4. Byte distribution (skewed) */
    {
        size_t n = 50000;
        uint8_t* d = (uint8_t*)malloc(n);
        srand(99);
        for (size_t i = 0; i < n; i++) {
            int r = rand() % 100;
            if (r < 40) d[i] = 0;
            else if (r < 70) d[i] = 1;
            else if (r < 90) d[i] = 2;
            else d[i] = (uint8_t)(r & 0xFF);
        }
        failures += test_roundtrip("50K skewed distribution", d, n);
        free(d);
    }

    /* 5. Random (should be incompressible) */
    {
        size_t n = 10000;
        uint8_t* d = (uint8_t*)malloc(n);
        srand(123);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)(rand() & 0xFF);
        failures += test_roundtrip("10K random", d, n);
        free(d);
    }

    printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures;
}
