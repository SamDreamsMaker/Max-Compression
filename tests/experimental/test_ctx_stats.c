/* Analyze context statistics after BWT+MTF+RLE2 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);

static void analyze_contexts(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(n);
    fread(data, 1, n, f);
    fclose(f);
    
    /* BWT + MTF + RLE2 */
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, data, n);
    mcx_mtf_encode(bwt, n);
    
    size_t cap = n * 2;
    uint8_t* rle = malloc(cap);
    size_t rle_sz = mcx_rle2_encode(rle, cap, bwt, n);
    
    /* Count active contexts and transitions */
    int ctx_active[256] = {0}; /* How many contexts have at least 1 transition */
    int pair_count = 0;        /* Total (ctx, sym) pairs with count > 0 */
    int total_ctx = 0;
    
    uint32_t raw[256][256];
    memset(raw, 0, sizeof(raw));
    if (rle_sz > 0) raw[0][rle[0]]++;
    for (size_t i = 1; i < rle_sz; i++) {
        raw[rle[i-1]][rle[i]]++;
    }
    
    for (int c = 0; c < 256; c++) {
        int active = 0;
        for (int s = 0; s < 256; s++) {
            if (raw[c][s] > 0) { active++; pair_count++; }
        }
        if (active > 0) { ctx_active[c] = active; total_ctx++; }
    }
    
    const char* name = strrchr(path, '/') + 1;
    printf("%-12s %6zu → rle2 %6zu | %3d active contexts, %5d pairs | sparse table: ~%d bytes\n",
           name, n, rle_sz, total_ctx, pair_count,
           (int)(1 + total_ctx * (1 + 1) + pair_count * (1 + 2))); /* ctx_count + per-ctx(id+num_sym) + per-pair(sym+freq16) */
    
    free(data); free(bwt); free(rle);
}

int main(void) {
    analyze_contexts("/tmp/cantrbry/grammar.lsp");
    analyze_contexts("/tmp/cantrbry/xargs.1");
    analyze_contexts("/tmp/cantrbry/fields.c");
    analyze_contexts("/tmp/cantrbry/alice29.txt");
    analyze_contexts("/tmp/cantrbry/lcet10.txt");
    analyze_contexts("/tmp/cantrbry/plrabn12.txt");
    return 0;
}
