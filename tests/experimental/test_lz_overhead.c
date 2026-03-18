/* Measure LZ-HC compressed size breakdown */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char* files[] = {"/tmp/cantrbry/grammar.lsp", "/tmp/cantrbry/xargs.1", "/tmp/cantrbry/fields.c", NULL};
    for (int fi = 0; files[fi]; fi++) {
        FILE* f = fopen(files[fi], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        size_t n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* data = malloc(n);
        fread(data, 1, n, f);
        fclose(f);
        
        size_t cap = n * 2;
        uint8_t* c3 = malloc(cap);
        uint8_t* c9 = malloc(cap);
        uint8_t* c20 = malloc(cap);
        uint8_t* dec = malloc(n);
        
        size_t s3 = mcx_compress(c3, cap, data, n, 3);
        size_t s9 = mcx_compress(c9, cap, data, n, 9);
        size_t s20 = mcx_compress(c20, cap, data, n, 20);
        
        const char* name = strrchr(files[fi], '/') + 1;
        printf("%-12s %5zu | L3=%5zu (%.2fx) L9=%5zu (%.2fx) L20=%5zu (%.2fx)\n",
               name, n,
               mcx_is_error(s3) ? 0 : s3, mcx_is_error(s3) ? 0 : (double)n/s3,
               mcx_is_error(s9) ? 0 : s9, mcx_is_error(s9) ? 0 : (double)n/s9,
               mcx_is_error(s20) ? 0 : s20, mcx_is_error(s20) ? 0 : (double)n/s20);
        
        /* Verify roundtrip */
        if (!mcx_is_error(s20)) {
            size_t ds = mcx_decompress(dec, n, c20, s20);
            printf("  Roundtrip: %s\n", (!mcx_is_error(ds) && ds == n && memcmp(data, dec, n) == 0) ? "OK" : "FAIL");
        }
        
        free(data); free(c3); free(c9); free(c20); free(dec);
    }
    return 0;
}
