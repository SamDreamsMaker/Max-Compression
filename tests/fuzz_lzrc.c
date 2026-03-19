/**
 * @file fuzz_lzrc.c
 * @brief Fuzz test for LZRC compression (L24 and L26).
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static uint32_t xorshift(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (*state = x);
}

int main(int argc, char** argv) {
    int iters = 500;
    int max_size = 65536;
    if (argc > 1) iters = atoi(argv[1]);
    if (argc > 2) max_size = atoi(argv[2]);
    
    printf("LZRC fuzz test: %d iterations, max size %d\n", iters, max_size);
    
    uint32_t seed = 12345;
    int passed = 0;
    
    for (int i = 0; i < iters; i++) {
        /* Random size 1 to max_size */
        size_t size = (xorshift(&seed) % max_size) + 1;
        uint8_t* data = malloc(size);
        
        /* Fill with random data (mix of patterns) */
        int pattern = xorshift(&seed) % 4;
        for (size_t j = 0; j < size; j++) {
            switch (pattern) {
                case 0: data[j] = (uint8_t)(xorshift(&seed) & 0xFF); break;  /* random */
                case 1: data[j] = (uint8_t)(j & 0xFF); break;                 /* sequential */
                case 2: data[j] = (uint8_t)(xorshift(&seed) % 16); break;     /* low entropy */
                case 3: data[j] = (j < size/2) ? 0 : (uint8_t)(xorshift(&seed) & 0xFF); break; /* mixed */
            }
        }
        
        size_t bound = mcx_compress_bound(size);
        uint8_t* comp = malloc(bound);
        uint8_t* dec = malloc(size + 1);
        
        /* Test L24 (HC) */
        size_t cs24 = mcx_compress(comp, bound, data, size, 24);
        if (!mcx_is_error(cs24)) {
            size_t ds = mcx_decompress(dec, size + 1, comp, cs24);
            assert(!mcx_is_error(ds));
            assert(ds == size);
            assert(memcmp(data, dec, size) == 0);
        }
        
        /* Test L26 (BT) */
        size_t cs26 = mcx_compress(comp, bound, data, size, 26);
        if (!mcx_is_error(cs26)) {
            size_t ds = mcx_decompress(dec, size + 1, comp, cs26);
            assert(!mcx_is_error(ds));
            assert(ds == size);
            assert(memcmp(data, dec, size) == 0);
        }
        
        free(data);
        free(comp);
        free(dec);
        passed++;
        
        if ((i + 1) % 100 == 0) printf("  %d/%d passed\n", i + 1, iters);
    }
    
    printf("\nAll %d iterations passed (L24 + L26)\n", passed);
    return 0;
}
