/**
 * Compare RLE vs RLE2 (RUNA/RUNB) on BWT+MTF output.
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rle2_decode(uint8_t*, size_t, const uint8_t*, size_t);
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
    
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, data, n);
    mcx_mtf_encode(bwt, n);
    
    /* RLE1 */
    size_t rle_cap = n + n/4 + 1024;
    uint8_t* rle1 = malloc(rle_cap);
    size_t rle1_size = mcx_rle_encode(rle1, rle_cap, bwt, n);
    
    /* RLE2 (RUNA/RUNB) */
    uint8_t* rle2 = malloc(rle_cap);
    size_t rle2_size = mcx_rle2_encode(rle2, rle_cap, bwt, n);
    
    /* rANS on both */
    size_t rans_cap = n * 2;
    uint8_t* r1_buf = malloc(rans_cap);
    uint8_t* r2_buf = malloc(rans_cap);
    size_t r1_comp = mcx_rans_compress(r1_buf, rans_cap, rle1, rle1_size);
    size_t r2_comp = mcx_rans_compress(r2_buf, rans_cap, rle2, rle2_size);
    
    /* Verify RLE2 roundtrip */
    uint8_t* decoded = malloc(n);
    size_t dec_size = mcx_rle2_decode(decoded, n, rle2, rle2_size);
    int ok = (dec_size == n && memcmp(bwt, decoded, n) == 0);
    
    printf("%-16s %6zu | RLE1: %6zu → rANS %6zu (%5.2fx) | RLE2: %6zu → rANS %6zu (%5.2fx) | Δ=%+.1f%% %s\n",
           name, n,
           rle1_size, mcx_is_error(r1_comp) ? 0 : r1_comp, mcx_is_error(r1_comp) ? 0 : (double)n/r1_comp,
           rle2_size, mcx_is_error(r2_comp) ? 0 : r2_comp, mcx_is_error(r2_comp) ? 0 : (double)n/r2_comp,
           mcx_is_error(r2_comp) || mcx_is_error(r1_comp) ? 0 :
               ((double)r2_comp/r1_comp - 1.0) * 100.0,
           ok ? "✓" : "ROUNDTRIP FAIL");
    
    free(data); free(bwt); free(rle1); free(rle2);
    free(r1_buf); free(r2_buf); free(decoded);
}

int main(void) {
    printf("RLE1 vs RLE2 (RUNA/RUNB) comparison:\n\n");
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/asyoulik.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    test_file("/tmp/cantrbry/plrabn12.txt");
    test_file("/tmp/cantrbry/kennedy.xls");
    test_file("/tmp/cantrbry/ptt5");
    test_file("/tmp/cantrbry/fields.c");
    test_file("/tmp/cantrbry/grammar.lsp");
    return 0;
}
