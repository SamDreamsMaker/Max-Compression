#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/maxcomp/maxcomp.h"

int main(void) {
    /* kennedy.xls-like: mostly zeros with periodic non-zero structure.
     * Sweep sizes to find exactly where the BWT path breaks. */
    size_t sizes[] = {65536, 131072, 262144, 524288, 786432, 1029744, 0};
    for (int s = 0; sizes[s]; s++) {
        size_t n = sizes[s];
        uint8_t* d = (uint8_t*)calloc(n, 1);
        for (size_t i = 0; i < n; i++) {
            if (i % 512 < 8)  d[i] = (uint8_t)(i & 0xFF);
            else if (i % 64 == 0) d[i] = 0x09;
        }

        size_t cap = mcx_compress_bound(n);
        uint8_t* comp = (uint8_t*)malloc(cap);
        uint8_t* dec  = (uint8_t*)malloc(n + 64);

        size_t cs = mcx_compress(comp, cap, d, n, 12);
        size_t ds = mcx_decompress(dec, n + 64, comp, cs);

        int ok = (!mcx_is_error(cs) && !mcx_is_error(ds) &&
                  ds == n && memcmp(d, dec, n) == 0);
        printf("  n=%7zu  cs=%7zu  ds=0x%zx  %s\n",
               n, mcx_is_error(cs) ? (size_t)0 : cs, ds,
               ok ? "OK" : "FAIL");

        free(d); free(comp); free(dec);
    }
    return 0;
}
