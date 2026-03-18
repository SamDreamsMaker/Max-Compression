/**
 * Test: Dict v2 preprocessing → MCX L12 (BWT+CM-rANS) 
 * vs raw MCX L12
 * Also: Dict v2 → MCX L20 (Babel+rANS)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>

/* External dict v2 functions */
size_t mcx_babel_dict2_forward(uint8_t* dst, size_t dst_cap,
                                const uint8_t* src, size_t src_size);
size_t mcx_babel_dict2_inverse(uint8_t* dst, size_t dst_cap,
                                const uint8_t* src, size_t src_size);

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    *out_size = fread(buf, 1, sz, f);
    fclose(f); return buf;
}

static void test(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src || src_size < 10) { if(src) free(src); return; }
    
    /* Method 1: Raw L12 (BWT+CM-rANS) */
    size_t bound = mcx_compress_bound(src_size) + src_size;
    uint8_t* comp = malloc(bound);
    size_t raw_l12 = mcx_compress(comp, bound, src, src_size, 12);
    
    /* Method 2: Raw L3 (LZ77) */
    size_t raw_l3 = mcx_compress(comp, bound, src, src_size, 3);
    
    /* Method 3: Dict v2 → L12 */
    size_t dict_cap = src_size * 2 + 65536;
    uint8_t* dict_buf = malloc(dict_cap);
    size_t dict_size = mcx_babel_dict2_forward(dict_buf, dict_cap, src, src_size);
    
    size_t dict_l12 = 0, dict_l3 = 0;
    if (dict_size > 0) {
        size_t bound2 = mcx_compress_bound(dict_size) + dict_size;
        uint8_t* comp2 = malloc(bound2);
        dict_l12 = mcx_compress(comp2, bound2, dict_buf, dict_size, 12);
        dict_l3 = mcx_compress(comp2, bound2, dict_buf, dict_size, 3);
        
        /* Verify roundtrip: decompress L12 → dict inverse */
        uint8_t* dec_comp = malloc(dict_size + 1024);
        size_t dec_size = mcx_decompress(dec_comp, dict_size + 1024, comp2, dict_l12);
        if (dec_size == dict_size) {
            uint8_t* dec_orig = malloc(src_size + 1024);
            size_t orig_size = mcx_babel_dict2_inverse(dec_orig, src_size + 1024, dec_comp, dec_size);
            int ok = (orig_size == src_size && memcmp(src, dec_orig, src_size) == 0);
            if (!ok) printf("  *** ROUNDTRIP FAIL for %s\n", name);
            free(dec_orig);
        }
        free(dec_comp);
        free(comp2);
    }
    
    printf("  %-25s %8zu  L3=%7zu(%.2fx)  L12=%7zu(%.2fx)",
           name, src_size, raw_l3, (double)src_size/raw_l3, raw_l12, (double)src_size/raw_l12);
    
    if (dict_size > 0) {
        printf("  D+L3=%7zu(%.2fx)  D+L12=%7zu(%.2fx)",
               dict_l3, (double)src_size/dict_l3, dict_l12, (double)src_size/dict_l12);
        if (dict_l12 < raw_l12) printf(" ✅");
        else printf(" ❌");
    } else {
        printf("  (no dict)");
    }
    printf("\n");
    
    free(src); free(comp); free(dict_buf);
}

int main(void) {
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("  Dict v2 + BWT benchmark: Does preprocessing help?\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n\n");
    
    test("corpora/alice29.txt", "alice29.txt");
    test("corpora/asyoulik.txt", "asyoulik.txt");
    test("corpora/cp.html", "cp.html");
    test("corpora/fields.c", "fields.c");
    test("corpora/lcet10.txt", "lcet10.txt");
    test("corpora/plrabn12.txt", "plrabn12.txt");
    test("corpora/kennedy.xls", "kennedy.xls");
    test("corpora/silesia/dickens", "sil/dickens");
    test("corpora/silesia/xml", "sil/xml");
    test("corpora/silesia/samba", "sil/samba");
    test("corpora/silesia/webster", "sil/webster");
    test("corpora/silesia/nci", "sil/nci");
    test("corpora/silesia/mozilla", "sil/mozilla");
    test("lib/core.c", "core.c");
    
    return 0;
}
