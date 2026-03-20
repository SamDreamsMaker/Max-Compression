/**
 * Experiment 004: Approximate Pattern Matching Compression
 * 
 * HYPOTHESIS: LZ77 only finds EXACT matches. But real data has many
 * APPROXIMATE matches — regions that differ by 1-2 bytes out of 20.
 * If we could encode "copy 20 bytes from offset X, but change byte 5
 * from A to B", we'd capture more redundancy than exact matching.
 * 
 * This is essentially what video codecs do (motion compensation with
 * residuals) but applied to general data compression.
 * 
 * APPROACH:
 * 1. Build a hash index of N-gram fingerprints (like LZ, but fuzzy)
 * 2. For each position, find the best approximate match
 * 3. If the match saves bytes (match_len - edit_cost > 0), emit it
 * 4. Encode: exact match | approx match (copy + patch list) | literal
 * 
 * KEY INSIGHT: BWT doesn't capture this either — BWT groups by exact
 * suffix context. Two suffixes that differ by 1 byte go to completely
 * different positions in the BWT output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define WINDOW_SIZE (1 << 16) /* 64KB sliding window */
#define MIN_MATCH 8           /* Minimum match length for approx */
#define MAX_EDITS 3           /* Max byte differences per match */
#define HASH_BITS 16
#define HASH_SIZE (1 << HASH_BITS)
#define NGRAM_LEN 4           /* Hash fingerprint length */

/* Rolling hash for fuzzy matching */
static uint32_t ngram_hash(const uint8_t* p) {
    return ((uint32_t)p[0] << 12) ^ ((uint32_t)p[1] << 8) ^
           ((uint32_t)p[2] << 4) ^ p[3];
}

typedef struct {
    uint32_t pos;
} hash_entry;

typedef struct {
    size_t offset;      /* Distance back */
    size_t length;      /* Total match length */
    size_t n_edits;     /* Number of byte differences */
    size_t edit_positions[MAX_EDITS]; /* Where the differences are */
    uint8_t edit_values[MAX_EDITS];  /* What the correct values are */
} approx_match_t;

/* Find best approximate match at position */
static approx_match_t find_approx_match(const uint8_t* data, size_t size, size_t pos,
                                         hash_entry* htab) {
    approx_match_t best = {0, 0, 0, {0}, {0}};
    
    if (pos + NGRAM_LEN > size) return best;
    
    uint32_t h = ngram_hash(data + pos) & (HASH_SIZE - 1);
    
    /* Check multiple hash chain entries */
    for (int chain = 0; chain < 8; chain++) {
        uint32_t slot = (h + chain * 7) & (HASH_SIZE - 1);
        if (htab[slot].pos == 0) continue;
        
        size_t ref = htab[slot].pos - 1; /* 0 = empty, so store pos+1 */
        if (ref >= pos) continue;
        if (pos - ref > WINDOW_SIZE) continue;
        
        /* Extend match allowing up to MAX_EDITS mismatches */
        size_t len = 0;
        size_t edits = 0;
        size_t edit_pos[MAX_EDITS];
        uint8_t edit_val[MAX_EDITS];
        
        while (ref + len < pos && pos + len < size && len < 256) {
            if (data[ref + len] != data[pos + len]) {
                if (edits >= MAX_EDITS) break;
                edit_pos[edits] = len;
                edit_val[edits] = data[pos + len];
                edits++;
            }
            len++;
        }
        
        /* Score: bytes saved = match_length - overhead */
        /* Exact match overhead: ~3 bytes (len + offset) */
        /* Approx match overhead: ~3 + edits * 2 bytes (pos + value per edit) */
        size_t overhead_exact = 3;
        size_t overhead_approx = 3 + edits * 2;
        
        if (len > overhead_approx && len > best.length) {
            best.offset = pos - ref;
            best.length = len;
            best.n_edits = edits;
            memcpy(best.edit_positions, edit_pos, edits * sizeof(size_t));
            memcpy(best.edit_values, edit_val, edits * sizeof(uint8_t));
        }
    }
    
    return best;
}

