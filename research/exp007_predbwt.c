/**
 * Experiment 007: Prediction Residual + BWT
 * 
 * BREAKTHROUGH HYPOTHESIS: Combine contextual prediction with BWT.
 * 
 * Current MCX: data → BWT → MTF → RLE2 → rANS
 * Proposed:    data → PREDICT → RESIDUAL → BWT → MTF → RLE2 → rANS
 * 
 * The prediction step replaces each byte with (byte XOR predicted_byte).
 * When prediction is correct, residual = 0.
 * A stream with many zeros is IDEAL for BWT+MTF+RLE2.
 * 
 * The predictor is built adaptively (two-pass or single-pass):
 * - Order-1 context: freq[prev_byte][cur_byte]
 * - Prediction = most frequent byte after prev_byte
 * - Decoder can rebuild the same predictor from the decoded residual
 * 
 * WHY THIS IS NEW: 
 * - PAQ uses prediction + arithmetic coding (slow)
 * - BWT uses sorting + MTF + entropy coding (no prediction)
 * - NOBODY combines prediction + BWT
 * 
 * WHY IT COULD WORK:
 * - BWT output of all-zeros = one giant run → near-zero RLE2 output
 * - Even 50% correct predictions would halve the effective entropy
 * - The predictor costs ZERO extra storage (rebuilt from data)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ── Predictors ─────────────────────────────────────────────────── */

/* Order-1: predict based on previous byte */
static void predict_order1(const uint8_t* in, uint8_t* residual, size_t size) {
    uint16_t freq[256][256] = {{0}};
    uint8_t best[256];
    memset(best, 0, 256);
    
    for (size_t i = 0; i < size; i++) {
        uint8_t ctx = (i > 0) ? in[i-1] : 0;
        uint8_t pred = best[ctx];
        residual[i] = in[i] ^ pred;
        
        /* Update model */
        freq[ctx][in[i]]++;
        if (freq[ctx][in[i]] > freq[ctx][best[ctx]]) {
            best[ctx] = in[i];
        }
        /* Prevent overflow */
        if (freq[ctx][in[i]] > 60000) {
            for (int j = 0; j < 256; j++) freq[ctx][j] >>= 1;
        }
    }
}

/* Order-2: predict based on two previous bytes */
static void predict_order2(const uint8_t* in, uint8_t* residual, size_t size) {
    /* Use hash table for order-2 (65536 contexts) */
    uint8_t* best = calloc(65536, 1);
    uint16_t* freq = calloc(65536, sizeof(uint16_t)); /* freq of best prediction */
    
    for (size_t i = 0; i < size; i++) {
        uint16_t ctx = 0;
        if (i >= 2) ctx = ((uint16_t)in[i-2] << 8) | in[i-1];
        else if (i >= 1) ctx = in[i-1];
        
        residual[i] = in[i] ^ best[ctx];
        
        /* Simple adaptation: if actual byte seen more often, update prediction */
        /* This is a simplified version — a real impl would track full freq tables */
        if (in[i] == best[ctx]) {
            freq[ctx]++;
        } else {
            if (freq[ctx] > 0) freq[ctx]--;
            else best[ctx] = in[i]; /* Switch prediction */
        }
    }
    
    free(best);
    free(freq);
}

/* Order-1 with full adaptation */
static void predict_order1_full(const uint8_t* in, uint8_t* residual, size_t size) {
    /* Track full frequency table, predict most frequent */
    uint32_t freq[256][256];
    memset(freq, 0, sizeof(freq));
    
    for (size_t i = 0; i < size; i++) {
        uint8_t ctx = (i > 0) ? in[i-1] : 0;
        
        /* Find best prediction */
        uint8_t pred = 0;
        uint32_t max_f = 0;
        for (int j = 0; j < 256; j++) {
            if (freq[ctx][j] > max_f) {
                max_f = freq[ctx][j];
                pred = j;
            }
        }
        
        residual[i] = in[i] ^ pred;
        freq[ctx][in[i]]++;
    }
}

/* ── BWT simulation (simplified — just measure entropy) ─── */

