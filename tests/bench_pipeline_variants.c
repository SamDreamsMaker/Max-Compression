/**
 * Test pipeline variants to find optimal combination.
 * Uses internal API to try different genome configurations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxcomp/maxcomp.h>

/* Access internal structures */
typedef struct {
    uint8_t use_bwt;
    uint8_t use_mtf_rle;
    uint8_t use_delta;
    uint8_t entropy_coder;  /* 0=FSE, 1=rANS, 2=CM-rANS */
    uint8_t cm_learning;
} mcx_genome;

/* These are internal functions, declared here for testing */
extern mcx_genome mcx_evolve(const uint8_t* data, size_t size, int level);

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    *out_size = fread(buf, 1, sz, f);
    fclose(f); return buf;
}

static void test_file(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src) return;
    
    size_t bound = mcx_compress_bound(src_size) + src_size;
    uint8_t* comp = malloc(bound);
    uint8_t* dec = malloc(src_size + 1024);
    
    /* Test several levels to see what genome gets selected */
    int levels[] = {1, 3, 6, 10, 12, 15, 18, 22};
    
    printf("  %-20s %8zu  ", name, src_size);
    for (int li = 0; li < 8; li++) {
        size_t csize = mcx_compress(comp, bound, src, src_size, levels[li]);
        if (!mcx_is_error(csize)) {
            /* Verify */
            size_t dsize = mcx_decompress(dec, src_size + 1024, comp, csize);
            int ok = (!mcx_is_error(dsize) && dsize == src_size && memcmp(src, dec, src_size) == 0);
            printf("L%d=%zu(%.2fx)%s ", levels[li], csize, (double)src_size/csize, ok?"":"!");
        } else {
            printf("L%d=ERR ", levels[li]);
        }
    }
    printf("\n");
    
    free(src); free(comp); free(dec);
}

int main(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  MCX All-Levels Comparison\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    test_file("corpora/alice29.txt", "alice29.txt");
    test_file("corpora/lcet10.txt", "lcet10.txt");
    test_file("corpora/kennedy.xls", "kennedy.xls");
    test_file("corpora/fields.c", "fields.c");
    test_file("corpora/silesia/dickens", "sil/dickens");
    test_file("corpora/silesia/xml", "sil/xml");
    test_file("corpora/silesia/samba", "sil/samba");
    
    return 0;
}
