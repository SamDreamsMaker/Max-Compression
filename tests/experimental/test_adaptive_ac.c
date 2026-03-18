/**
 * Test adaptive arithmetic coder: standalone and vs CM-rANS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../lib/entropy/adaptive_ac.h"

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    *out_size = fread(buf, 1, sz, f);
    fclose(f); return buf;
}

static int test_roundtrip(const uint8_t* src, size_t src_size, const char* name) {
    size_t cap = src_size * 2 + 1024;
    uint8_t* comp = malloc(cap);
    uint8_t* dec = malloc(src_size + 64);
    
    size_t csize = mcx_adaptive_ac_compress(comp, cap, src, src_size);
    if (csize == 0) {
        printf("  %-25s %8zu → COMPRESS FAILED\n", name, src_size);
        free(comp); free(dec);
        return 0;
    }
    
    size_t dsize = mcx_adaptive_ac_decompress(dec, src_size + 64, comp, csize);
    int ok = (dsize == src_size && memcmp(src, dec, src_size) == 0);
    
    printf("  %-25s %8zu → %8zu (%.2fx) %s\n",
           name, src_size, csize, (double)src_size/csize, ok ? "✓" : "✗ ROUNDTRIP FAIL");
    
    if (!ok && dsize > 0) {
        /* Find first difference */
        for (size_t i = 0; i < src_size && i < dsize; i++) {
            if (src[i] != dec[i]) {
                printf("    First diff at byte %zu: expected 0x%02x got 0x%02x\n", i, src[i], dec[i]);
                break;
            }
        }
        if (dsize != src_size) {
            printf("    Size mismatch: expected %zu got %zu\n", src_size, dsize);
        }
    }
    
    free(comp); free(dec);
    return ok;
}

static void test_file(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src) return;
    test_roundtrip(src, src_size, name);
    free(src);
}

int main(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  Adaptive AC — Roundtrip + Compression Test\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    /* Simple test vectors */
    {
        const char* text = "Hello, World! Hello, World! Hello, World!";
        test_roundtrip((const uint8_t*)text, strlen(text), "hello_repeat");
    }
    {
        uint8_t zeros[1000];
        memset(zeros, 0, sizeof(zeros));
        test_roundtrip(zeros, sizeof(zeros), "all_zeros");
    }
    {
        uint8_t seq[256];
        for (int i = 0; i < 256; i++) seq[i] = i;
        test_roundtrip(seq, sizeof(seq), "0-255_sequence");
    }
    
    printf("\n── Canterbury Corpus ──\n");
    test_file("corpora/alice29.txt", "alice29.txt");
    test_file("corpora/asyoulik.txt", "asyoulik.txt");
    test_file("corpora/lcet10.txt", "lcet10.txt");
    test_file("corpora/fields.c", "fields.c");
    test_file("corpora/cp.html", "cp.html");
    test_file("corpora/kennedy.xls", "kennedy.xls");
    
    printf("\n── Silesia Corpus ──\n");
    test_file("corpora/silesia/dickens", "sil/dickens");
    test_file("corpora/silesia/xml", "sil/xml");
    test_file("corpora/silesia/samba", "sil/samba");
    
    return 0;
}
