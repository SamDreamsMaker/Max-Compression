/* Test hybrid: BWT+MTF+RLE2 → then LZ-HC vs rANS on the RLE2 output */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rle_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);
extern size_t mcx_lzfse_compress(void*, size_t, const void*, size_t);
extern size_t mcx_lz_compress_hc(void*, size_t, const void*, size_t, int);

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
    
    /* BWT + MTF */
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, data, n);
    mcx_mtf_encode(bwt, n);
    
    /* RLE2 */
    uint8_t* rle = malloc(cap);
    size_t rle_sz = mcx_rle2_encode(rle, cap, bwt, n);
    
    /* RLE1 */
    uint8_t* rle1 = malloc(cap);
    size_t rle1_sz = mcx_rle_encode(rle1, cap, bwt, n);
    
    /* rANS on RLE2 */
    uint8_t* rans = malloc(cap);
    size_t rans_sz = mcx_rans_compress(rans, cap, rle, rle_sz);
    
    /* rANS on RLE1 */
    uint8_t* rans1 = malloc(cap);
    size_t rans1_sz = mcx_rans_compress(rans1, cap, rle1, rle1_sz);
    
    /* rANS directly on BWT+MTF (no RLE) */
    uint8_t* rans_raw = malloc(cap);
    size_t rans_raw_sz = mcx_rans_compress(rans_raw, cap, bwt, n);
    
    /* LZ-HC on RLE2 output */
    uint8_t* lz_rle = malloc(cap);
    size_t lz_rle_sz = mcx_lzfse_compress(lz_rle, cap, rle, rle_sz);
    
    /* LZ-HC directly on BWT+MTF */
    uint8_t* lz_bwt = malloc(cap);
    size_t lz_bwt_sz = mcx_lzfse_compress(lz_bwt, cap, bwt, n);
    
    printf("%-12s %5zu | rle2=%5zu rle1=%5zu | rANS(rle2)=%5zu rANS(rle1)=%5zu rANS(raw)=%5zu | LZ(rle2)=%5zu LZ(bwt)=%5zu\n",
           name, n,
           rle_sz, rle1_sz,
           mcx_is_error(rans_sz) ? 0 : rans_sz,
           mcx_is_error(rans1_sz) ? 0 : rans1_sz,
           mcx_is_error(rans_raw_sz) ? 0 : rans_raw_sz,
           mcx_is_error(lz_rle_sz) ? 0 : lz_rle_sz,
           mcx_is_error(lz_bwt_sz) ? 0 : lz_bwt_sz);
    
    free(data); free(bwt); free(rle); free(rle1); free(rans); free(rans1); free(rans_raw); free(lz_rle); free(lz_bwt);
}

int main(void) {
    test_file("/tmp/cantrbry/grammar.lsp");
    test_file("/tmp/cantrbry/xargs.1");
    test_file("/tmp/cantrbry/fields.c");
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    return 0;
}
