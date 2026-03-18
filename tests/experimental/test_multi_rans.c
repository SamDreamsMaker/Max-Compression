/* Test multi-table rANS vs single rANS on BWT+MTF+RLE2 output */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);
extern size_t mcx_multi_rans_compress(uint8_t*, size_t, const uint8_t*, size_t);

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
    
    uint8_t* r0 = malloc(cap);
    size_t r0_sz = mcx_rans_compress(r0, cap, rle, rle_sz);
    
    uint8_t* r1 = malloc(cap);
    size_t r1_sz = mcx_multi_rans_compress(r1, cap, rle, rle_sz);
    
    const char* name = strrchr(path, '/') + 1;
    printf("%-12s %6zu | single=%6zu (%.2fx) | multi=%6zu (%.2fx) | %+.1f%%\n",
           name, n,
           mcx_is_error(r0_sz) ? 0 : r0_sz, mcx_is_error(r0_sz) ? 0 : (double)n/r0_sz,
           mcx_is_error(r1_sz) ? 0 : r1_sz, mcx_is_error(r1_sz) ? 0 : (double)n/r1_sz,
           (mcx_is_error(r0_sz) || mcx_is_error(r1_sz)) ? 0 : 100.0*(double)((ssize_t)r0_sz - (ssize_t)r1_sz) / r0_sz);
    
    free(data); free(bwt); free(rle); free(r0); free(r1);
}

int main(void) {
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    test_file("/tmp/cantrbry/plrabn12.txt");
    test_file("/tmp/cantrbry/asyoulik.txt");
    test_file("/tmp/cantrbry/fields.c");
    test_file("/tmp/cantrbry/cp.html");
    return 0;
}
