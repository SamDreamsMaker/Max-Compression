/**
 * @file test_cmrans.c
 * @brief Targeted CM-rANS test at level 20.
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
    if (!compressed || !decompressed) { free(compressed); free(decompressed); return 1; }

    comp_size = mcx_compress(compressed, dst_cap, data, size, level);
    if (mcx_is_error(comp_size)) {
        printf("  %-22s  COMPRESS ERROR: %s\n", label, mcx_get_error_name(comp_size));
        free(compressed); free(decompressed); return 1;
    }

    dec_size = mcx_decompress(decompressed, size, compressed, comp_size);
    if (mcx_is_error(dec_size)) {
        printf("  %-22s  DECOMPRESS ERROR: %s\n", label, mcx_get_error_name(dec_size));
        free(compressed); free(decompressed); return 1;
    }

    ok = (dec_size == size && memcmp(data, decompressed, size) == 0);
    printf("  %-22s  %zu -> %zu (%.1fx)  %s\n",
           label, size, comp_size, (double)size/(double)comp_size,
           ok ? "OK" : "MISMATCH!");

    if (!ok) {
        size_t i; int diffs = 0;
        for (i = 0; i < size && diffs < 3; i++) {
            if (data[i] != decompressed[i]) {
                printf("    diff[%zu]: expected %d got %d\n", i, data[i], decompressed[i]);
                diffs++;
            }
        }
    }

    free(compressed); free(decompressed);
    return ok ? 0 : 1;
}

int main(void)
{
    int failures = 0;
    size_t size = 100 * 1024;
    size_t i;
    uint8_t* data;
    unsigned int seed;

    printf("=== Level 20 (CM-rANS) ===\n");

    data = (uint8_t*)calloc(size, 1);
    failures += test_data("zeros", data, size, 20);
    free(data);

    data = (uint8_t*)malloc(size);
    memset(data, 'A', size);
    failures += test_data("repeated-A", data, size, 20);
    free(data);

    data = (uint8_t*)malloc(size);
    for (i = 0; i < size; i++) data[i] = (uint8_t)(i % 13);
    failures += test_data("pattern-mod13", data, size, 20);
    free(data);

    data = (uint8_t*)malloc(size);
    for (i = 0; i < size; i++) data[i] = (uint8_t)('a' + (i % 26));
    failures += test_data("text-az", data, size, 20);
    free(data);

    data = (uint8_t*)malloc(size);
    seed = 12345;
    for (i = 0; i < size; i++) { seed = seed*1103515245+12345; data[i] = (uint8_t)((seed>>16)&0xFF); }
    failures += test_data("random", data, size, 20);
    free(data);

    /* Small sizes */
    printf("\n=== Small sizes (level 20) ===\n");
    for (size = 1; size <= 1024; size *= 2) {
        char label[32];
        data = (uint8_t*)malloc(size);
        for (i = 0; i < size; i++) data[i] = (uint8_t)('a' + (i % 26));
        sprintf(label, "text-%zu", size);
        failures += test_data(label, data, size, 20);
        free(data);
    }

    printf("\n%d failures\n", failures);
    return failures;
}