/* Measure how much data we can capture with approximate matching */
static void analyze_approx_potential(const uint8_t* data, size_t size) {
    hash_entry* htab = calloc(HASH_SIZE, sizeof(hash_entry));
    
    size_t exact_matched = 0;
    size_t approx_matched = 0;
    size_t approx_only = 0; /* Bytes matched by approx but NOT by exact */
    size_t n_exact = 0, n_approx = 0, n_approx_only = 0;
    size_t total_edits = 0;
    
    /* Distribution of edit counts */
    size_t edit_dist[MAX_EDITS + 1] = {0};
    
    size_t pos = 0;
    while (pos < size - NGRAM_LEN) {
        approx_match_t m = find_approx_match(data, size, pos, htab);
        
        /* Update hash table */
        uint32_t h = ngram_hash(data + pos) & (HASH_SIZE - 1);
        htab[h].pos = pos + 1;
        
        if (m.length >= MIN_MATCH) {
            if (m.n_edits == 0) {
                exact_matched += m.length;
                n_exact++;
                edit_dist[0]++;
            } else {
                approx_matched += m.length;
                n_approx++;
                total_edits += m.n_edits;
                edit_dist[m.n_edits]++;
                
                /* How many of these bytes would NOT be captured by exact match? */
                /* Simple heuristic: if edits > 0, at least some bytes are new */
                approx_only += m.n_edits * 5; /* rough: each edit "enables" ~5 extra bytes */
                n_approx_only++;
            }
            pos += m.length;
        } else {
            pos++;
        }
    }
    
    printf("\n=== Approximate Matching Analysis ===\n");
    printf("File size: %zu bytes\n", size);
    printf("\nExact matches (0 edits):\n");
    printf("  Count: %zu, Total bytes: %zu (%.1f%% of file)\n",
           n_exact, exact_matched, 100.0 * exact_matched / size);
    printf("\nApproximate matches (1-%d edits):\n", MAX_EDITS);
    printf("  Count: %zu, Total bytes: %zu (%.1f%% of file)\n",
           n_approx, approx_matched, 100.0 * approx_matched / size);
    printf("  Avg edits per match: %.2f\n", n_approx > 0 ? (double)total_edits / n_approx : 0);
    printf("  Approx-only bytes (not in exact): ~%zu (%.1f%% of file)\n",
           approx_only, 100.0 * approx_only / size);
    
    printf("\nEdit distribution:\n");
    for (int e = 0; e <= MAX_EDITS; e++) {
        printf("  %d edits: %zu matches\n", e, edit_dist[e]);
    }
    
    size_t total_matched = exact_matched + approx_matched;
    size_t unmatched = size - total_matched;
    printf("\nTotal matched: %zu (%.1f%%)\n", total_matched, 100.0 * total_matched / size);
    printf("Unmatched (literals): %zu (%.1f%%)\n", unmatched, 100.0 * unmatched / size);
    
    /* Estimate compression with and without approx matching */
    /* Exact-only compressed: exact_match tokens + literals */
    double exact_comp = n_exact * 3.0 + (size - exact_matched) * 1.0;
    /* Approx compressed: exact + approx tokens + fewer literals */
    double approx_comp = n_exact * 3.0 + n_approx * (3.0 + 2.0 * ((double)total_edits / (n_approx > 0 ? n_approx : 1))) + unmatched * 1.0;
    
    printf("\n=== Estimated Impact ===\n");
    printf("LZ (exact only): ~%.0f bytes (%.2fx)\n", exact_comp, size / exact_comp);
    printf("LZ (with approx): ~%.0f bytes (%.2fx)\n", approx_comp, size / approx_comp);
    printf("Potential gain from approx: %.1f%%\n",
           100.0 * (exact_comp - approx_comp) / exact_comp);
    
    free(htab);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Approximate Pattern Matching Experiment ===\n");
    printf("Input: %s (%zu bytes)\n", argv[1], size);
    
    analyze_approx_potential(data, size);
    
    free(data);
    return 0;
}
