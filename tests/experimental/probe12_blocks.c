/**
 * probe12_blocks.c — Pinpoint which block/stage fails for kennedy.xls at L12.
 * Compresses block-by-block manually and reports the failing block.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/maxcomp/maxcomp.h"

#define BLOCK_SIZE (131072)

int main(void)
{
    const char* path = "benchmarks/corpus/canterbury/kennedy.xls";
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    rewind(f);
    uint8_t* src = (uint8_t*)malloc(n);
    fread(src, 1, n, f);
    fclose(f);

    printf("kennedy.xls: %zu bytes\n\n", n);

    int global_fail = 0;
    size_t offset = 0;
    int block_idx = 0;
    while (offset < n) {
        size_t bsz = n - offset;
        if (bsz > BLOCK_SIZE) bsz = BLOCK_SIZE;

        size_t cap = mcx_compress_bound(bsz);
        uint8_t* comp = (uint8_t*)malloc(cap);
        uint8_t* dec  = (uint8_t*)malloc(bsz + 64);

        size_t cs = mcx_compress(comp, cap, src + offset, bsz, 12);
        if (mcx_is_error(cs)) {
            printf("  Block %d [%zu..%zu]: COMPRESS ERROR %zu\n",
                   block_idx, offset, offset+bsz, cs & ~((size_t)1<<63));
            global_fail = 1;
        } else {
            size_t ds = mcx_decompress(dec, bsz + 64, comp, cs);
            if (mcx_is_error(ds)) {
                printf("  Block %d [%zu..%zu]: DECOMP ERROR code=%zu  bsz=%zu cs=%zu\n",
                       block_idx, offset, offset+bsz, ds & ~((size_t)1<<63), bsz, cs);
                global_fail = 1;
            } else if (ds != bsz || memcmp(src+offset, dec, bsz) != 0) {
                printf("  Block %d [%zu..%zu]: MISMATCH  bsz=%zu cs=%zu ds=%zu\n",
                       block_idx, offset, offset+bsz, bsz, cs, ds);
                global_fail = 1;
            } else {
                printf("  Block %d [%zu..%zu]: OK  %zu -> %zu (%.1fx)\n",
                       block_idx, offset, offset+bsz, bsz, cs, (double)bsz/(double)cs);
            }
        }

        free(comp); free(dec);
        offset += bsz;
        block_idx++;
    }

    printf("\n%s\n", global_fail ? "FAILED" : "ALL PASSED");
    free(src);
    return global_fail;
}
