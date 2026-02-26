#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>

static int test_level(const char* name, const uint8_t* data, size_t size, int level) {
    size_t cap = mcx_compress_bound(size);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec  = (uint8_t*)malloc(size + 16);
    if (!comp || !dec) { free(comp); free(dec); return 1; }
    
    size_t cs = mcx_compress(comp, cap, data, size, level);
    if (mcx_is_error(cs)) {
        printf("  FAIL L%-2d %-22s — compress error 0x%zx\n", level, name, cs);
        free(comp); free(dec); return 1;
    }
    size_t ds = mcx_decompress(dec, size, comp, cs);
    if (mcx_is_error(ds) || ds != size || memcmp(data, dec, size) != 0) {
        printf("  FAIL L%-2d %-22s — decomp mismatch (ds=%zu)\n", level, name, ds);
        free(comp); free(dec); return 1;
    }
    printf("  PASS L%-2d %-22s  %6zu → %6zu  (%.2fx)\n",
           level, name, size, cs, (double)size / (double)cs);
    free(comp); free(dec);
    return 0;
}

int main(void) {
    int fail = 0;
    printf("=== BWT Path Validation (Levels 10-22) ===\n\n");
    
    /* Build test data */
    size_t n = 128 * 1024; /* 128KB */
    uint8_t* text = (uint8_t*)malloc(n);
    const char* words[] = { "the ", "quick ", "brown ", "fox ", "jumps ",
                             "over ", "the ", "lazy ", "dog. ", "compression ",
                             "is ", "wonderful ", "and ", "efficient. " };
    size_t pos = 0, wi = 0;
    while (pos < n) {
        const char* w = words[wi++ % 14];
        size_t wl = strlen(w);
        if (pos + wl > n) wl = n - pos;
        memcpy(text + pos, w, wl); pos += wl;
    }
    
    /* Binary: incrementing byte pattern */
    uint8_t* binary = (uint8_t*)malloc(n);
    for (size_t i = 0; i < n; i++) binary[i] = (uint8_t)(i & 0xFF);
    
    /* Zeros: RLE-friendly */
    uint8_t* zeros = (uint8_t*)calloc(n, 1);
    
    int levels[] = {3, 9, 10, 15, 22};
    for (int li = 0; li < 5; li++) {
        int lv = levels[li];
        fail += test_level("128K text",    text,   n, lv);
        fail += test_level("128K binary",  binary, n, lv);
        fail += test_level("128K zeros",   zeros,  n, lv);
        printf("\n");
    }
    
    free(text); free(binary); free(zeros);
    printf("%s: %d failure(s)\n", fail == 0 ? "ALL PASSED" : "FAILED", fail);
    return fail;
}
