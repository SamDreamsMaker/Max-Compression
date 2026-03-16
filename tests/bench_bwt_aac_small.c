/**
 * Quick benchmark: BWT + MTF + Adaptive AC vs MCX L12
 * Canterbury corpus only (small files)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxcomp/maxcomp.h>

extern size_t mcx_bwt_forward(uint8_t* dst, size_t* primary_idx,
                               const uint8_t* src, size_t size);
extern void mcx_mtf_encode(uint8_t* data, size_t size);
extern void mcx_mtf_decode(uint8_t* data, size_t size);
extern size_t mcx_bwt_inverse(uint8_t* dst, size_t primary_idx,
                               const uint8_t* src, size_t size);

extern size_t mcx_adaptive_ac_compress(uint8_t* dst, size_t dst_cap,
                                        const uint8_t* src, size_t src_size);
extern size_t mcx_adaptive_ac_decompress(uint8_t* dst, size_t dst_cap,
                                          const uint8_t* src, size_t src_size);

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    *out_size = fread(buf, 1, sz, f);
    fclose(f); return buf;
}

static void bench(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src) { printf("  %-25s SKIP\n", name); return; }
    
    /* Skip files > 2MB for speed */
    if (src_size > 2 * 1024 * 1024) {
        printf("  %-25s %8zu → SKIP (too large)\n", name, src_size);
        free(src); return;
    }
    
    size_t bound = src_size * 2 + 65536;
    uint8_t* bwt_out = malloc(src_size);
    uint8_t* mtf_data = malloc(src_size);
    uint8_t* ac_out = malloc(bound);
    
    /* BWT → MTF → Adaptive AC */
    size_t primary_idx;
    size_t bwt_size = mcx_bwt_forward(bwt_out, &primary_idx, src, src_size);
    if (bwt_size == 0 || mcx_is_error(bwt_size)) {
        printf("  %-25s %8zu → BWT FAIL\n", name, src_size);
        free(src); free(bwt_out); free(mtf_data); free(ac_out); return;
    }
    
    memcpy(mtf_data, bwt_out, bwt_size);
    mcx_mtf_encode(mtf_data, bwt_size);
    
    size_t ac_size = mcx_adaptive_ac_compress(ac_out, bound, mtf_data, bwt_size);
    size_t total_new = (ac_size > 0) ? 4 + ac_size : 0;  /* +4 for primary_idx */
    
    /* Roundtrip verify */
    int rt_ok = 0;
    if (ac_size > 0) {
        uint8_t* dec1 = malloc(bwt_size + 64);
        size_t d1 = mcx_adaptive_ac_decompress(dec1, bwt_size + 64, ac_out, ac_size);
        if (d1 == bwt_size) {
            mcx_mtf_decode(dec1, d1);
            uint8_t* dec2 = malloc(src_size + 64);
            size_t d2 = mcx_bwt_inverse(dec2, primary_idx, dec1, d1);
            rt_ok = (d2 == src_size && memcmp(src, dec2, src_size) == 0);
            free(dec2);
        }
        free(dec1);
    }
    
    /* MCX L12 for comparison */
    uint8_t* l12 = malloc(bound);
    size_t l12_size = mcx_compress(l12, bound, src, src_size, 12);
    free(l12);
    
    /* MCX L3 */
    uint8_t* l3 = malloc(bound);
    size_t l3_size = mcx_compress(l3, bound, src, src_size, 3);
    free(l3);
    
    double r_new = total_new > 0 ? (double)src_size / total_new : 0;
    double r_l12 = !mcx_is_error(l12_size) ? (double)src_size / l12_size : 0;
    double r_l3 = !mcx_is_error(l3_size) ? (double)src_size / l3_size : 0;
    
    printf("  %-22s %8zu  L3=%7zu(%.2fx)  L12=%7zu(%.2fx)  BWT+AAC=%7zu(%.2fx)%s",
           name, src_size, l3_size, r_l3, l12_size, r_l12,
           total_new, r_new, rt_ok ? "✓" : "✗");
    
    if (total_new > 0 && total_new < l12_size) printf(" 🏆+%zu", l12_size - total_new);
    else if (total_new > 0) printf(" -%zu", total_new - l12_size);
    printf("\n");
    
    free(src); free(bwt_out); free(mtf_data); free(ac_out);
}

int main(void) {
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("  BWT + MTF + Adaptive AC vs MCX L12 vs L3\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n\n");
    
    bench("corpora/alice29.txt", "alice29.txt");
    bench("corpora/asyoulik.txt", "asyoulik.txt");
    bench("corpora/lcet10.txt", "lcet10.txt");
    bench("corpora/plrabn12.txt", "plrabn12.txt");
    bench("corpora/cp.html", "cp.html");
    bench("corpora/fields.c", "fields.c");
    bench("corpora/grammar.lsp", "grammar.lsp");
    bench("corpora/kennedy.xls", "kennedy.xls");
    bench("corpora/xargs.1", "xargs.1");
    bench("corpora/ptt5", "ptt5");
    bench("corpora/sum", "sum");
    
    printf("\n── Source Files ──\n");
    bench("lib/core.c", "core.c");
    bench("README.md", "README.md");
    bench("tests/test_babel.c", "test_babel.c");
    
    return 0;
}
