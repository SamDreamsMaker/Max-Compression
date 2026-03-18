/**
 * @file test_edge_cases.c
 * @brief Edge case tests for compression/decompression.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxcomp/maxcomp.h>

static int tests_run = 0, tests_pass = 0;
#define TEST(name) do { tests_run++; printf("  %-40s ", name); } while(0)
#define PASS() do { tests_pass++; printf("OK\n"); } while(0)
#define FAIL(msg) printf("FAIL (%s)\n", msg)

/* Roundtrip helper */
static int roundtrip(const uint8_t* src, size_t size, int level) {
    size_t cap = mcx_compress_bound(size);
    uint8_t* comp = malloc(cap);
    uint8_t* dec = malloc(size + 64);
    
    size_t csz = mcx_compress(comp, cap, src, size, level);
    if (mcx_is_error(csz)) { free(comp); free(dec); return 0; }
    
    size_t dsz = mcx_decompress(dec, size + 64, comp, csz);
    int ok = (!mcx_is_error(dsz) && dsz == size && memcmp(src, dec, size) == 0);
    
    free(comp); free(dec);
    return ok;
}

int main(void) {
    printf("=== Edge Case Tests ===\n");
    
    /* Single byte */
    TEST("Single byte");
    uint8_t one = 42;
    if (roundtrip(&one, 1, 1)) PASS(); else FAIL("roundtrip");
    
    /* Two bytes */
    TEST("Two bytes");
    uint8_t two[2] = {0xAA, 0x55};
    if (roundtrip(two, 2, 1)) PASS(); else FAIL("roundtrip");
    
    /* All zeros */
    TEST("All zeros (4KB)");
    uint8_t* zeros = calloc(4096, 1);
    if (roundtrip(zeros, 4096, 12)) PASS(); else FAIL("roundtrip");
    free(zeros);
    
    /* All 0xFF */
    TEST("All 0xFF (4KB)");
    uint8_t* ffs = malloc(4096);
    memset(ffs, 0xFF, 4096);
    if (roundtrip(ffs, 4096, 12)) PASS(); else FAIL("roundtrip");
    free(ffs);
    
    /* Single repeated byte (highly compressible) */
    TEST("Single byte repeated 64KB");
    uint8_t* rep = malloc(65536);
    memset(rep, 'A', 65536);
    if (roundtrip(rep, 65536, 20)) PASS(); else FAIL("roundtrip");
    free(rep);
    
    /* Ascending sequence */
    TEST("Ascending 0-255 repeated");
    uint8_t* asc = malloc(4096);
    for (int i = 0; i < 4096; i++) asc[i] = i & 0xFF;
    if (roundtrip(asc, 4096, 6)) PASS(); else FAIL("roundtrip");
    free(asc);
    
    /* Random data (incompressible) */
    TEST("Pseudo-random 8KB (incompressible)");
    uint8_t* rnd = malloc(8192);
    uint32_t r = 1;
    for (int i = 0; i < 8192; i++) { r = r * 1103515245 + 12345; rnd[i] = (r >> 16) & 0xFF; }
    if (roundtrip(rnd, 8192, 9)) PASS(); else FAIL("roundtrip");
    free(rnd);
    
    /* Exact block boundary (64KB) */
    TEST("Exact 64KB boundary");
    uint8_t* blk = malloc(65536);
    for (int i = 0; i < 65536; i++) blk[i] = (i * 7 + 13) & 0xFF;
    if (roundtrip(blk, 65536, 3)) PASS(); else FAIL("roundtrip");
    free(blk);
    
    /* Very small with all levels */
    TEST("16 bytes at all levels");
    uint8_t small[16] = "Hello, World!!!\n";
    int all_ok = 1;
    for (int l = 1; l <= 26; l++) {
        if (l > 3 && l < 6) continue;
        if (l > 9 && l < 12) continue;
        if (l > 14 && l < 20) continue;
        if (l > 20 && l < 24) continue;
        if (l > 24 && l < 26) continue;
        if (!roundtrip(small, 16, l)) { all_ok = 0; break; }
    }
    if (all_ok) PASS(); else FAIL("some level failed");
    
    /* Large repetitive (1MB) */
    TEST("1MB repetitive pattern");
    uint8_t* big = malloc(1024 * 1024);
    for (int i = 0; i < 1024 * 1024; i++) big[i] = "The quick brown fox jumps over the lazy dog. "[i % 46];
    if (roundtrip(big, 1024 * 1024, 1)) PASS(); else FAIL("roundtrip");
    free(big);
    
    /* Binary-like data */
    TEST("Binary with null bytes");
    uint8_t bin[256];
    for (int i = 0; i < 256; i++) bin[i] = (uint8_t)i;
    if (roundtrip(bin, 256, 6)) PASS(); else FAIL("roundtrip");
    
    /* Frame info on compressed data */
    TEST("Frame info verification");
    {
        uint8_t data[1000];
        memset(data, 'X', sizeof(data));
        uint8_t comp[2048];
        size_t csz = mcx_compress(comp, sizeof(comp), data, sizeof(data), 6);
        if (!mcx_is_error(csz)) {
            mcx_frame_info info;
            size_t r = mcx_get_frame_info(&info, comp, csz);
            if (!mcx_is_error(r) && info.original_size == 1000 && info.level == 6) {
                PASS();
            } else {
                FAIL("wrong frame info");
            }
        } else {
            FAIL("compress failed");
        }
    }
    
    printf("\n%d/%d tests passed\n", tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
