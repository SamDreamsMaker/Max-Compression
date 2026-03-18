/**
 * Benchmark different Babel parameters on selected files.
 * Tests hash bits (14,16,18,20) and context length (2,3,4,5).
 * Also tests higher-order prediction (frequency counting).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Parameterized Babel transform ───────────────────────────── */

static inline uint32_t babel_hash_n(const uint8_t* ctx, int ctx_len, int hash_bits) {
    uint32_t h = 0;
    const uint32_t primes[] = {2654435761u, 2246822519u, 3266489917u, 2024735687u, 1664525u};
    for (int i = 0; i < ctx_len && i < 5; i++) {
        h ^= (uint32_t)ctx[i] * primes[i];
    }
    return (h >> (32 - hash_bits)) & ((1u << hash_bits) - 1);
}

/* Simple adaptive XOR (last-seen prediction) */
static size_t babel_xor_encode(const uint8_t* src, size_t n, uint8_t* dst,
                                int ctx_len, int hash_bits) {
    size_t hash_size = 1u << hash_bits;
    uint8_t* pred = calloc(hash_size, 1);
    if (!pred) return 0;
    
    memcpy(dst, src, ctx_len);
    for (size_t i = ctx_len; i < n; i++) {
        uint32_t h = babel_hash_n(src + i - ctx_len, ctx_len, hash_bits);
        dst[i] = src[i] ^ pred[h];
        pred[h] = src[i];
    }
    free(pred);
    return n;
}

/* Frequency-based prediction (predict most frequent byte for context) */
typedef struct {
    uint16_t counts[256];
    uint8_t best;
} FreqEntry;

static size_t babel_freq_encode(const uint8_t* src, size_t n, uint8_t* dst,
                                 int ctx_len, int hash_bits) {
    size_t hash_size = 1u << hash_bits;
    FreqEntry* table = calloc(hash_size, sizeof(FreqEntry));
    if (!table) return 0;
    
    memcpy(dst, src, ctx_len);
    for (size_t i = ctx_len; i < n; i++) {
        uint32_t h = babel_hash_n(src + i - ctx_len, ctx_len, hash_bits);
        FreqEntry* e = &table[h];
        dst[i] = src[i] ^ e->best;
        
        /* Update frequency */
        e->counts[src[i]]++;
        if (src[i] != e->best && e->counts[src[i]] > e->counts[e->best]) {
            e->best = src[i];
        }
    }
    free(table);
    return n;
}

/* Compute order-0 entropy in bits per byte */
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

typedef struct { const char* path; const char* name; } TestFile;

int main(void) {
    TestFile files[] = {
        {"corpora/alice29.txt", "alice29.txt"},
        {"corpora/lcet10.txt", "lcet10.txt"},
        {"corpora/kennedy.xls", "kennedy.xls"},
        {"corpora/cp.html", "cp.html"},
        {"corpora/fields.c", "fields.c"},
        {"corpora/silesia/dickens", "sil/dickens"},
        {"corpora/silesia/xml", "sil/xml"},
        {"corpora/silesia/samba", "sil/samba"},
        {"corpora/silesia/mr", "sil/mr"},
        {"corpora/silesia/mozilla", "sil/mozilla"},
        {"corpora/silesia/nci", "sil/nci"},
        {"corpora/silesia/ooffice", "sil/ooffice"},
        {NULL, NULL}
    };
    
    int hash_bits_opts[] = {14, 16, 18, 20};
    int ctx_len_opts[] = {2, 3, 4, 5};
    
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Babel Parameter Sweep: H0 entropy of XOR residuals (bits/byte)\n");
    printf("  Lower = better (0 = perfect prediction, 8 = random)\n");
    printf("═══════════════════════════════════════════════════════════════════\n\n");
    
    for (int fi = 0; files[fi].path; fi++) {
        size_t src_size;
        uint8_t* src = read_file(files[fi].path, &src_size);
        if (!src) continue;
        
        double orig_h0 = entropy_h0(src, src_size);
        printf("%-20s (%7zu B, H0=%.3f)\n", files[fi].name, src_size, orig_h0);
        
        uint8_t* dst = malloc(src_size);
        
        /* Header: hash_bits → ctx_len variations */
        printf("  %5s ", "");
        for (int ci = 0; ci < 4; ci++) printf("  ctx=%d  ", ctx_len_opts[ci]);
        printf("  | freq ctx=3\n");
        
        for (int hi = 0; hi < 4; hi++) {
            int hb = hash_bits_opts[hi];
            printf("  h=%2d ", hb);
            
            for (int ci = 0; ci < 4; ci++) {
                int cl = ctx_len_opts[ci];
                babel_xor_encode(src, src_size, dst, cl, hb);
                double h0 = entropy_h0(dst, src_size);
                printf("  %5.3f  ", h0);
            }
            
            /* Also test frequency-based with ctx=3 */
            babel_freq_encode(src, src_size, dst, 3, hb);
            double h0_freq = entropy_h0(dst, src_size);
            printf("  | %5.3f", h0_freq);
            
            printf("\n");
        }
        printf("\n");
        
        free(dst);
        free(src);
    }
    
    return 0;
}
