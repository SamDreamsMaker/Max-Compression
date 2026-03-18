/* Test context-aware MTF: use separate MTF lists per previous byte.
 * After BWT, byte sequences like "eee" "ttt" become grouped.
 * Context MTF should make the output more predictable for rANS. */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);

/* Context MTF: 256 separate MTF lists, one per previous output byte */
static void mtf_ctx_encode(uint8_t* data, size_t n) {
    uint8_t lists[256][256]; /* lists[ctx][rank] = byte */
    
    /* Initialize all 256 lists to identity */
    for (int c = 0; c < 256; c++)
        for (int i = 0; i < 256; i++)
            lists[c][i] = (uint8_t)i;
    
    uint8_t prev = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = data[i];
        uint8_t* list = lists[prev];
        
        /* Find rank */
        int rank;
        for (rank = 0; rank < 256; rank++) {
            if (list[rank] == b) break;
        }
        
        /* Move to front */
        if (rank > 0) {
            memmove(list + 1, list, rank);
            list[0] = b;
        }
        
        data[i] = (uint8_t)rank;
        prev = (uint8_t)rank; /* Context is the output rank, not the input byte */
    }
}

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
    
    /* Standard: BWT → MTF → RLE2 → rANS */
    uint8_t* bwt1 = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt1, &pidx, data, n);
    uint8_t* bwt1_copy = malloc(n);
    memcpy(bwt1_copy, bwt1, n);
    
    mcx_mtf_encode(bwt1, n);
    uint8_t* rle1 = malloc(cap);
    size_t rle1_sz = mcx_rle2_encode(rle1, cap, bwt1, n);
    uint8_t* r1 = malloc(cap);
    size_t r1_sz = mcx_rans_compress(r1, cap, rle1, rle1_sz);
    
    /* Context MTF: BWT → ctx_MTF → RLE2 → rANS */
    mtf_ctx_encode(bwt1_copy, n);
    uint8_t* rle2 = malloc(cap);
    size_t rle2_sz = mcx_rle2_encode(rle2, cap, bwt1_copy, n);
    uint8_t* r2 = malloc(cap);
    size_t r2_sz = mcx_rans_compress(r2, cap, rle2, rle2_sz);
    
    const char* name = strrchr(path, '/') + 1;
    printf("%-12s %6zu | std_mtf: rle=%6zu rans=%6zu (%.2fx) | ctx_mtf: rle=%6zu rans=%6zu (%.2fx) | %+.1f%%\n",
           name, n, rle1_sz, r1_sz, (double)n/r1_sz,
           rle2_sz, r2_sz, (double)n/r2_sz,
           100.0*(double)(r1_sz - r2_sz)/r1_sz);
    
    free(data); free(bwt1); free(bwt1_copy); free(rle1); free(rle2); free(r1); free(r2);
}

int main(void) {
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    test_file("/tmp/cantrbry/plrabn12.txt");
    test_file("/tmp/cantrbry/asyoulik.txt");
    test_file("/tmp/cantrbry/fields.c");
    test_file("/tmp/cantrbry/cp.html");
    test_file("/tmp/cantrbry/grammar.lsp");
    return 0;
}
