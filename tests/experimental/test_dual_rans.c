/* Test dual-table rANS: split symbols into "low" (0-3) and "high" (4+) streams.
 * After BWT+MTF+RLE2, ~70-80% of symbols are 0-3. Two focused tables should
 * be more precise than one averaging everything. */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);

static void test_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(n);
    fread(data, 1, n, f);
    fclose(f);
    
    size_t cap = n * 2;
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, data, n);
    mcx_mtf_encode(bwt, n);
    
    uint8_t* rle = malloc(cap);
    size_t rle_sz = mcx_rle2_encode(rle, cap, bwt, n);
    
    /* Measure H0 entropy */
    int freq[256] = {0};
    for (size_t i = 0; i < rle_sz; i++) freq[rle[i]]++;
    double h0 = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / rle_sz;
        h0 -= p * log2(p);
    }
    
    /* Measure H1 entropy (order-1) */
    int ctx[256][256];
    memset(ctx, 0, sizeof(ctx));
    for (size_t i = 1; i < rle_sz; i++) ctx[rle[i-1]][rle[i]]++;
    double h1 = 0;
    for (int c = 0; c < 256; c++) {
        int ctot = 0;
        for (int s = 0; s < 256; s++) ctot += ctx[c][s];
        if (ctot == 0) continue;
        double pw = (double)ctot / (rle_sz - 1);
        double hc = 0;
        for (int s = 0; s < 256; s++) {
            if (ctx[c][s] == 0) continue;
            double p = (double)ctx[c][s] / ctot;
            hc -= p * log2(p);
        }
        h1 += pw * hc;
    }
    
    /* Single rANS */
    uint8_t* r0 = malloc(cap);
    size_t r0_sz = mcx_rans_compress(r0, cap, rle, rle_sz);
    
    const char* name = strrchr(path, '/') + 1;
    double min_h0 = rle_sz * h0 / 8;
    double min_h1 = (rle_sz - 1) * h1 / 8;
    
    printf("%-12s %6zu rle2=%6zu | H0=%.3f (%5.0f) H1=%.3f (%5.0f) | rANS=%5zu | gap_to_H0=%+.1f%% gap_to_H1=%+.1f%% | bzip2_target=%.0f\n",
           name, n, rle_sz, h0, min_h0, h1, min_h1,
           r0_sz,
           100.0*(r0_sz - min_h0)/min_h0,
           100.0*(r0_sz - min_h1)/min_h1,
           n / 3.52); /* bzip2 ratio for alice29 */
    
    free(data); free(bwt); free(rle); free(r0);
}

int main(void) {
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    test_file("/tmp/cantrbry/plrabn12.txt");
    test_file("/tmp/cantrbry/asyoulik.txt");
    return 0;
}
