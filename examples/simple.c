/**
 * @file simple.c
 * @brief Minimal MaxCompression (MCX) example — compress and decompress in memory.
 *
 * Build:
 *   gcc -O2 -o simple simple.c -I../include -L../build/lib -lmaxcomp -lpthread -lm
 *
 * Or with the installed library:
 *   gcc -O2 -o simple simple.c $(pkg-config --cflags --libs maxcomp)
 *
 * Usage:
 *   ./simple [level]
 */

#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    /* Choose compression level (default: 6) */
    int level = (argc > 1) ? atoi(argv[1]) : 6;
    if (level < MCX_LEVEL_MIN) level = MCX_LEVEL_MIN;
    if (level > MCX_LEVEL_MAX) level = MCX_LEVEL_MAX;

    /* Sample data to compress */
    const char* text =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "MaxCompression uses BWT, MTF, RLE, and rANS entropy coding "
        "to achieve very high compression ratios on text and binary data. "
        "This sentence is repeated to give the compressor something to work with. "
        "MaxCompression uses BWT, MTF, RLE, and rANS entropy coding "
        "to achieve very high compression ratios on text and binary data. ";

    size_t src_size = strlen(text);
    printf("Original:     %zu bytes\n", src_size);
    printf("Level:        %d\n", level);

    /* --- Compress --- */

    size_t dst_cap = mcx_compress_bound(src_size);
    void* compressed = malloc(dst_cap);
    if (!compressed) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    size_t comp_size = mcx_compress(compressed, dst_cap,
                                     text, src_size, level);
    if (mcx_is_error(comp_size)) {
        fprintf(stderr, "Compress error: %s\n", mcx_get_error_name(comp_size));
        free(compressed);
        return 1;
    }

    double ratio = (double)src_size / (double)comp_size;
    printf("Compressed:   %zu bytes (%.2fx)\n", comp_size, ratio);

    /* --- Decompress --- */

    /* Get the original size from the compressed frame header */
    unsigned long long orig_size = mcx_get_decompressed_size(compressed, comp_size);
    if (orig_size == 0) {
        fprintf(stderr, "Cannot determine decompressed size\n");
        free(compressed);
        return 1;
    }

    void* decompressed = malloc((size_t)orig_size);
    if (!decompressed) {
        fprintf(stderr, "malloc failed\n");
        free(compressed);
        return 1;
    }

    size_t dec_size = mcx_decompress(decompressed, (size_t)orig_size,
                                      compressed, comp_size);
    if (mcx_is_error(dec_size)) {
        fprintf(stderr, "Decompress error: %s\n", mcx_get_error_name(dec_size));
        free(compressed);
        free(decompressed);
        return 1;
    }

    printf("Decompressed: %zu bytes\n", dec_size);

    /* --- Verify roundtrip --- */

    if (dec_size == src_size && memcmp(text, decompressed, src_size) == 0) {
        printf("Roundtrip:    OK ✓\n");
    } else {
        printf("Roundtrip:    FAIL ✗\n");
        free(compressed);
        free(decompressed);
        return 1;
    }

    /* --- Version info --- */
    printf("MCX version:  %s\n", mcx_version_string());

    free(compressed);
    free(decompressed);
    return 0;
}
