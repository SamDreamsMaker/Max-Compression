/**
 * Experiment 003: Bit-Level BWT
 * 
 * HYPOTHESIS: Byte-level BWT treats each byte as an atomic symbol.
 * But bytes have internal bit-level structure (e.g., ASCII bit7=0).
 * A bit-level BWT would sort by BIT contexts, potentially finding
 * correlations invisible to byte-level BWT.
 * 
 * APPROACH:
 * 1. Expand N bytes into 8N bits
 * 2. Apply BWT on the bit stream (alphabet = {0, 1})
 * 3. The BWT output is a binary string with very long runs
 * 4. Encode runs with simple RLE or arithmetic coding
 * 
 * ALTERNATIVE: Bit-plane decomposition
 * Split bytes into 8 bit-planes, apply byte-BWT on each plane separately.
 * Each plane is a binary stream that may have different compressibility.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ── Bit operations ─────────────────────────────────────────────── */

static void bytes_to_bits(const uint8_t* in, size_t n_bytes, uint8_t* bits) {
    for (size_t i = 0; i < n_bytes; i++) {
        for (int b = 7; b >= 0; b--) {
            bits[i * 8 + (7 - b)] = (in[i] >> b) & 1;
        }
    }
}

/* ── Bit-Plane Decomposition ────────────────────────────────────── */

/* decompose_bitplanes removed — done inline in main */

/* ── Simple suffix array for small inputs ───────────────────────── */

static const uint8_t* g_data;
static size_t g_size;

static int suffix_cmp(const void* a, const void* b) {
    size_t ia = *(const size_t*)a;
    size_t ib = *(const size_t*)b;
    size_t len = g_size;
    
    for (size_t k = 0; k < len; k++) {
        size_t pa = (ia + k) % len;
        size_t pb = (ib + k) % len;
        if (g_data[pa] < g_data[pb]) return -1;
        if (g_data[pa] > g_data[pb]) return 1;
    }
    return 0;
}

static void simple_bwt(const uint8_t* in, size_t size, uint8_t* out, size_t* primary_idx) {
    size_t* sa = malloc(size * sizeof(size_t));
    for (size_t i = 0; i < size; i++) sa[i] = i;
    
    g_data = in;
    g_size = size;
    qsort(sa, size, sizeof(size_t), suffix_cmp);
    
    for (size_t i = 0; i < size; i++) {
        out[i] = in[(sa[i] + size - 1) % size];
        if (sa[i] == 0) *primary_idx = i;
    }
    free(sa);
}

/* ── Entropy calculation ────────────────────────────────────────── */

