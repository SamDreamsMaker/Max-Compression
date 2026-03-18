/**
 * @file test_lzrc.c
 * @brief LZRC v2.0 roundtrip test — verifies compress/decompress via MCX API.
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_roundtrip(const char* name, const uint8_t* data, size_t size, int level) {
    size_t cap = size * 2 + 4096;
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(size + 1024);
    if (!comp || !dec) { printf("ALLOC FAIL\n"); return 1; }

    size_t csz = mcx_compress(comp, cap, data, size, level);
    if (mcx_is_error(csz)) {
        printf("FAIL %-20s L%d: compress error\n", name, level);
        free(comp); free(dec);
        return 1;
    }

    size_t dsz = mcx_decompress(dec, size + 1024, comp, csz);
    if (mcx_is_error(dsz) || dsz != size || memcmp(data, dec, size) != 0) {
        printf("FAIL %-20s L%d: decompress mismatch (dsz=%zu, expected=%zu)\n",
               name, level, dsz, size);
        free(comp); free(dec);
        return 1;
    }

    printf("OK   %-20s L%d: %zu → %zu (%.2fx)\n",
           name, level, size, csz, (double)size / csz);
    free(comp); free(dec);
    return 0;
}

int main(void) {
    int fails = 0;

    /* Test 1: Repetitive text */
    const char* text = "The quick brown fox jumps over the lazy dog. ";
    size_t tlen = strlen(text);
    uint8_t* rep_text = (uint8_t*)malloc(tlen * 100);
    for (int i = 0; i < 100; i++) memcpy(rep_text + i * tlen, text, tlen);
    fails += test_roundtrip("repetitive_text", rep_text, tlen * 100, 26);

    /* Test 2: Binary pattern */
    uint8_t bin[8192];
    for (int i = 0; i < 8192; i++) bin[i] = (uint8_t)((i * 7 + 13) ^ (i >> 3));
    fails += test_roundtrip("binary_pattern", bin, sizeof(bin), 26);

    /* Test 3: All zeros */
    uint8_t zeros[4096];
    memset(zeros, 0, sizeof(zeros));
    fails += test_roundtrip("all_zeros", zeros, sizeof(zeros), 26);

    /* Test 4: All same byte */
    uint8_t same[4096];
    memset(same, 0xAB, sizeof(same));
    fails += test_roundtrip("all_same", same, sizeof(same), 26);

    /* Test 5: Single byte */
    uint8_t one = 42;
    fails += test_roundtrip("single_byte", &one, 1, 26);

    /* Test 6: Random-ish (incompressible) */
    uint8_t rnd[4096];
    uint32_t s = 0xDEADBEEF;
    for (int i = 0; i < 4096; i++) { s = s * 1103515245 + 12345; rnd[i] = (uint8_t)(s >> 16); }
    fails += test_roundtrip("pseudo_random", rnd, sizeof(rnd), 26);

    /* Test 7: L20 multi-trial (should pick LZRC or BWT, verify roundtrip) */
    uint8_t mixed[16384];
    for (int i = 0; i < 16384; i++) mixed[i] = (uint8_t)(i % 256);
    fails += test_roundtrip("mixed_L20", mixed, sizeof(mixed), 20);

    /* Test 8: L24 LZRC-HC fast mode */
    fails += test_roundtrip("rep_text_L24", rep_text, tlen * 100, 24);
    free(rep_text);
    
    /* Test 9: L24 on binary */
    fails += test_roundtrip("binary_L24", bin, sizeof(bin), 24);

    printf("\n%s: %d failures\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails;
}
