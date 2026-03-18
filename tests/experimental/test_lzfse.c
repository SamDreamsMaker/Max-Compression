/**
 * @file test_lzfse.c
 * @brief Round-trip tests for multi-stream LZ77 + FSE (Phase 3).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../lib/lz/mcx_lz.h"

static int test_roundtrip(const char* name, const uint8_t* data, size_t size)
{
    size_t comp_cap = size * 2 + 1024;
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec  = (uint8_t*)malloc(size + 16);
    if (!comp || !dec) {
        printf("  [FAIL] %s — alloc failed\n", name);
        free(comp); free(dec); return 1;
    }

    size_t cs = mcx_lzfse_compress(comp, comp_cap, data, size);
    if (cs == 0) {
        printf("  [FAIL] %s — compress returned 0\n", name);
        free(comp); free(dec); return 1;
    }

    size_t ds = mcx_lzfse_decompress(dec, size, comp, cs);
    if (ds != size) {
        printf("  [FAIL] %s — dec size %zu != orig %zu\n", name, ds, size);
        free(comp); free(dec); return 1;
    }

    if (memcmp(data, dec, size) != 0) {
        for (size_t i = 0; i < size; i++) {
            if (data[i] != dec[i]) {
                printf("  [FAIL] %s — mismatch byte %zu: expected 0x%02X got 0x%02X\n",
                       name, i, data[i], dec[i]);
                break;
            }
        }
        free(comp); free(dec); return 1;
    }

    double ratio = (double)size / (double)cs;
    printf("  [PASS] %-32s  %8zu → %8zu  (%.2fx)\n", name, size, cs, ratio);
    free(comp); free(dec);
    return 0;
}

int main(void)
{
    int fail = 0;
    printf("=== Multi-stream LZ77+FSE (Phase 3) Tests ===\n\n");

    /* 1. All-zeros (RLE-like) */
    {
        size_t n = 32768;
        uint8_t* d = (uint8_t*)calloc(n, 1);
        fail += test_roundtrip("32K zeros", d, n);
        free(d);
    }

    /* 2. Repeated pattern */
    {
        size_t n = 32768;
        uint8_t* d = (uint8_t*)malloc(n);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)(i % 64);
        fail += test_roundtrip("32K cycling 64-pattern", d, n);
        free(d);
    }

    /* 3. English text (high compressibility) */
    {
        const char chunk[] =
            "the quick brown fox jumps over the lazy dog. "
            "compression works by finding repeated patterns in data. "
            "the finite state entropy coder approaches Shannon entropy limits. ";
        size_t clen = strlen(chunk);
        size_t n = 65536;
        uint8_t* d = (uint8_t*)malloc(n);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)chunk[i % clen];
        fail += test_roundtrip("64K repeated text", d, n);
        free(d);
    }

    /* 4. Binary data (mixed) */
    {
        size_t n = 16384;
        uint8_t* d = (uint8_t*)malloc(n);
        srand(42);
        for (size_t i = 0; i < n; i++) {
            /* 70% structured, 30% random */
            if (rand() % 10 < 7)
                d[i] = (uint8_t)(i & 0xFF);
            else
                d[i] = (uint8_t)(rand() & 0xFF);
        }
        fail += test_roundtrip("16K structured+random", d, n);
        free(d);
    }

    /* 5. Small input (just above minimum) */
    {
        size_t n = 32;
        uint8_t d[] = "ABCDABCDABCDABCDABCDABCDABCDABCD";
        fail += test_roundtrip("32B repeated ABCD", d, n);
    }

    /* 6. Large input for timing */
    {
        size_t n = 512 * 1024;
        uint8_t* d = (uint8_t*)malloc(n);
        /* Simulate text-like data: repeated with minor variation */
        const char* words[] = {"the ", "quick ", "brown ", "fox ", "jumps ",
                                "over ", "lazy ", "dog ", "and ", "cat "};
        size_t pos = 0;
        int wi = 0;
        while (pos < n) {
            const char* w = words[wi % 10]; wi++;
            size_t wl = strlen(w);
            if (pos + wl > n) wl = n - pos;
            memcpy(d + pos, w, wl);
            pos += wl;
        }
        fail += test_roundtrip("512K text-like", d, n);
        free(d);
    }

    printf("\n%s: %d failure(s)\n",
           fail == 0 ? "ALL PASSED" : "FAILED", fail);
    return fail;
}
