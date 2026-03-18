#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/maxcomp/maxcomp.h"

static int test_file(const char* path, int level) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("  SKIP: %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    rewind(f);
    uint8_t* src = (uint8_t*)malloc(n);
    fread(src, 1, n, f);
    fclose(f);

    size_t cap  = mcx_compress_bound(n);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec  = (uint8_t*)malloc(n + 64);

    size_t cs = mcx_compress(comp, cap, src, n, level);
    if (mcx_is_error(cs)) {
        printf("  [ERR-COMP] %s  code=%zu\n", path, cs & ~((size_t)1<<63));
        free(src); free(comp); free(dec); return 1;
    }

    size_t ds = mcx_decompress(dec, n + 64, comp, cs);
    if (mcx_is_error(ds)) {
        printf("  [ERR-DECOMP] %s  n=%zu cs=%zu  errcode=%zu\n",
               path, n, cs, ds & ~((size_t)1<<63));
        free(src); free(comp); free(dec); return 1;
    }
    if (ds != n || memcmp(src, dec, n) != 0) {
        size_t pos = 0;
        while (pos < n && src[pos] == dec[pos]) pos++;
        printf("  [MISMATCH] %s  ds=%zu  first_diff=%zu\n", path, ds, pos);
        free(src); free(comp); free(dec); return 1;
    }
    printf("  [OK] %s  %zu -> %zu (%.2fx)\n", path, n, cs, (double)n/(double)cs);
    free(src); free(comp); free(dec); return 0;
}

int main(void) {
    const char* base = "benchmarks/corpus/canterbury/";
    const char* files[] = {
        "alice29.txt","asyoulik.txt","cp.html","fields.c",
        "grammar.lsp","kennedy.xls","lcet10.txt","plrabn12.txt",
        "ptt5","sum","xargs.1", NULL
    };
    int fail = 0;
    char path[512];
    printf("=== Real Canterbury L12 round-trip ===\n");
    for (int i = 0; files[i]; i++) {
        snprintf(path, sizeof(path), "%s%s", base, files[i]);
        fail += test_file(path, 12);
    }
    printf("\n%s: %d failure(s)\n", fail==0?"ALL PASSED":"FAILED", fail);
    return fail;
}
