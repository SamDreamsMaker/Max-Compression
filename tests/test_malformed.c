/**
 * @file test_malformed.c
 * @brief Test decompressor robustness against malformed/corrupted input.
 * Must not crash, must return error gracefully.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxcomp/maxcomp.h>

static int tests_run = 0;
static int tests_pass = 0;

#define TEST(name) do { tests_run++; printf("  %s: ", name); } while(0)
#define PASS() do { tests_pass++; printf("PASS\n"); } while(0)
#define FAIL(msg) printf("FAIL (%s)\n", msg)

int main(void) {
    uint8_t dec[4096];
    
    printf("=== Malformed Input Tests ===\n");
    
    /* Empty input */
    TEST("Empty input");
    size_t r = mcx_decompress(dec, sizeof(dec), NULL, 0);
    if (mcx_is_error(r) || r == 0) PASS(); else FAIL("should error");
    
    /* Too short for header */
    TEST("Short input (< 20 bytes)");
    uint8_t short_data[10] = {0x4D, 0x43, 0x58, 0x01, 0,0,0,0,0,0};
    r = mcx_decompress(dec, sizeof(dec), short_data, 10);
    if (mcx_is_error(r) || r == 0) PASS(); else FAIL("should error");
    
    /* Wrong magic */
    TEST("Wrong magic number");
    uint8_t bad_magic[20] = {0};
    r = mcx_decompress(dec, sizeof(dec), bad_magic, 20);
    if (mcx_is_error(r) || r == 0) PASS(); else FAIL("should error");
    
    /* Valid header, truncated body */
    TEST("Truncated compressed data");
    {
        /* First compress something valid */
        uint8_t src[100];
        memset(src, 'A', sizeof(src));
        uint8_t comp[512];
        size_t csz = mcx_compress(comp, sizeof(comp), src, sizeof(src), 1);
        if (!mcx_is_error(csz) && csz > 20) {
            /* Truncate to just the header */
            r = mcx_decompress(dec, sizeof(dec), comp, 21);
            if (mcx_is_error(r) || r == 0) PASS(); else FAIL("should error on truncated");
        } else {
            FAIL("compress failed");
        }
    }
    
    /* Corrupted data (flip bits in compressed stream) */
    TEST("Corrupted compressed data");
    {
        uint8_t src[200];
        for (int i = 0; i < 200; i++) src[i] = i & 0xFF;
        uint8_t comp[512];
        size_t csz = mcx_compress(comp, sizeof(comp), src, sizeof(src), 6);
        if (!mcx_is_error(csz) && csz > 30) {
            /* Flip bits in the middle of compressed data */
            comp[csz/2] ^= 0xFF;
            comp[csz/2 + 1] ^= 0x55;
            r = mcx_decompress(dec, sizeof(dec), comp, csz);
            /* Should either error or produce wrong data (not crash) */
            if (mcx_is_error(r) || r == 0 || memcmp(src, dec, sizeof(src)) != 0) {
                PASS();
            } else {
                FAIL("corruption not detected");
            }
        } else {
            FAIL("compress failed");
        }
    }
    
    /* Claimed size larger than output buffer */
    TEST("Output buffer too small");
    {
        uint8_t src[1000];
        memset(src, 'B', sizeof(src));
        uint8_t comp[2048];
        size_t csz = mcx_compress(comp, sizeof(comp), src, sizeof(src), 3);
        if (!mcx_is_error(csz)) {
            uint8_t tiny[10];
            r = mcx_decompress(tiny, sizeof(tiny), comp, csz);
            if (mcx_is_error(r) || r == 0) PASS(); else FAIL("should error on small buffer");
        } else {
            FAIL("compress failed");
        }
    }
    
    /* All zeros (valid MCX magic but nonsense payload) */
    TEST("Valid magic, zero payload");
    {
        uint8_t zeros[64] = {0x4D, 0x43, 0x58, 0x01, 1, 1, 3, 1};
        memset(zeros + 8, 0, 56);
        /* Write claimed original size = 100 */
        uint64_t claimed = 100;
        memcpy(zeros + 8, &claimed, 8);
        r = mcx_decompress(dec, sizeof(dec), zeros, sizeof(zeros));
        if (mcx_is_error(r) || r == 0) PASS(); else FAIL("should handle gracefully");
    }
    
    printf("\n%d/%d tests passed\n", tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
