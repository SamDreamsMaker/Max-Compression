/**
 * @file test_roundtrip.c
 * @brief Round-trip test: compress → decompress must equal original.
 *
 * This is the most critical test: data integrity.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        return 1; \
    } \
} while(0)

/* ─── Test: Simple text ──────────────────────────────────────────────── */

static int test_text_roundtrip(void)
{
    const char* text = "Hello, MaxCompression! This is a test of the "
                       "compression library. Repeated text is important "
                       "for testing compression ratios. Hello, MaxCompression! "
                       "This is a test of the compression library again. "
                       "Repeated text is important for testing compression ratios.";

    size_t src_size = strlen(text);
    size_t dst_cap  = mcx_compress_bound(src_size);

    uint8_t* compressed   = (uint8_t*)malloc(dst_cap);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);

    TEST_ASSERT(compressed != NULL, "malloc compressed");
    TEST_ASSERT(decompressed != NULL, "malloc decompressed");

    /* Compress */
    size_t comp_size = mcx_compress(compressed, dst_cap, text, src_size, MCX_LEVEL_DEFAULT);
    TEST_ASSERT(!mcx_is_error(comp_size), "compression failed");
    TEST_ASSERT(comp_size > 0, "compressed size > 0");

    /* Get decompressed size from header */
    unsigned long long dec_size = mcx_get_decompressed_size(compressed, comp_size);
    TEST_ASSERT(dec_size == src_size, "decompressed size matches");

    /* Decompress */
    size_t result = mcx_decompress(decompressed, src_size, compressed, comp_size);
    TEST_ASSERT(!mcx_is_error(result), "decompression failed");
    TEST_ASSERT(result == src_size, "decompressed size correct");

    /* Compare */
    TEST_ASSERT(memcmp(text, decompressed, src_size) == 0, "data mismatch!");

    printf("  PASS: text roundtrip (ratio: %.2fx)\n",
           (double)src_size / (double)comp_size);

    free(compressed);
    free(decompressed);
    return 0;
}

/* ─── Test: Binary data ──────────────────────────────────────────────── */

static int test_binary_roundtrip(void)
{
    /* Create test data with some patterns */
    size_t src_size = 8192;
    uint8_t* src = (uint8_t*)malloc(src_size);
    TEST_ASSERT(src != NULL, "malloc src");

    for (size_t i = 0; i < src_size; i++) {
        src[i] = (uint8_t)((i * 7 + 13) % 256);
    }

    size_t dst_cap = mcx_compress_bound(src_size);
    uint8_t* compressed   = (uint8_t*)malloc(dst_cap);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);

    TEST_ASSERT(compressed != NULL, "malloc compressed");
    TEST_ASSERT(decompressed != NULL, "malloc decompressed");

    /* Compress at high level */
    size_t comp_size = mcx_compress(compressed, dst_cap, src, src_size, 10);
    TEST_ASSERT(!mcx_is_error(comp_size), "compression failed");

    /* Decompress */
    size_t result = mcx_decompress(decompressed, src_size, compressed, comp_size);
    TEST_ASSERT(!mcx_is_error(result), "decompression failed");
    TEST_ASSERT(result == src_size, "decompressed size correct");

    /* Compare */
    TEST_ASSERT(memcmp(src, decompressed, src_size) == 0, "data mismatch!");

    printf("  PASS: binary roundtrip (ratio: %.2fx)\n",
           (double)src_size / (double)comp_size);

    free(src);
    free(compressed);
    free(decompressed);
    return 0;
}

/* ─── Test: Single byte ──────────────────────────────────────────────── */

static int test_single_byte(void)
{
    uint8_t src[1] = {42};
    size_t dst_cap = mcx_compress_bound(1);
    uint8_t* compressed   = (uint8_t*)malloc(dst_cap);
    uint8_t  decompressed[1] = {0};

    size_t comp_size = mcx_compress(compressed, dst_cap, src, 1, MCX_LEVEL_DEFAULT);
    TEST_ASSERT(!mcx_is_error(comp_size), "single byte compress failed");

    size_t result = mcx_decompress(decompressed, 1, compressed, comp_size);
    TEST_ASSERT(!mcx_is_error(result), "single byte decompress failed");
    TEST_ASSERT(decompressed[0] == 42, "single byte mismatch");

    printf("  PASS: single byte roundtrip\n");

    free(compressed);
    return 0;
}

/* ─── Test: Repeated bytes (high compression expected) ───────────────── */

