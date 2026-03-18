/* Roundtrip test for multi-table rANS */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_multi_rans_compress(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_multi_rans_decompress(uint8_t*, size_t, const uint8_t*, size_t);

int main(void) {
    FILE* f = fopen("/tmp/cantrbry/alice29.txt", "rb");
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
    
    printf("Input: %zu bytes (RLE2 output)\n", rle_sz);
    
    /* Compress */
    uint8_t* comp = malloc(cap);
    size_t comp_sz = mcx_multi_rans_compress(comp, cap, rle, rle_sz);
    printf("Compressed: %zu bytes (%.2fx)\n", comp_sz, (double)rle_sz/comp_sz);
    
    if (mcx_is_error(comp_sz)) {
        printf("COMPRESS FAILED: error %zu\n", comp_sz);
        return 1;
    }
    
    /* Decompress */
    uint8_t* dec = malloc(rle_sz + 1024);
    size_t dec_sz = mcx_multi_rans_decompress(dec, rle_sz + 1024, comp, comp_sz);
    printf("Decompressed: %zu bytes\n", dec_sz);
    
    if (mcx_is_error(dec_sz)) {
        printf("DECOMPRESS FAILED: error %zu\n", dec_sz);
        return 1;
    }
    
    if (dec_sz != rle_sz) {
        printf("SIZE MISMATCH: expected %zu, got %zu\n", rle_sz, dec_sz);
        return 1;
    }
    
    /* Compare */
    int mismatches = 0;
    size_t first_mm = 0;
    for (size_t i = 0; i < rle_sz; i++) {
        if (rle[i] != dec[i]) {
            if (mismatches == 0) first_mm = i;
            mismatches++;
        }
    }
    
    if (mismatches > 0) {
        printf("MISMATCH: %d bytes differ, first at offset %zu\n", mismatches, first_mm);
        printf("  Expected: %02x %02x %02x %02x\n", rle[first_mm], rle[first_mm+1], rle[first_mm+2], rle[first_mm+3]);
        printf("  Got:      %02x %02x %02x %02x\n", dec[first_mm], dec[first_mm+1], dec[first_mm+2], dec[first_mm+3]);
    } else {
        printf("ROUNDTRIP OK ✓\n");
    }
    
    free(data); free(bwt); free(rle); free(comp); free(dec);
    return mismatches > 0 ? 1 : 0;
}