static double calc_entropy(const uint8_t* data, size_t size) {
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

static double calc_binary_entropy(const uint8_t* bits, size_t n_bits) {
    size_t ones = 0;
    for (size_t i = 0; i < n_bits; i++) ones += bits[i];
    if (ones == 0 || ones == n_bits) return 0;
    double p = (double)ones / n_bits;
    return -(p * log2(p) + (1-p) * log2(1-p));
}

/* Count runs in binary data */
static size_t count_runs(const uint8_t* data, size_t size) {
    if (size < 2) return size;
    size_t runs = 1;
    for (size_t i = 1; i < size; i++) {
        if (data[i] != data[i-1]) runs++;
    }
    return runs;
}

/* Estimate RLE-encoded size for binary data */
static double estimate_rle_bits(const uint8_t* data, size_t size) {
    if (size < 2) return size;
    size_t runs = count_runs(data, size);
    /* Each run needs: 1 bit for value + log2(avg_run_length) bits for length */
    double avg_run = (double)size / runs;
    double bits_per_run = 1.0 + (avg_run > 1 ? log2(avg_run) : 0) + 1.0; /* +1 for Elias gamma overhead */
    return runs * bits_per_run;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file> [max_bytes_for_bit_bwt]\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(file_size);
    if (fread(data, 1, file_size, f) != file_size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    /* Limit for bit-BWT (O(n²) sort is slow) */
    size_t bwt_limit = (argc >= 3) ? (size_t)atoi(argv[2]) : 8192;
    size_t work_size = file_size < bwt_limit ? file_size : bwt_limit;
    
    printf("=== Bit-Level BWT Experiment ===\n");
    printf("Input: %s (%zu bytes, using first %zu)\n", argv[1], file_size, work_size);
    printf("Byte entropy: %.4f bpb\n\n", calc_entropy(data, work_size));
    
    /* ── Approach A: Bit-Plane Decomposition ─── */
    printf("=== Approach A: Bit-Plane Decomposition ===\n");
    
    size_t plane_bytes = (work_size + 7) / 8;
    uint8_t (*planes)[1048576] = malloc(8 * 1048576); /* max 1MB per plane */
    if (!planes) { printf("OOM\n"); free(data); return 1; }
    
    /* Manual bit-plane extraction */
    memset(planes, 0, 8 * plane_bytes);
    for (size_t i = 0; i < work_size; i++) {
        size_t byte_idx = i / 8;
        int bit_pos = 7 - (i % 8);
        for (int plane = 0; plane < 8; plane++) {
            int bit = (data[i] >> plane) & 1;
            planes[plane][byte_idx] |= (bit << bit_pos);
        }
    }
    
    printf("Per-plane byte entropy (after packing bits into bytes):\n");
    double total_plane_entropy_bits = 0;
    for (int p = 0; p < 8; p++) {
        double h = calc_entropy(planes[p], plane_bytes);
        printf("  Plane %d (bit %d): %.4f bpb, %zu distinct\n", 
               p, p, h, (size_t)0 /* placeholder */);
        total_plane_entropy_bits += h * plane_bytes;
    }
    printf("Total entropy across planes: %.0f bytes (vs %.0f original)\n",
           total_plane_entropy_bits / 8.0, calc_entropy(data, work_size) * work_size / 8.0);
    
    /* BWT each plane separately */
    printf("\nBWT per plane:\n");
    double total_bwt_plane_bits = 0;
    for (int p = 0; p < 8; p++) {
        uint8_t* bwt_out = malloc(plane_bytes);
        size_t pidx;
        simple_bwt(planes[p], plane_bytes, bwt_out, &pidx);
        double h_before = calc_entropy(planes[p], plane_bytes);
        double h_after = calc_entropy(bwt_out, plane_bytes);
        size_t runs_before = count_runs(planes[p], plane_bytes);
        size_t runs_after = count_runs(bwt_out, plane_bytes);
        printf("  Plane %d: entropy %.4f→%.4f bpb, runs %zu→%zu (%.1f%% fewer)\n",
               p, h_before, h_after, runs_before, runs_after,
               100.0 * (1.0 - (double)runs_after / runs_before));
        total_bwt_plane_bits += h_after * plane_bytes;
        free(bwt_out);
    }
    printf("Total BWT-plane entropy: %.0f bytes\n", total_bwt_plane_bits / 8.0);
    
    /* ── Approach B: Direct bit-BWT ─── */
    printf("\n=== Approach B: Direct Bit-Level BWT ===\n");
    
    size_t n_bits = work_size * 8;
    printf("Bit stream: %zu bits\n", n_bits);
    
    /* Only do this for small inputs (O(n²) sort) */
    if (n_bits <= 100000) {
        uint8_t* bits = malloc(n_bits);
        uint8_t* bwt_bits = malloc(n_bits);
        bytes_to_bits(data, work_size, bits);
        
        size_t pidx;
        printf("Running bit-BWT on %zu bits (this may take a while)...\n", n_bits);
        simple_bwt(bits, n_bits, bwt_bits, &pidx);
        
        double h_before = calc_binary_entropy(bits, n_bits);
        double h_after = calc_binary_entropy(bwt_bits, n_bits);
        size_t runs_before = count_runs(bits, n_bits);
        size_t runs_after = count_runs(bwt_bits, n_bits);
        
        printf("Binary entropy: %.6f → %.6f bpb\n", h_before, h_after);
        printf("Runs: %zu → %zu (%.1f%% fewer, avg run: %.1f → %.1f bits)\n",
               runs_before, runs_after,
               100.0 * (1.0 - (double)runs_after / runs_before),
               (double)n_bits / runs_before, (double)n_bits / runs_after);
        
        /* Estimate compressed size */
        double est_rle = estimate_rle_bits(bwt_bits, n_bits) / 8.0;
        printf("Estimated RLE size: %.0f bytes (%.3fx compression)\n",
               est_rle, work_size / est_rle);
        
        free(bits);
        free(bwt_bits);
    } else {
        printf("Input too large for O(n²) bit-BWT. Use max_bytes <= 12500.\n");
    }
    
    /* ── Compare with byte-BWT ─── */
    printf("\n=== Reference: Byte-Level BWT ===\n");
    {
        uint8_t* bwt_out = malloc(work_size);
        size_t pidx;
        simple_bwt(data, work_size, bwt_out, &pidx);
        double h_before = calc_entropy(data, work_size);
        double h_after = calc_entropy(bwt_out, work_size);
        size_t runs_before = count_runs(data, work_size);
        size_t runs_after = count_runs(bwt_out, work_size);
        printf("Byte entropy: %.4f → %.4f bpb\n", h_before, h_after);
        printf("Runs: %zu → %zu (%.1f%% fewer, avg run: %.2f → %.2f bytes)\n",
               runs_before, runs_after,
               100.0 * (1.0 - (double)runs_after / runs_before),
               (double)work_size / runs_before, (double)work_size / runs_after);
        printf("Theoretical compressed: %.0f bytes (%.3fx)\n",
               h_after * work_size / 8.0, 8.0 / h_after);
        free(bwt_out);
    }
    
    printf("\n=== SUMMARY ===\n");
    printf("The question: does bit-level BWT capture structure that byte-level misses?\n");
    printf("Compare the 'runs' and 'entropy after BWT' numbers above.\n");
    
    free(planes);
    free(data);
    return 0;
}
