/* Test: BWT → MTF → RLE2 → BWT2 → MTF2 → rANS vs single pass */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rle_encode(uint8_t*, size_t, const uint8_t*, size_t);
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
    
    const char* name = strrchr(path, '/') + 1;
    size_t cap = n * 3 + 4096;
    
    /* Single pass: BWT → MTF → RLE2 → rANS */
    uint8_t* bwt1 = malloc(n);
    size_t pidx1;
    mcx_bwt_forward(bwt1, &pidx1, data, n);
    mcx_mtf_encode(bwt1, n);
    uint8_t* rle1 = malloc(cap);
    size_t rle1_sz = mcx_rle2_encode(rle1, cap, bwt1, n);
    uint8_t* comp1 = malloc(cap);
    size_t comp1_sz = mcx_rans_compress(comp1, cap, rle1, rle1_sz);
    
    /* Double pass: BWT → MTF → BWT2 → MTF2 → RLE2 → rANS */
    uint8_t* bwt2a = malloc(n);
    size_t pidx2a;
    mcx_bwt_forward(bwt2a, &pidx2a, data, n);
    mcx_mtf_encode(bwt2a, n);
    /* Second BWT on MTF output */
    uint8_t* bwt2b = malloc(n);
    size_t pidx2b;
    mcx_bwt_forward(bwt2b, &pidx2b, bwt2a, n);
    mcx_mtf_encode(bwt2b, n);
    uint8_t* rle2 = malloc(cap);
    size_t rle2_sz = mcx_rle2_encode(rle2, cap, bwt2b, n);
    uint8_t* comp2 = malloc(cap);
    size_t comp2_sz = mcx_rans_compress(comp2, cap, rle2, rle2_sz);
    
    /* BWT → MTF → RLE2 → BWT3 on RLE2 output → MTF3 → rANS */
    uint8_t* bwt3a = malloc(n);
    size_t pidx3a;
    mcx_bwt_forward(bwt3a, &pidx3a, data, n);
    mcx_mtf_encode(bwt3a, n);
    uint8_t* rle3a = malloc(cap);
    size_t rle3a_sz = mcx_rle2_encode(rle3a, cap, bwt3a, n);
    /* BWT on RLE2 output */
    uint8_t* bwt3b = malloc(rle3a_sz);
    size_t pidx3b;
    mcx_bwt_forward(bwt3b, &pidx3b, rle3a, rle3a_sz);
    mcx_mtf_encode(bwt3b, rle3a_sz);
    uint8_t* rle3b = malloc(cap);
    size_t rle3b_sz = mcx_rle2_encode(rle3b, cap, bwt3b, rle3a_sz);
    uint8_t* comp3 = malloc(cap);
    size_t comp3_sz = mcx_rans_compress(comp3, cap, rle3b, rle3b_sz);
    
    printf("%-16s %6zu | 1×BWT+RLE2: %6zu (%.2fx) | 2×BWT+RLE2: %6zu (%.2fx) | BWT+RLE2+BWT: %6zu (%.2fx)\n",
           name, n,
           mcx_is_error(comp1_sz) ? 0 : comp1_sz, mcx_is_error(comp1_sz) ? 0 : (double)n/comp1_sz,
           mcx_is_error(comp2_sz) ? 0 : comp2_sz, mcx_is_error(comp2_sz) ? 0 : (double)n/comp2_sz,
           mcx_is_error(comp3_sz) ? 0 : comp3_sz, mcx_is_error(comp3_sz) ? 0 : (double)n/comp3_sz);
    
    free(data); free(bwt1); free(rle1); free(comp1);
    free(bwt2a); free(bwt2b); free(rle2); free(comp2);
    free(bwt3a); free(rle3a); free(bwt3b); free(rle3b); free(comp3);
}

int main(void) {
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    test_file("/tmp/cantrbry/plrabn12.txt");
    return 0;
}
