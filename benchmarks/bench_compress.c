/**
 * @file bench_compress.c
 * @brief Benchmark — measures compression ratio and speed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <maxcomp/maxcomp.h>

static double get_time_sec(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static void benchmark_data(const char* label, const uint8_t* data, size_t size, int level)
{
    size_t dst_cap = mcx_compress_bound(size);
    uint8_t* compressed = (uint8_t*)malloc(dst_cap);
    uint8_t* decompressed = (uint8_t*)malloc(size);

    if (compressed == NULL || decompressed == NULL) {
        fprintf(stderr, "Out of memory\n");
        free(compressed); free(decompressed);
        return;
    }

    /* Compress benchmark */
    double t0 = get_time_sec();
    size_t comp_size = mcx_compress(compressed, dst_cap, data, size, level);
    double t1 = get_time_sec();
    double comp_time = t1 - t0;

    if (mcx_is_error(comp_size)) {
        printf("  %-20s  ERROR: %s\n", label, mcx_get_error_name(comp_size));
        free(compressed); free(decompressed);
        return;
    }

    /* Decompress benchmark */
    double t2 = get_time_sec();
    size_t dec_size = mcx_decompress(decompressed, size, compressed, comp_size);
    double t3 = get_time_sec();
    double dec_time = t3 - t2;

    if (mcx_is_error(dec_size)) {
        printf("  %-20s  DECOMPRESS ERROR\n", label);
        free(compressed); free(decompressed);
        return;
    }

    /* Verify */
    int ok = (dec_size == size && memcmp(data, decompressed, size) == 0);

    double ratio = (double)size / (double)comp_size;
    double comp_speed = (comp_time > 0) ? (double)size / comp_time / 1e6 : 0;
    double dec_speed  = (dec_time > 0)  ? (double)size / dec_time / 1e6  : 0;

    printf("  %-20s  %8zu -> %8zu  ratio: %5.2fx  comp: %6.1f MB/s  dec: %6.1f MB/s  %s\n",
           label, size, comp_size, ratio, comp_speed, dec_speed,
           ok ? "OK" : "MISMATCH!");

    free(compressed);
    free(decompressed);
}

int main(void)
{
    printf("MaxCompression v%s — Benchmark\n", mcx_version_string());
    printf("================================================\n\n");

    /* Generate test data */
    size_t size = 100 * 1024; /* 100 KB */

    /* All zeros */
    uint8_t* zeros = (uint8_t*)calloc(size, 1);

    /* Repeated pattern */
    uint8_t* pattern = (uint8_t*)malloc(size);
    for (size_t i = 0; i < size; i++) pattern[i] = (uint8_t)(i % 13);

    /* English-like text */
    const char* words[] = {"the ", "quick ", "brown ", "fox ", "jumps ",
                           "over ", "lazy ", "dog ", "and ", "a "};
    uint8_t* text = (uint8_t*)malloc(size);
    size_t ti = 0;
    while (ti < size) {
        const char* w = words[ti % 10];
        size_t wlen = strlen(w);
        size_t copy = (ti + wlen > size) ? size - ti : wlen;
        memcpy(text + ti, w, copy);
        ti += copy;
    }

    /* Pseudo-random */
    uint8_t* randdata = (uint8_t*)malloc(size);
    unsigned int seed = 12345;
    for (size_t i = 0; i < size; i++) {
        seed = seed * 1103515245 + 12345;
        randdata[i] = (uint8_t)((seed >> 16) & 0xFF);
    }

    printf("Level %d:\n", MCX_LEVEL_DEFAULT);
    benchmark_data("zeros", zeros, size, MCX_LEVEL_DEFAULT);
    benchmark_data("pattern", pattern, size, MCX_LEVEL_DEFAULT);
    benchmark_data("text", text, size, MCX_LEVEL_DEFAULT);
    benchmark_data("pseudo-random", randdata, size, MCX_LEVEL_DEFAULT);

    printf("\nLevel 1 (fast):\n");
    benchmark_data("text (fast)", text, size, 1);

    printf("\nLevel 10 (rANS):\n");
    benchmark_data("zeros",       zeros,   size, 10);
    benchmark_data("text",        text,    size, 10);
    benchmark_data("pattern",     pattern, size, 10);

    printf("\nLevel 20 (CM-rANS):\n");
    benchmark_data("zeros-CM",    zeros,   size, 20);
    benchmark_data("text-CM",     text,    size, 20);
    benchmark_data("pattern-CM",  pattern, size, 20);

    free(zeros);
    free(pattern);
    free(text);
    free(randdata);

    printf("\nDone.\n");
    return 0;
}
