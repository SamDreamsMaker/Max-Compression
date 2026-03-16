/**
 * Debug RLE2 - step by step
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern size_t mcx_bwt_inverse(uint8_t*, const uint8_t*, size_t, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern void mcx_mtf_decode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rle2_decode(uint8_t*, size_t, const uint8_t*, size_t);

int main(void) {
    /* Use a small test string */
    const char* test = "abracadabra abracadabra abracadabra";
    size_t n = strlen(test);
    uint8_t* orig = (uint8_t*)malloc(n);
    memcpy(orig, test, n);
    
    printf("Original (%zu): ", n);
    for (size_t i = 0; i < n; i++) printf("%02x ", orig[i]);
    printf("\n\n");
    
    /* BWT */
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, orig, n);
    printf("BWT (pidx=%zu): ", pidx);
    for (size_t i = 0; i < n; i++) printf("%02x ", bwt[i]);
    printf("\n");
    
    /* Save copy for later comparison */
    uint8_t* bwt_copy = malloc(n);
    memcpy(bwt_copy, bwt, n);
    
    /* MTF */
    mcx_mtf_encode(bwt, n);
    printf("MTF: ");
    for (size_t i = 0; i < n; i++) printf("%02x ", bwt[i]);
    printf("\n");
    
    /* Save MTF output */
    uint8_t* mtf_copy = malloc(n);
    memcpy(mtf_copy, bwt, n);
    
    /* RLE2 encode */
    size_t rle_cap = n * 2 + 100;
    uint8_t* rle = malloc(rle_cap);
    size_t rle_size = mcx_rle2_encode(rle, rle_cap, bwt, n);
    printf("RLE2 (%zu→%zu): ", n, rle_size);
    for (size_t i = 0; i < rle_size; i++) printf("%02x ", rle[i]);
    printf("\n");
    
    /* RLE2 decode */
    uint8_t* dec = malloc(n + 100);
    size_t dec_size = mcx_rle2_decode(dec, n + 100, rle, rle_size);
    printf("RLE2 dec (%zu): ", dec_size);
    for (size_t i = 0; i < dec_size && i < n; i++) printf("%02x ", dec[i]);
    printf("\n");
    
    /* Compare MTF output vs RLE2 roundtrip */
    if (dec_size != n) {
        printf("SIZE MISMATCH: %zu vs %zu\n", dec_size, n);
    } else if (memcmp(mtf_copy, dec, n) != 0) {
        printf("DATA MISMATCH at RLE2 level!\n");
        for (size_t i = 0; i < n; i++) {
            if (mtf_copy[i] != dec[i]) {
                printf("  pos %zu: expected %02x got %02x\n", i, mtf_copy[i], dec[i]);
                if (i > 3) break;
            }
        }
    } else {
        printf("RLE2 roundtrip OK\n");
    }
    
    /* MTF decode */
    mcx_mtf_decode(dec, dec_size);
    printf("MTF dec: ");
    for (size_t i = 0; i < dec_size && i < n; i++) printf("%02x ", dec[i]);
    printf("\n");
    
    /* Compare with BWT output */
    if (memcmp(bwt_copy, dec, n) != 0) {
        printf("MTF ROUNDTRIP MISMATCH\n");
    }
    
    /* BWT inverse */
    uint8_t* final = malloc(n);
    mcx_bwt_inverse(final, pidx, dec, dec_size);
    printf("Final: ");
    for (size_t i = 0; i < n; i++) printf("%02x ", final[i]);
    printf("\n\n");
    
    if (memcmp(orig, final, n) == 0) {
        printf("FULL PIPELINE OK!\n");
    } else {
        printf("FULL PIPELINE FAIL!\n");
    }
    
    free(orig); free(bwt); free(bwt_copy); free(mtf_copy);
    free(rle); free(dec); free(final);
    return 0;
}
