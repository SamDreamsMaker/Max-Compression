/* Test segmented rANS: split RLE2 output into segments, each with own freq table.
 * bzip2 uses 6 Huffman tables switching every 50 bytes — we try similar approach. */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);

static size_t segmented_rans(const uint8_t* data, size_t n, int seg_size) {
    size_t total = 0;
    size_t cap = n + 4096;
    uint8_t* buf = malloc(cap);
    int num_segs = 0;
    
    for (size_t off = 0; off < n; off += seg_size) {
        size_t len = (off + seg_size <= n) ? seg_size : (n - off);
        size_t cs = mcx_rans_compress(buf, cap, data + off, len);
        if (mcx_is_error(cs)) { free(buf); return 0; }
        total += cs;
        num_segs++;
    }
    /* Add segment count header */
    total += 4; /* num_segs */
    total += num_segs * 4; /* per-segment compressed size */
    
    free(buf);
    return total;
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
    uint8_t* bwt = malloc(n);
    size_t pidx;
    mcx_bwt_forward(bwt, &pidx, data, n);
    mcx_mtf_encode(bwt, n);
    
    uint8_t* rle = malloc(cap);
    size_t rle_sz = mcx_rle2_encode(rle, cap, bwt, n);
    
    /* Single rANS (current) */
    uint8_t* r0 = malloc(cap);
    size_t r0_sz = mcx_rans_compress(r0, cap, rle, rle_sz);
    
    const char* name = strrchr(path, '/') + 1;
    printf("%-12s %6zu → rle2 %6zu | single=%6zu (%.2fx)", name, n, rle_sz, r0_sz, (double)n/r0_sz);
    
    /* Segmented rANS with different segment sizes */
    int segs[] = {512, 1024, 2048, 4096, 8192, 16384, 32768};
    for (int i = 0; i < 7; i++) {
        if ((size_t)segs[i] >= rle_sz) break;
        size_t ss = segmented_rans(rle, rle_sz, segs[i]);
        printf(" | %dk=%zu", segs[i]/1024 > 0 ? segs[i]/1024 : 0, ss);
        if (ss && ss < r0_sz) printf("✓");
    }
    printf("\n");
    
    free(data); free(bwt); free(rle); free(r0);
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
