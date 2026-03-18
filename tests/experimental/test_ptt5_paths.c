/* Test different compression paths for ptt5 after stride-delta */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_bwt_inverse(uint8_t*, size_t, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rle_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);
extern size_t mcx_lzfse_compress(void*, size_t, const void*, size_t);

int main(void) {
    FILE* f = fopen("/tmp/cantrbry/ptt5", "rb");
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(n);
    fread(data, 1, n, f);
    fclose(f);
    
    size_t cap = n * 3;
    int stride = 216;
    
    /* Apply stride-delta */
    uint8_t* delta = malloc(n);
    memcpy(delta, data, n);
    for (size_t i = n - 1; i >= (size_t)stride; i--) {
        delta[i] = data[i] - data[i - stride];
    }
    
    /* Count zeros */
    size_t zeros = 0;
    for (size_t i = 0; i < n; i++) if (delta[i] == 0) zeros++;
    printf("After stride=%d: %zu/%zu zeros (%.1f%%)\n\n", stride, zeros, n, 100.0*zeros/n);
    
    /* Path 1: rANS direct on delta */
    uint8_t* r1 = malloc(cap);
    size_t r1_sz = mcx_rans_compress(r1, cap, delta, n);
    printf("Path 1 (rANS direct):       %6zu bytes (%.2fx)\n", r1_sz, (double)n/r1_sz);
    
    /* Path 2: RLE2 + rANS */
    uint8_t* rle2 = malloc(cap);
    size_t rle2_sz = mcx_rle2_encode(rle2, cap, delta, n);
    uint8_t* r2 = malloc(cap);
    size_t r2_sz = mcx_rans_compress(r2, cap, rle2, rle2_sz);
    printf("Path 2 (RLE2+rANS):         %6zu bytes (%.2fx)  rle2=%zu\n", r2_sz, (double)n/r2_sz, rle2_sz);
    
    /* Path 3: BWT + MTF + RLE2 + rANS */
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, delta, n);
    mcx_mtf_encode(bwt, n);
    uint8_t* rle3 = malloc(cap);
    size_t rle3_sz = mcx_rle2_encode(rle3, cap, bwt, n);
    uint8_t* r3 = malloc(cap);
    size_t r3_sz = mcx_rans_compress(r3, cap, rle3, rle3_sz);
    printf("Path 3 (BWT+MTF+RLE2+rANS): %6zu bytes (%.2fx)  rle2=%zu\n", r3_sz, (double)n/r3_sz, rle3_sz);
    
    /* Path 4: RLE1 + rANS */
    uint8_t* rle1 = malloc(cap);
    size_t rle1_sz = mcx_rle_encode(rle1, cap, delta, n);
    uint8_t* r4 = malloc(cap);
    size_t r4_sz = mcx_rans_compress(r4, cap, rle1, rle1_sz);
    printf("Path 4 (RLE1+rANS):         %6zu bytes (%.2fx)  rle1=%zu\n", r4_sz, (double)n/r4_sz, rle1_sz);
    
    /* Path 5: LZ multistream FSE on delta */
    uint8_t* r5 = malloc(cap);
    size_t r5_sz = mcx_lzfse_compress(r5, cap, delta, n);
    printf("Path 5 (LZ-FSE on delta):   %6zu bytes (%.2fx)\n", mcx_is_error(r5_sz) ? 0 : r5_sz, mcx_is_error(r5_sz) ? 0 : (double)n/r5_sz);
    
    /* Path 6: BWT + MTF + RLE2 + rANS on ORIGINAL (no stride) */
    uint8_t* bwt6 = malloc(n);
    size_t pidx6;
    mcx_bwt_forward(bwt6, &pidx6, data, n);
    mcx_mtf_encode(bwt6, n);
    uint8_t* rle6 = malloc(cap);
    size_t rle6_sz = mcx_rle2_encode(rle6, cap, bwt6, n);
    uint8_t* r6 = malloc(cap);
    size_t r6_sz = mcx_rans_compress(r6, cap, rle6, rle6_sz);
    printf("Path 6 (BWT+RLE2 no stride):%6zu bytes (%.2fx)\n", r6_sz, (double)n/r6_sz);
    
    printf("\nxz reference: %zu / 12.22 = ~%zu bytes\n", n, (size_t)(n/12.22));
    
    free(data); free(delta); free(bwt); free(rle2); free(rle3); free(rle1);
    free(r1); free(r2); free(r3); free(r4); free(r5); free(r6); free(bwt6); free(rle6);
    return 0;
}
