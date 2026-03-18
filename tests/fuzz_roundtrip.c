/**
 * @file fuzz_roundtrip.c
 * @brief Roundtrip fuzz test — compress random data and verify decompression.
 * 
 * Usage: fuzz_roundtrip [iterations] [max_size]
 * Default: 10000 iterations, up to 64KB per test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxcomp/maxcomp.h>

static uint32_t rng_state = 42;
static uint32_t xorshift32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Generate data with various patterns */
static void gen_data(uint8_t* buf, size_t size, int pattern) {
    switch (pattern % 6) {
        case 0: /* Random */
            for (size_t i = 0; i < size; i++)
                buf[i] = xorshift32() & 0xFF;
            break;
        case 1: /* Repeated bytes */
            memset(buf, xorshift32() & 0xFF, size);
            break;
        case 2: /* Sequential */
            for (size_t i = 0; i < size; i++)
                buf[i] = (uint8_t)(i & 0xFF);
            break;
        case 3: /* Sparse (mostly zeros) */
            memset(buf, 0, size);
            for (size_t i = 0; i < size / 16; i++)
                buf[xorshift32() % size] = xorshift32() & 0xFF;
            break;
        case 4: /* Text-like (printable ASCII) */
            for (size_t i = 0; i < size; i++)
                buf[i] = 32 + (xorshift32() % 95);
            break;
        case 5: /* Repeating pattern */
            {
                int plen = 1 + (xorshift32() % 32);
                for (size_t i = 0; i < size; i++)
                    buf[i] = (i < (size_t)plen) ? (xorshift32() & 0xFF) : buf[i % plen];
            }
            break;
    }
}

int main(int argc, char** argv) {
    int iterations = (argc > 1) ? atoi(argv[1]) : 10000;
    int max_size = (argc > 2) ? atoi(argv[2]) : 65536;
    int levels[] = {1, 3, 6, 9, 12, 20, 24, 26};
    int n_levels = sizeof(levels) / sizeof(levels[0]);
    
    printf("Fuzz roundtrip: %d iterations, up to %d bytes, %d levels\n",
           iterations, max_size, n_levels);
    
    uint8_t* src = (uint8_t*)malloc(max_size);
    size_t comp_cap = max_size * 2 + 4096;
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec = (uint8_t*)malloc(max_size + 64);
    
    int failures = 0;
    
    for (int i = 0; i < iterations; i++) {
        int size = 1 + (xorshift32() % max_size);
        int pattern = xorshift32() % 6;
        int level = levels[xorshift32() % n_levels];
        
        gen_data(src, size, pattern);
        
        size_t csz = mcx_compress(comp, comp_cap, src, size, level);
        if (mcx_is_error(csz)) {
            printf("FAIL: compress error at iter %d (size=%d, L%d, pat=%d): %s\n",
                   i, size, level, pattern, mcx_get_error_name(csz));
            failures++;
            continue;
        }
        
        size_t dsz = mcx_decompress(dec, max_size + 64, comp, csz);
        if (mcx_is_error(dsz)) {
            printf("FAIL: decompress error at iter %d (size=%d, L%d, pat=%d): %s\n",
                   i, size, level, pattern, mcx_get_error_name(dsz));
            failures++;
            continue;
        }
        
        if (dsz != (size_t)size || memcmp(src, dec, size) != 0) {
            printf("FAIL: mismatch at iter %d (size=%d→%zu, L%d, pat=%d)\n",
                   i, size, dsz, level, pattern);
            failures++;
            continue;
        }
        
        if ((i + 1) % 1000 == 0)
            printf("  %d/%d OK\n", i + 1, iterations);
    }
    
    free(src); free(comp); free(dec);
    
    if (failures > 0) {
        printf("\n%d FAILURES out of %d tests\n", failures, iterations);
        return 1;
    }
    printf("\nAll %d tests PASSED\n", iterations);
    return 0;
}
