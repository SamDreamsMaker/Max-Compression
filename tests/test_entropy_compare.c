/**
 * Compare entropy coders on BWT+MTF+RLE output to find the best one.
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Internal headers for direct access */
#include "../lib/internal.h"
#include "../lib/entropy/entropy.h"

extern size_t mcx_bwt_forward(uint8_t* dst, size_t* primary_idx,
                               const uint8_t* src, size_t src_size);
extern void mcx_mtf_encode(uint8_t* data, size_t size);
extern size_t mcx_rle_encode(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size);
extern size_t mcx_fse_compress(void* dst, size_t dst_cap,
                                const void* src, size_t src_size);

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
    
    /* BWT */
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, data, n);
    
    /* MTF */
    mcx_mtf_encode(bwt, n);
    
    /* RLE */
    uint8_t* rle = malloc(n + n/4 + 1024);
    size_t rle_size = mcx_rle_encode(rle, n + n/4 + 1024, bwt, n);
    
    /* H0 of RLE output */
    size_t freq[256] = {0};
    for (size_t i = 0; i < rle_size; i++) freq[rle[i]]++;
    double h0 = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / rle_size;
        h0 -= p * log2(p);
    }
    size_t theoretical = (size_t)(rle_size * h0 / 8.0);
    
    /* Huffman */
    size_t huf_cap = n * 2;
    uint8_t* huf_buf = malloc(huf_cap);
    size_t huf_size = mcx_huffman_compress(huf_buf, huf_cap, rle, rle_size, NULL);
    
    /* rANS */
    uint8_t* rans_buf = malloc(huf_cap);
    size_t rans_size = mcx_rans_compress(rans_buf, huf_cap, rle, rle_size);
    
    /* FSE */
    uint8_t* fse_buf = malloc(huf_cap);
    size_t fse_size = mcx_fse_compress(fse_buf, huf_cap, rle, rle_size);
    
    printf("%-16s %6zu → RLE %6zu (H0=%.2f) → theory %6zu | Huf %6zu | rANS %6zu | FSE %6zu | best=%s\n",
           name, n, rle_size, h0, theoretical,
           MCX_IS_ERROR(huf_size) ? 0 : huf_size,
           MCX_IS_ERROR(rans_size) ? 0 : rans_size,
           fse_size == 0 ? 0 : fse_size,
           (fse_size > 0 && fse_size <= (MCX_IS_ERROR(rans_size) ? 999999 : rans_size) && fse_size <= (MCX_IS_ERROR(huf_size) ? 999999 : huf_size)) ? "FSE" :
           (!MCX_IS_ERROR(rans_size) && rans_size <= (MCX_IS_ERROR(huf_size) ? 999999 : huf_size)) ? "rANS" : "Huf");
    
    free(data); free(bwt); free(rle); free(huf_buf); free(rans_buf); free(fse_buf);
}

int main(void) {
    printf("Entropy coder comparison on BWT+MTF+RLE output:\n\n");
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/asyoulik.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    test_file("/tmp/cantrbry/plrabn12.txt");
    test_file("/tmp/cantrbry/cp.html");
    test_file("/tmp/cantrbry/fields.c");
    test_file("/tmp/cantrbry/kennedy.xls");
    test_file("/tmp/cantrbry/ptt5");
    return 0;
}
