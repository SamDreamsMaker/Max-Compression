/**
 * @file test_zeros.c
 * @brief Targeted test for all data types used in benchmarks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>

static int test_data(const char* label, const uint8_t* data, size_t size, int level)
{
    size_t dst_cap, comp_size, dec_size;
    uint8_t* compressed;
    uint8_t* decompressed;
    int ok;

    dst_cap = mcx_compress_bound(size);
    compressed = (uint8_t*)malloc(dst_cap);
    decompressed = (uint8_t*)malloc(size);

    if (!compressed || !decompressed) {
        free(compressed); free(decompressed);
        printf("  %-20s  OOM\n", label);
        return 1;
    }

    comp_size = mcx_compress(compressed, dst_cap, data, size, level);
    if (mcx_is_error(comp_size)) {
        printf("  %-20s  COMPRESS ERROR: %s\n", label, mcx_get_error_name(comp_size));
        free(compressed); free(decompressed);
        return 1;
    }

    dec_size = mcx_decompress(decompressed, size, compressed, comp_size);
    if (mcx_is_error(dec_size)) {
        printf("  %-20s  DECOMPRESS ERROR: %s\n", label, mcx_get_error_name(dec_size));
        free(compressed); free(decompressed);
        return 1;
    }

    ok = (dec_size == size && memcmp(data, decompressed, size) == 0);
    printf("  %-20s  %zu -> %zu (%.1fx)  %s\n",
           label, size, comp_size, (double)size/(double)comp_size,
           ok ? "OK" : "MISMATCH!");

    if (!ok) {
        size_t i;
        int diffs = 0;
        for (i = 0; i < size && diffs < 5; i++) {
            if (data[i] != decompressed[i]) {
                printf("    diff[%zu]: expected %d got %d\n", i, data[i], decompressed[i]);
                diffs++;
            }
        }
    }

    free(compressed);
    free(decompressed);
    return ok ? 0 : 1;
}

int main(void)
{
    int failures = 0;
    size_t size = 100 * 1024;
    size_t i;
    uint8_t* data;
    unsigned int seed;

    printf("=== Level 3 (FAST/Huffman) ===\n");

    /* Zeros */
    data = (uint8_t*)calloc(size, 1);
    failures += test_data("zeros-L3", data, size, 3);
    free(data);

    /* Pattern */
    data = (uint8_t*)malloc(size);
    for (i = 0; i < size; i++) data[i] = (uint8_t)(i % 13);
    failures += test_data("pattern-L3", data, size, 3);
    free(data);

    /* Text */
    data = (uint8_t*)malloc(size);
    for (i = 0; i < size; i++) data[i] = (uint8_t)('a' + (i % 26));
    failures += test_data("text-L3", data, size, 3);
    free(data);

    /* Random */
    data = (uint8_t*)malloc(size);
    seed = 12345;
    for (i = 0; i < size; i++) { seed = seed * 1103515245 + 12345; data[i] = (uint8_t)((seed >> 16) & 0xFF); }
    failures += test_data("random-L3", data, size, 3);
    free(data);

    printf("\n=== Level 10 (DEFAULT/BWT+rANS) ===\n");

    /* Zeros */
    data = (uint8_t*)calloc(size, 1);
    failures += test_data("zeros-L10", data, size, 10);
    free(data);

    /* Repeated 'A' */
    data = (uint8_t*)malloc(size);
    memset(data, 'A', size);
    failures += test_data("repeated-A-L10", data, size, 10);
    free(data);

    /* Pattern */
    data = (uint8_t*)malloc(size);
    for (i = 0; i < size; i++) data[i] = (uint8_t)(i % 13);
    failures += test_data("pattern-L10", data, size, 10);
    free(data);

    /* Text */
    data = (uint8_t*)malloc(size);
    for (i = 0; i < size; i++) data[i] = (uint8_t)('a' + (i % 26));
    failures += test_data("text-L10", data, size, 10);
    free(data);

    printf("\n%d failures\n", failures);
    return failures;
}
