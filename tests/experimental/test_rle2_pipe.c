/**
 * Test RLE2 roundtrip through the exact same pipeline as core.c:
 * BWT → MTF → RLE2 → rANS → rANS_decompress → RLE2_decode → MTF_decode → BWT_inverse
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern size_t mcx_bwt_inverse(uint8_t*, size_t, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern void mcx_mtf_decode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rle2_decode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);
extern size_t mcx_rans_decompress(void*, size_t, const void*, size_t);

static int test_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("SKIP %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* orig = malloc(n);
    fread(orig, 1, n, f);
    fclose(f);
    
    const char* name = strrchr(path, '/') + 1;
    size_t cap = n * 3 + 4096;
    
    uint8_t* buf_bwt = malloc(n);
    uint8_t* buf_rle = malloc(cap);
    uint8_t* buf_rans = malloc(cap);
    uint8_t* buf_dec_rans = malloc(cap);
    uint8_t* buf_dec_rle = malloc(cap);
    uint8_t* buf_final = malloc(n);
    
    /* Compress: BWT → MTF → RLE2 → rANS */
    size_t pidx;
    mcx_bwt_forward(buf_bwt, &pidx, orig, n);
    mcx_mtf_encode(buf_bwt, n);
    
    size_t rle2_size = mcx_rle2_encode(buf_rle, cap, buf_bwt, n);
    if (rle2_size == 0) { printf("FAIL %s: rle2_encode returned 0\n", name); return 1; }
    
    size_t rans_size = mcx_rans_compress(buf_rans, cap, buf_rle, rle2_size);
    if (mcx_is_error(rans_size)) { printf("FAIL %s: rans_compress failed\n", name); return 1; }
    
    /* Decompress: rANS → RLE2 → MTF → BWT */
    size_t dec_rans = mcx_rans_decompress(buf_dec_rans, cap, buf_rans, rans_size);
    if (mcx_is_error(dec_rans)) { printf("FAIL %s: rans_decompress failed\n", name); return 1; }
    
    if (dec_rans != rle2_size) {
        printf("FAIL %s: rans roundtrip size mismatch %zu vs %zu\n", name, dec_rans, rle2_size);
        return 1;
    }
    
    size_t dec_rle = mcx_rle2_decode(buf_dec_rle, cap, buf_dec_rans, dec_rans);
    if (dec_rle == 0) { printf("FAIL %s: rle2_decode returned 0\n", name); return 1; }
    
    if (dec_rle != n) {
        printf("FAIL %s: rle2 roundtrip size %zu vs expected %zu\n", name, dec_rle, n);
        return 1;
    }
    
    mcx_mtf_decode(buf_dec_rle, dec_rle);
    mcx_bwt_inverse(buf_final, pidx, buf_dec_rle, dec_rle);
    
    if (memcmp(orig, buf_final, n) != 0) {
        printf("FAIL %s: data mismatch!\n", name);
        return 1;
    }
    
    printf("OK   %-16s %6zu → rle2 %6zu → rANS %6zu (%.2fx)\n",
           name, n, rle2_size, rans_size, (double)n / rans_size);
    
    free(orig); free(buf_bwt); free(buf_rle); free(buf_rans);
    free(buf_dec_rans); free(buf_dec_rle); free(buf_final);
    return 0;
}

int main(void) {
    int fails = 0;
    printf("RLE2 full pipeline roundtrip test:\n\n");
    fails += test_file("/tmp/cantrbry/alice29.txt");
    fails += test_file("/tmp/cantrbry/asyoulik.txt");
    fails += test_file("/tmp/cantrbry/lcet10.txt");
    fails += test_file("/tmp/cantrbry/plrabn12.txt");
    fails += test_file("/tmp/cantrbry/cp.html");
    fails += test_file("/tmp/cantrbry/fields.c");
    fails += test_file("/tmp/cantrbry/grammar.lsp");
    fails += test_file("/tmp/cantrbry/ptt5");
    fails += test_file("/tmp/cantrbry/kennedy.xls");
    printf("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails;
}
