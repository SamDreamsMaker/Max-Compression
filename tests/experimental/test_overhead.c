/* Measure exact overhead breakdown for small file compression */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);

int main(void) {
    FILE* f = fopen("/tmp/cantrbry/grammar.lsp", "rb");
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(n);
    fread(data, 1, n, f);
    fclose(f);
    
    size_t cap = n * 3;
    
    /* BWT */
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, data, n);
    printf("After BWT: %zu bytes (pidx=%zu)\n", n, pidx);
    
    /* MTF */
    mcx_mtf_encode(bwt, n);
    
    /* Count unique and 254+ */
    int freq[256] = {0};
    size_t high = 0;
    for (size_t i = 0; i < n; i++) { freq[bwt[i]]++; if (bwt[i] >= 254) high++; }
    int unique = 0;
    for (int i = 0; i < 256; i++) if (freq[i]) unique++;
    printf("After MTF: %d unique bytes, %zu >= 254 (%.1f%%)\n", unique, high, 100.0*high/n);
    
    /* RLE2 */
    uint8_t* rle = malloc(cap);
    size_t rle_sz = mcx_rle2_encode(rle, cap, bwt, n);
    printf("After RLE2: %zu bytes (%.1f%% of original)\n", rle_sz, 100.0*rle_sz/n);
    
    /* rANS */
    uint8_t* rans = malloc(cap);
    size_t rans_sz = mcx_rans_compress(rans, cap, rle, rle_sz);
    printf("After rANS: %zu bytes (%.2fx)\n", rans_sz, (double)n/rans_sz);
    
    /* Full pipeline */
    uint8_t* comp = malloc(cap);
    size_t comp_sz = mcx_compress(comp, cap, data, n, 20);
    printf("\nFull pipeline L20: %zu bytes (%.2fx)\n", comp_sz, (double)n/comp_sz);
    printf("Pure rANS output: %zu bytes\n", rans_sz);
    printf("Pipeline overhead: %zu bytes\n", comp_sz - rans_sz);
    printf("  Frame header: 20 bytes\n");
    printf("  Block count+size: 8 bytes\n");
    printf("  Genome byte: 1 byte\n");
    printf("  BWT pidx: 8 bytes\n");
    printf("  RLE32 size: 4 bytes\n");
    printf("  Total expected: 41 bytes\n");
    printf("  Actual: %zu bytes\n", comp_sz - rans_sz);
    printf("  Unexplained: %zd bytes\n", (ssize_t)(comp_sz - rans_sz - 41));
    
    free(data); free(bwt); free(rle); free(rans); free(comp);
    return 0;
}
