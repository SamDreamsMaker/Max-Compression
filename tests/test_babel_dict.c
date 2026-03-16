/**
 * Test and benchmark Babel Dictionary Transform
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../lib/babel/babel_dict.h"

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    *out_size = fread(buf, 1, sz, f);
    fclose(f);
    return buf;
}

static double entropy_h0(const uint8_t* data, size_t n) {
    size_t freq[256] = {0};
    for (size_t i = 0; i < n; i++) freq[data[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / n;
        h -= p * __builtin_log2(p);
    }
    return h;
}

static int test_file(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src) { printf("  %-30s SKIP (can't read)\n", name); return 0; }
    if (src_size < 10) { free(src); return 0; }
    
    size_t bound = mcx_babel_dict_bound(src_size);
    uint8_t* enc = malloc(bound);
    uint8_t* dec = malloc(src_size + 1024);
    
    /* Forward */
    size_t enc_size = mcx_babel_dict_forward(enc, bound, src, src_size);
    
    if (enc_size == 0) {
        printf("  %-30s %8zu → no dict (skipped)\n", name, src_size);
        free(src); free(enc); free(dec);
        return 1;
    }
    
    /* Inverse */
    size_t dec_size = mcx_babel_dict_inverse(dec, src_size + 1024, enc, enc_size);
    
    int roundtrip = (dec_size == src_size && memcmp(src, dec, src_size) == 0);
    
    double orig_h0 = entropy_h0(src, src_size);
    double enc_h0 = entropy_h0(enc, enc_size);
    double ratio = (double)src_size / enc_size;
    
    /* Theoretical compression: enc_size * enc_h0/8 */
    double theoretical_compressed = enc_size * enc_h0 / 8.0;
    double theoretical_ratio = src_size / theoretical_compressed;
    
    printf("  %-30s %8zu → %8zu (%.2fx, H0: %.2f→%.2f, theo: %.2fx) %s\n",
           name, src_size, enc_size, ratio, orig_h0, enc_h0, theoretical_ratio,
           roundtrip ? "✓" : "✗ FAIL");
    
    free(src); free(enc); free(dec);
    return roundtrip ? 1 : 0;
}

int main(void) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Babel Dictionary Transform — Test & Benchmark\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    int pass = 0, fail = 0;
    
    /* Canterbury */
    printf("── Canterbury Corpus ──\n");
    const char* cant_files[] = {
        "corpora/alice29.txt", "corpora/asyoulik.txt", "corpora/cp.html",
        "corpora/fields.c", "corpora/grammar.lsp", "corpora/kennedy.xls",
        "corpora/lcet10.txt", "corpora/plrabn12.txt", "corpora/ptt5",
        "corpora/sum", "corpora/xargs.1", NULL
    };
    for (int i = 0; cant_files[i]; i++) {
        const char* name = strrchr(cant_files[i], '/') + 1;
        if (test_file(cant_files[i], name)) pass++; else fail++;
    }
    
    /* Silesia (text-heavy ones) */
    printf("\n── Silesia Corpus ──\n");
    const char* sil_files[] = {
        "corpora/silesia/dickens", "corpora/silesia/reymont",
        "corpora/silesia/samba", "corpora/silesia/webster",
        "corpora/silesia/xml", "corpora/silesia/nci",
        "corpora/silesia/mozilla", "corpora/silesia/ooffice",
        "corpora/silesia/mr", "corpora/silesia/osdb", NULL
    };
    for (int i = 0; sil_files[i]; i++) {
        const char* name = strrchr(sil_files[i], '/') + 1;
        if (test_file(sil_files[i], name)) pass++; else fail++;
    }
    
    /* Source files */
    printf("\n── Source Files ──\n");
    test_file("lib/core.c", "core.c") ? pass++ : fail++;
    test_file("README.md", "README.md") ? pass++ : fail++;
    
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("  Results: %d pass, %d fail\n", pass, fail);
    printf("═══════════════════════════════════════════════════════════════\n");
    
    return fail > 0 ? 1 : 0;
}
