/**
 * Exp 005b: Clean approximate matching measurement
 * Compares total encoded bits: exact-only vs exact+approx
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define WINDOW_SIZE (1 << 16)
#define HASH_SIZE   (1 << 16)
#define HC_DEPTH    32
#define MIN_MATCH   4
#define MAX_MATCH   273
#define MAX_EDITS   3
#define NICE_MATCH  64

typedef struct {
    size_t length, distance, n_edits;
    uint16_t epos[MAX_EDITS];
    uint8_t eval[MAX_EDITS];
} match_t;

typedef struct {
    uint32_t head[HASH_SIZE];
    uint32_t chain[WINDOW_SIZE];
} hc_t;

static uint32_t h4(const uint8_t* p) {
    return (((uint32_t)p[0]<<11)^((uint32_t)p[1]<<5)^((uint32_t)p[2]<<1)^(p[3]>>2))&(HASH_SIZE-1);
}

static void hc_init(hc_t* hc) { memset(hc->head, 0xFF, sizeof(hc->head)); }

static void hc_insert(hc_t* hc, const uint8_t* d, size_t pos) {
    uint32_t h = h4(d + pos);
    uint32_t w = pos & (WINDOW_SIZE-1);
    hc->chain[w] = hc->head[h];
    hc->head[h] = w;
}

static match_t find_match(const uint8_t* d, size_t size, size_t pos, hc_t* hc, int allow_approx) {
    match_t best = {0};
    if (pos + MIN_MATCH > size) return best;
    
    uint32_t cur = hc->head[h4(d + pos)];
    for (int dep = 0; dep < HC_DEPTH && cur != 0xFFFFFFFF; dep++) {
        size_t ref = (pos & ~(size_t)(WINDOW_SIZE-1)) | cur;
        if (ref >= pos) { if (pos >= WINDOW_SIZE) ref -= WINDOW_SIZE; else goto nxt; }
        if (pos - ref > WINDOW_SIZE) goto nxt;
        
        size_t max_len = size - pos;
        if (max_len > MAX_MATCH) max_len = MAX_MATCH;
        
        /* Exact match */
        size_t elen = 0;
        while (elen < max_len && d[ref+elen] == d[pos+elen]) elen++;
        
        if (elen >= MIN_MATCH && elen > best.length) {
            best.length = elen; best.distance = pos - ref; best.n_edits = 0;
            if (elen >= NICE_MATCH) break;
        }
        
        /* Approximate match: extend past mismatches */
        if (allow_approx && elen >= 3) { /* At least 3 exact bytes at start */
            size_t alen = 0, edits = 0;
            uint16_t ep[MAX_EDITS]; uint8_t ev[MAX_EDITS];
            
            while (alen < max_len) {
                if (d[ref+alen] != d[pos+alen]) {
                    if (edits >= MAX_EDITS) break;
                    ep[edits] = (uint16_t)alen;
                    ev[edits] = d[pos+alen];
                    edits++;
                }
                alen++;
            }
            
            if (edits > 0 && alen >= MIN_MATCH + edits * 2) {
                /* Score: net bits saved vs treating extra bytes as literals */
                /* Approx match cost: 24 (base) + edits * 16 (pos+val) bits */
                /* vs exact(elen) + literals(alen-elen) cost: 24 + (alen-elen)*8 */
                double approx_cost = 24.0 + edits * 16.0;
                double alt_cost = (elen >= MIN_MATCH ? 24.0 : elen * 8.0) + 
                                  (alen - (elen >= MIN_MATCH ? elen : 0)) * 8.0;
                
                if (approx_cost < alt_cost && alen > best.length) {
                    best.length = alen;
                    best.distance = pos - ref;
                    best.n_edits = edits;
                    memcpy(best.epos, ep, edits * sizeof(uint16_t));
                    memcpy(best.eval, ev, edits * sizeof(uint8_t));
                }
            }
        }
    nxt:
        cur = hc->chain[cur];
    }
    return best;
}

typedef struct {
    double total_bits;
    size_t n_exact, exact_bytes, n_approx, approx_bytes, n_lit, total_edits;
} result_t;

static result_t simulate(const uint8_t* d, size_t size, int use_approx) {
    hc_t* hc = calloc(1, sizeof(hc_t));
    hc_init(hc);
    result_t r = {0};
    
    for (size_t pos = 0; pos < size; ) {
        if (pos + 4 <= size) hc_insert(hc, d, pos);
        
        match_t m = find_match(d, size, pos, hc, use_approx);
        
        if (m.length >= MIN_MATCH) {
            for (size_t j = 1; j < m.length && pos+j+4 <= size; j++)
                hc_insert(hc, d, pos+j);
            
            if (m.n_edits == 0) {
                r.n_exact++; r.exact_bytes += m.length;
                r.total_bits += 24.0; /* match token */
            } else {
                r.n_approx++; r.approx_bytes += m.length;
                r.total_edits += m.n_edits;
                r.total_bits += 24.0 + m.n_edits * 16.0; /* match + patches */
            }
            pos += m.length;
        } else {
            r.n_lit++; r.total_bits += 8.0;
            pos++;
        }
    }
    free(hc);
    return r;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* d = malloc(size);
    if (fread(d, 1, size, f) != size) { fclose(f); free(d); return 1; }
    fclose(f);
    
    printf("=== Approximate Matching: Real LZ Comparison ===\n");
    printf("File: %s (%zu bytes)\n\n", argv[1], size);
    
    result_t exact = simulate(d, size, 0);
    result_t approx = simulate(d, size, 1);
    
    double exact_bytes = exact.total_bits / 8.0;
    double approx_bytes_out = approx.total_bits / 8.0;
    
    printf("EXACT ONLY:\n");
    printf("  Matches: %zu (%zu bytes, %.1f%%)\n", exact.n_exact, exact.exact_bytes, 100.0*exact.exact_bytes/size);
    printf("  Literals: %zu (%.1f%%)\n", exact.n_lit, 100.0*exact.n_lit/size);
    printf("  Encoded: %.0f bytes (%.3fx)\n\n", exact_bytes, size/exact_bytes);
    
    printf("WITH APPROX:\n");
    printf("  Exact matches:  %zu (%zu bytes, %.1f%%)\n", approx.n_exact, approx.exact_bytes, 100.0*approx.exact_bytes/size);
    printf("  Approx matches: %zu (%zu bytes, %.1f%%, avg %.1f edits)\n",
           approx.n_approx, approx.approx_bytes, 100.0*approx.approx_bytes/size,
           approx.n_approx > 0 ? (double)approx.total_edits/approx.n_approx : 0);
    printf("  Literals: %zu (%.1f%%)\n", approx.n_lit, 100.0*approx.n_lit/size);
    printf("  Encoded: %.0f bytes (%.3fx)\n\n", approx_bytes_out, size/approx_bytes_out);
    
    double delta = exact_bytes - approx_bytes_out;
    printf("=== VERDICT ===\n");
    if (delta > 0) {
        printf("🎯 APPROX SAVES: %.0f bytes (%.2f%%)\n", delta, 100.0*delta/exact_bytes);
    } else {
        printf("❌ APPROX LOSES: %.0f bytes (%.2f%%)\n", -delta, -100.0*delta/exact_bytes);
    }
    printf("Coverage: %.1f%% matched (was %.1f%%)\n",
           100.0*(approx.exact_bytes + approx.approx_bytes)/size,
           100.0*exact.exact_bytes/size);
    
    free(d);
    return 0;
}
