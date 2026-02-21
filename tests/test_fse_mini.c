#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../lib/entropy/mcx_fse.h"

int main() {
    /* Test with larger biased data (80% 'a', 20% 'b') */
    size_t n = 1000;
    unsigned char* data = (unsigned char*)malloc(n);
    srand(42);
    for (size_t i = 0; i < n; i++)
        data[i] = (rand() % 10 < 8) ? 'a' : 'b';

    size_t comp_cap = n + 1024;
    unsigned char* comp = (unsigned char*)malloc(comp_cap);
    unsigned char* dec  = (unsigned char*)malloc(n + 16);

    printf("Compressing %zu bytes (biased 80/20)...\n", n);
    fflush(stdout);

    size_t cs = mcx_fse_compress(comp, comp_cap, data, n);
    printf("Compressed: %zu bytes (ratio: %.2fx)\n", cs, cs > 0 ? (double)n / cs : 0);
    fflush(stdout);

    if (cs > 0) {
        size_t ds = mcx_fse_decompress(dec, n, comp, cs);
        printf("Decompressed: %zu bytes\n", ds);
        if (ds == n && memcmp(data, dec, n) == 0)
            printf("MATCH!\n");
        else {
            printf("MISMATCH at size %zu vs %zu\n", ds, n);
            if (ds >= 1)
                printf("  First byte: expected 0x%02X got 0x%02X\n", data[0], dec[0]);
        }
    }

    free(data); free(comp); free(dec);
    return 0;
}
