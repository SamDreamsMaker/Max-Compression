/**
 * Analyze CM-rANS output structure to understand overhead
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxcomp/maxcomp.h>

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    *out_size = fread(buf, 1, sz, f);
    fclose(f); return buf;
}

static void analyze(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src) return;
    
    size_t bound = mcx_compress_bound(src_size) + src_size;
    uint8_t* comp = malloc(bound);
    
    /* L12 = BWT + CM-rANS */
    size_t l12_size = mcx_compress(comp, bound, src, src_size, 12);
    
    /* L3 = LZ77 */
    size_t l3_size = mcx_compress(comp, bound, src, src_size, 3);
    
    /* Also try just rANS (order-0) via L20 Babel path but on raw data... 
     * Actually we can't control that from here. Let's just compute theoretical. */
    
    /* Compute order-0 entropy */
    size_t freq[256] = {0};
    for (size_t i = 0; i < src_size; i++) freq[src[i]]++;
    double h0 = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / src_size;
        h0 -= p * __builtin_log2(p);
    }
    
    /* Compute order-1 entropy */
    size_t freq2[256][256];
    memset(freq2, 0, sizeof(freq2));
    for (size_t i = 1; i < src_size; i++) freq2[src[i-1]][src[i]]++;
    double h1 = 0;
    for (int c = 0; c < 256; c++) {
        size_t ctx_total = 0;
        for (int s = 0; s < 256; s++) ctx_total += freq2[c][s];
        if (ctx_total == 0) continue;
        double ctx_weight = (double)ctx_total / (src_size - 1);
        double ctx_h = 0;
        for (int s = 0; s < 256; s++) {
            if (freq2[c][s] == 0) continue;
            double p = (double)freq2[c][s] / ctx_total;
            ctx_h -= p * __builtin_log2(p);
        }
        h1 += ctx_weight * ctx_h;
    }
    
    double h0_min = src_size * h0 / 8.0;
    double h1_min = src_size * h1 / 8.0;
    double overhead = l12_size - h1_min;
    double overhead_pct = overhead / l12_size * 100;
    
    printf("  %-25s %8zu  L3=%7zu(%.2fx)  L12=%7zu(%.2fx)  H0=%.2f H1=%.2f  H0min=%7.0f  H1min=%7.0f  overhead=%.0f(%.0f%%)\n",
           name, src_size, l3_size, (double)src_size/l3_size,
           l12_size, (double)src_size/l12_size,
           h0, h1, h0_min, h1_min, overhead, overhead_pct);
    
    free(src); free(comp);
}

int main(void) {
    printf("═══════════════════════════════════════════════════════════════════════════════════════\n");
    printf("  CM-rANS Analysis: actual vs theoretical compression\n");
    printf("  H1min = theoretical minimum with perfect order-1 coding (no overhead)\n");
    printf("  overhead = L12 actual - H1min (context tables + coding inefficiency)\n");
    printf("═══════════════════════════════════════════════════════════════════════════════════════\n\n");
    
    analyze("corpora/alice29.txt", "alice29.txt");
    analyze("corpora/asyoulik.txt", "asyoulik.txt");
    analyze("corpora/lcet10.txt", "lcet10.txt");
    analyze("corpora/plrabn12.txt", "plrabn12.txt");
    analyze("corpora/cp.html", "cp.html");
    analyze("corpora/fields.c", "fields.c");
    analyze("corpora/kennedy.xls", "kennedy.xls");
    analyze("corpora/silesia/dickens", "sil/dickens");
    analyze("corpora/silesia/xml", "sil/xml");
    analyze("corpora/silesia/samba", "sil/samba");
    analyze("corpora/silesia/webster", "sil/webster");
    
    return 0;
}
