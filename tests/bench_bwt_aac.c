/**
 * Benchmark: BWT + MTF + Adaptive AC vs MCX L12 (BWT + MTF + CM-rANS)
 * The hypothesis: eliminating CM-rANS table overhead with adaptive coding
 * will improve compression, especially on smaller files.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxcomp/maxcomp.h>

/* Internal preprocessing functions */
extern size_t mcx_bwt_forward(uint8_t* dst, size_t* primary_idx,
                               const uint8_t* src, size_t size);
extern void mcx_mtf_encode(uint8_t* data, size_t size);
extern void mcx_mtf_decode(uint8_t* data, size_t size);
extern size_t mcx_bwt_inverse(uint8_t* dst, size_t primary_idx,
                               const uint8_t* src, size_t size);
extern size_t mcx_rle_encode(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size);
extern size_t mcx_rle_decode(uint8_t* dst, size_t dst_cap,
                              const uint8_t* src, size_t src_size);

/* Adaptive AC */
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
    if (!src) return;
    
    size_t bound = src_size * 2 + 65536;
    
    /* Pipeline: BWT → MTF → Adaptive AC */
    uint8_t* bwt_out = malloc(src_size);
    uint8_t* mtf_out = malloc(src_size);
    uint8_t* ac_out = malloc(bound);
    
    if (!bwt_out || !mtf_out || !ac_out) {
        free(src); free(bwt_out); free(mtf_out); free(ac_out);
        return;
    }
    
    /* BWT */
    size_t primary_idx;
    size_t bwt_size = mcx_bwt_forward(bwt_out, &primary_idx, src, src_size);
    
    if (bwt_size == 0 || mcx_is_error(bwt_size)) {
        printf("  %-25s %8zu → BWT FAILED\n", name, src_size);
        free(src); free(bwt_out); free(mtf_out); free(ac_out);
        return;
    }
    
    /* MTF */
    memcpy(mtf_out, bwt_out, bwt_size);
    mcx_mtf_encode(mtf_out, bwt_size);
    
    /* Adaptive AC on MTF output */
    size_t ac_size = mcx_adaptive_ac_compress(ac_out, bound, mtf_out, bwt_size);
    
    /* Total compressed = 4 (primary_idx) + ac_size */
    size_t total_bwt_aac = 4 + ac_size;
    
    /* Verify roundtrip: AC decode → MTF decode → BWT inverse */
    uint8_t* ac_dec = malloc(bwt_size + 64);
    size_t ac_dec_size = mcx_adaptive_ac_decompress(ac_dec, bwt_size + 64, ac_out, ac_size);
    
    int roundtrip_ok = 0;
    if (ac_dec_size == bwt_size) {
        mcx_mtf_decode(ac_dec, ac_dec_size);
        uint8_t* final_dec = malloc(src_size + 64);
        size_t final_size = mcx_bwt_inverse(final_dec, primary_idx, ac_dec, ac_dec_size);
        roundtrip_ok = (final_size == src_size && memcmp(src, final_dec, src_size) == 0);
        free(final_dec);
    }
    free(ac_dec);
    
    /* Compare with MCX L12 */
    uint8_t* l12_out = malloc(bound);
    size_t l12_size = mcx_compress(l12_out, bound, src, src_size, 12);
    free(l12_out);
    
    /* Also compare with RLE + Adaptive AC */
    uint8_t* rle_out = malloc(bound);
    size_t rle_size = mcx_rle_encode(rle_out, bound, mtf_out, bwt_size);
    size_t ac_rle_size = 0;
    if (rle_size > 0 && !mcx_is_error(rle_size)) {
        uint8_t* ac_rle_out = malloc(bound);
        ac_rle_size = mcx_adaptive_ac_compress(ac_rle_out, bound, rle_out, rle_size);
        if (ac_rle_size > 0) ac_rle_size += 4; /* primary_idx overhead */
        free(ac_rle_out);
    }
    free(rle_out);
    
    /* Also: Adaptive AC directly on raw data (no BWT) */
    uint8_t* raw_ac = malloc(bound);
    size_t raw_ac_size = mcx_adaptive_ac_compress(raw_ac, bound, src, src_size);
    free(raw_ac);
    
    char rt_str[4];
    snprintf(rt_str, sizeof(rt_str), "%s", roundtrip_ok ? "✓" : "✗");
    
    printf("  %-22s %8zu  L12=%7zu(%.2fx)  BWT+MTF+AAC=%7zu(%.2fx)%s  BWT+MTF+RLE+AAC=%7zu(%.2fx)  RAW+AAC=%7zu(%.2fx)",
           name, src_size,
           l12_size, (double)src_size / l12_size,
           total_bwt_aac, ac_size > 0 ? (double)src_size / total_bwt_aac : 0.0, rt_str,
           ac_rle_size, ac_rle_size > 0 ? (double)src_size / ac_rle_size : 0.0,
           raw_ac_size, raw_ac_size > 0 ? (double)src_size / raw_ac_size : 0.0);
    
    if (total_bwt_aac < l12_size) printf(" 🏆");
    printf("\n");
    
    free(src); free(bwt_out); free(mtf_out); free(ac_out);
}

int main(void) {
    printf("═════════════════════════════════════════════════════════════════════════════════════════════════════════\n");
    printf("  BWT + MTF + Adaptive AC vs MCX L12 (BWT + CM-rANS)\n");
    printf("═════════════════════════════════════════════════════════════════════════════════════════════════════════\n\n");
    
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
    
    printf("\n── Silesia ──\n");
    bench("corpora/silesia/dickens", "sil/dickens");
    bench("corpora/silesia/xml", "sil/xml");
    bench("corpora/silesia/samba", "sil/samba");
    bench("corpora/silesia/webster", "sil/webster");
    bench("corpora/silesia/nci", "sil/nci");
    bench("corpora/silesia/ooffice", "sil/ooffice");
    bench("corpora/silesia/mr", "sil/mr");
    bench("corpora/silesia/mozilla", "sil/mozilla");
    bench("corpora/silesia/reymont", "sil/reymont");
    
    printf("\n── Source ──\n");
    bench("lib/core.c", "core.c");
    bench("README.md", "README.md");
    
    return 0;
}