static int test_repeated_bytes(void)
{
    size_t src_size = 10000;
    uint8_t* src = (uint8_t*)malloc(src_size);
    memset(src, 'A', src_size);

    size_t dst_cap = mcx_compress_bound(src_size);
    uint8_t* compressed   = (uint8_t*)malloc(dst_cap);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);

    size_t comp_size = mcx_compress(compressed, dst_cap, src, src_size, MCX_LEVEL_DEFAULT);
    TEST_ASSERT(!mcx_is_error(comp_size), "repeated compress failed");

    size_t result = mcx_decompress(decompressed, src_size, compressed, comp_size);
    TEST_ASSERT(!mcx_is_error(result), "repeated decompress failed");
    TEST_ASSERT(memcmp(src, decompressed, src_size) == 0, "repeated mismatch");

    double ratio = (double)src_size / (double)comp_size;
    printf("  PASS: repeated bytes (ratio: %.1fx — should be very high)\n", ratio);

    /* Scale up to exactly 131072 to reproduce the context.c bug */
    size_t big_size = 131072;
    uint8_t* big_src = (uint8_t*)malloc(big_size);
    uint8_t* big_comp = (uint8_t*)malloc(mcx_compress_bound(big_size));
    uint8_t* big_decomp = (uint8_t*)malloc(big_size);
    
    memset(big_src, 'Z', big_size);
    size_t big_csize = mcx_compress(big_comp, mcx_compress_bound(big_size), big_src, big_size, 10);
    TEST_ASSERT(!mcx_is_error(big_csize), "big repeated compress failed");
    size_t big_result = mcx_decompress(big_decomp, big_size, big_comp, big_csize);
    if (mcx_is_error(big_result)) {
        printf("FAIL: big repeated decompress failed: %s (line %d)\n", mcx_get_error_name(big_result), __LINE__);
        return 1;
    } else {
        TEST_ASSERT(big_result == big_size, "big repeated size correct");
        TEST_ASSERT(memcmp(big_src, big_decomp, big_size) == 0, "big repeated mismatch!");
        printf("  PASS: big repeated roundtrip (131072)\n");
    }

    /* Exactly trace the CM-rANS context table */
    for (size_t k = 0; k < big_size; k += 2) {
        big_src[k] = 0x10;
        big_src[k+1] = 0x00;
    }
    big_src[0] = 0x01; big_src[1] = 0x0F;
    for (size_t k = 2; k < 512; k += 2) {
        big_src[k] = 0x01; big_src[1] = 0x00;
    }
    big_csize = mcx_compress(big_comp, mcx_compress_bound(big_size), big_src, big_size, 10);
    TEST_ASSERT(!mcx_is_error(big_csize), "pattern table compress failed");
    big_result = mcx_decompress(big_decomp, big_size, big_comp, big_csize);
    TEST_ASSERT(!mcx_is_error(big_result), "pattern table decompress failed");
    TEST_ASSERT(big_result == big_size, "pattern table size correct");
    TEST_ASSERT(memcmp(big_src, big_decomp, big_size) == 0, "pattern table mismatch!");

    printf("  PASS: structured context table roundtrip (131072)\n");

    /* EXACT DUMP REPRODUCTION from context.c */
    FILE* gt = fopen("c:\\MaxCompression\\good_tables.bin", "rb");
    if (gt) {
        fread(big_src, 1, big_size, gt);
        fclose(gt);
        
        big_csize = mcx_compress(big_comp, mcx_compress_bound(big_size), big_src, big_size, 10);
        TEST_ASSERT(!mcx_is_error(big_csize), "good_tables compress failed");
        big_result = mcx_decompress(big_decomp, big_size, big_comp, big_csize);
        TEST_ASSERT(!mcx_is_error(big_result), "good_tables decompress failed");
        TEST_ASSERT(big_result == big_size, "good_tables size correct");
        
        if (memcmp(big_src, big_decomp, big_size) != 0) {
            printf("\nFAIL: good_tables.bin mismatch!\n");
            int diffs = 0;
            for (size_t i = 0; i < big_size; i++) {
                if (big_src[i] != big_decomp[i]) {
                    printf("  Byte %zu: src=%x, dec=%x\n", i, big_src[i], big_decomp[i]);
                    if (++diffs >= 5) break; 
                }
            }
            return 1;
        } else {
            printf("  PASS: real good_tables roundtrip (131072)\n");
        }
    }

    free(big_src); free(big_comp); free(big_decomp);

    free(src);
    free(compressed);
    free(decompressed);
    return 0;
}

/* ─── Test: API error handling ───────────────────────────────────────── */

static int test_errors(void)
{
    uint8_t buf[4096];
    size_t result;
    const char* name;

    /* NULL input */
    result = mcx_compress(NULL, 100, "test", 4, 3);
    TEST_ASSERT(mcx_is_error(result), "NULL dst should error");

    /* Invalid level */
    result = mcx_compress(buf, sizeof(buf), "test", 4, 100);
    TEST_ASSERT(mcx_is_error(result), "invalid level should error");

    /* Error name */
    name = mcx_get_error_name(result);
    TEST_ASSERT(name != NULL, "error name should not be NULL");

    printf("  PASS: error handling\n");
    return 0;
}

/* ─── Main ───────────────────────────────────────────────────────────── */

int main(void)
{
    printf("MaxCompression v%s — Round-trip Tests\n", mcx_version_string());
    printf("====================================\n");

    int failures = 0;
    failures += test_text_roundtrip();
    failures += test_binary_roundtrip();
    failures += test_single_byte();
    failures += test_repeated_bytes();
    failures += test_errors();

    printf("====================================\n");
    if (failures == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("%d test(s) FAILED!\n", failures);
    }

    return failures;
}