/* Simple BWT for small blocks */
static const uint8_t* g_data;
static size_t g_size;
static int suffix_cmp(const void* a, const void* b) {
    size_t ia = *(const size_t*)a, ib = *(const size_t*)b;
    for (size_t k = 0; k < g_size; k++) {
        uint8_t ca = g_data[(ia+k) % g_size], cb = g_data[(ib+k) % g_size];
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    return 0;
}

static void do_bwt(const uint8_t* in, uint8_t* out, size_t size) {
    size_t* sa = malloc(size * sizeof(size_t));
    for (size_t i = 0; i < size; i++) sa[i] = i;
    g_data = in; g_size = size;
    qsort(sa, size, sizeof(size_t), suffix_cmp);
    for (size_t i = 0; i < size; i++)
        out[i] = in[(sa[i] + size - 1) % size];
    free(sa);
}

/* MTF transform */
static void do_mtf(uint8_t* data, size_t size) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    for (size_t i = 0; i < size; i++) {
        uint8_t val = data[i];
        int pos = 0;
        while (table[pos] != val) pos++;
        data[i] = pos;
        /* Move to front */
        memmove(table + 1, table, pos);
        table[0] = val;
    }
}

static double entropy(const uint8_t* data, size_t size) {
    uint32_t counts[256] = {0};
    for (size_t i = 0; i < size; i++) counts[data[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / size;
            h -= p * log2(p);
        }
    }
    return h;
}

static size_t count_zeros(const uint8_t* data, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; i++) if (data[i] == 0) n++;
    return n;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file> [max_bwt_size]\n", argv[0]); return 1; }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t full_size = ftell(f); fseek(f, 0, SEEK_SET);
    
    size_t max_bwt = (argc >= 3) ? (size_t)atoi(argv[2]) : 16384;
    size_t size = full_size < max_bwt ? full_size : max_bwt;
    
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Exp 007: Prediction Residual + BWT ===\n");
    printf("File: %s (%zu bytes, using %zu for BWT)\n\n", argv[1], full_size, size);
    
    uint8_t* residual = malloc(size);
    uint8_t* bwt_out = malloc(size);
    uint8_t* mtf_out = malloc(size);
    
    /* ── Baseline: raw → BWT → MTF ─── */
    printf("=== Baseline: data → BWT → MTF ===\n");
    do_bwt(data, bwt_out, size);
    memcpy(mtf_out, bwt_out, size);
    do_mtf(mtf_out, size);
    double base_entropy = entropy(mtf_out, size);
    size_t base_zeros = count_zeros(mtf_out, size);
    printf("  BWT+MTF entropy: %.4f bpb\n", base_entropy);
    printf("  MTF zeros: %zu (%.1f%%)\n", base_zeros, 100.0*base_zeros/size);
    printf("  Estimated: %.0f bytes (%.3fx)\n\n", base_entropy*size/8, 8.0/base_entropy);
    
    /* ── Test each predictor ─── */
    struct {
        const char* name;
        void (*fn)(const uint8_t*, uint8_t*, size_t);
    } predictors[] = {
        {"Order-1 (simple)", predict_order1},
        {"Order-1 (full freq)", predict_order1_full},
        {"Order-2 (hash)", predict_order2},
    };
    
    for (int p = 0; p < 3; p++) {
        printf("=== %s → BWT → MTF ===\n", predictors[p].name);
        
        /* Generate residual */
        predictors[p].fn(data, residual, size);
        
        /* Analyze residual before BWT */
        size_t res_zeros = count_zeros(residual, size);
        double res_entropy = entropy(residual, size);
        printf("  Residual: entropy=%.4f bpb, zeros=%zu (%.1f%%)\n",
               res_entropy, res_zeros, 100.0*res_zeros/size);
        
        /* BWT the residual */
        do_bwt(residual, bwt_out, size);
        
        /* MTF the BWT output */
        memcpy(mtf_out, bwt_out, size);
        do_mtf(mtf_out, size);
        
        double pred_entropy = entropy(mtf_out, size);
        size_t pred_zeros = count_zeros(mtf_out, size);
        
        printf("  BWT+MTF entropy: %.4f bpb (baseline: %.4f)\n", pred_entropy, base_entropy);
        printf("  MTF zeros: %zu (%.1f%%) (baseline: %zu, %.1f%%)\n",
               pred_zeros, 100.0*pred_zeros/size, base_zeros, 100.0*base_zeros/size);
        printf("  Estimated: %.0f bytes (%.3fx)\n", pred_entropy*size/8, 8.0/pred_entropy);
        
        double delta = base_entropy - pred_entropy;
        if (delta > 0) {
            printf("  🎯 BETTER: -%.4f bpb (%.1f%% improvement)\n", delta, 100.0*delta/base_entropy);
            printf("     Saves ~%.0f bytes on %zu\n", delta*size/8, size);
        } else {
            printf("  ❌ WORSE: +%.4f bpb\n", -delta);
        }
        printf("\n");
    }
    
    free(data);
    free(residual);
    free(bwt_out);
    free(mtf_out);
    return 0;
}
