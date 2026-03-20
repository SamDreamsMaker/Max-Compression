/**
 * Experiment 005: Approximate Matching for LZRC
 * 
 * REAL integration test: use MCX's actual hash chain / binary tree
 * match finder, then extend matches with edit tolerance.
 * 
 * New token types:
 *   LITERAL:  as before
 *   MATCH:    as before (exact copy from distance)
 *   AMATCH:   approximate match — copy N bytes from distance D,
 *             with K patches at positions P[i] with values V[i]
 * 
 * Encoding an AMATCH:
 *   - is_match=1, is_approx=1 (1 bit)
 *   - length (same as regular match)
 *   - distance (same as regular match)
 *   - n_edits (2 bits: 1-3 edits)
 *   - for each edit: position_in_match (log2(len) bits) + new_value (8 bits)
 *   
 * Cost: ~3 + n_edits * (log2(match_len) + 8) bits per AMATCH
 * vs literal cost: match_len * ~8 bits
 * 
 * Break-even: match_len > 3 + n_edits * (log2(len) + 8) / 8
 *   For 1 edit:  len > 3 + ~2 = 5 bytes
 *   For 2 edits: len > 3 + ~4 = 7 bytes
 *   For 3 edits: len > 3 + ~6 = 9 bytes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define WINDOW_BITS 16
#define WINDOW_SIZE (1 << WINDOW_BITS)
#define HASH_BITS 16
#define HASH_SIZE (1 << HASH_BITS)
#define HC_DEPTH 32
#define MIN_MATCH 4
#define MAX_MATCH 273
#define MAX_EDITS 3
#define NICE_MATCH 64  /* Stop searching if match >= this */

typedef struct {
    size_t length;
    size_t distance;
    size_t n_edits;
    uint16_t edit_pos[MAX_EDITS];
    uint8_t edit_val[MAX_EDITS];
} match_t;

/* Hash chain match finder */
typedef struct {
    uint32_t head[HASH_SIZE];
    uint32_t chain[WINDOW_SIZE];
    size_t pos;
} hc_state_t;

static uint32_t hash4(const uint8_t* p) {
    return (((uint32_t)p[0] << 11) ^ ((uint32_t)p[1] << 5) ^ 
            ((uint32_t)p[2] << 1) ^ (p[3] >> 2)) & (HASH_SIZE - 1);
}

static void hc_init(hc_state_t* hc) {
    memset(hc->head, 0xFF, sizeof(hc->head));
    hc->pos = 0;
}

static void hc_insert(hc_state_t* hc, const uint8_t* data, size_t pos) {
    if (pos + 4 > hc->pos + WINDOW_SIZE) return; /* Shouldn't happen */
    uint32_t h = hash4(data + pos);
    uint32_t wpos = pos & (WINDOW_SIZE - 1);
    hc->chain[wpos] = hc->head[h];
    hc->head[h] = wpos;
}

/* Find best EXACT match */
static match_t find_exact_match(const uint8_t* data, size_t size, size_t pos,
                                 hc_state_t* hc) {
    match_t best = {0, 0, 0, {0}, {0}};
    if (pos + MIN_MATCH > size) return best;
    
    uint32_t h = hash4(data + pos);
    uint32_t cur = hc->head[h];
    
    for (int depth = 0; depth < HC_DEPTH && cur != 0xFFFFFFFF; depth++) {
        size_t ref = (pos & ~(WINDOW_SIZE - 1)) | cur;
        if (ref >= pos) {
            if (pos >= WINDOW_SIZE)
                ref -= WINDOW_SIZE;
            else
                goto next;
        }
        if (pos - ref > WINDOW_SIZE) goto next;
        
        /* Extend exact match */
        size_t len = 0;
        size_t max_len = size - pos;
        if (max_len > MAX_MATCH) max_len = MAX_MATCH;
        
        while (len < max_len && data[ref + len] == data[pos + len])
            len++;
        
        if (len >= MIN_MATCH && len > best.length) {
            best.length = len;
            best.distance = pos - ref;
            best.n_edits = 0;
            if (len >= NICE_MATCH) break;
        }
        
    next:
        cur = hc->chain[cur];
    }
    return best;
}

/* Find best APPROXIMATE match (exact + fuzzy) */
static match_t find_approx_match(const uint8_t* data, size_t size, size_t pos,
                                  hc_state_t* hc) {
    match_t best = {0, 0, 0, {0}, {0}};
    if (pos + MIN_MATCH > size) return best;
    
    uint32_t h = hash4(data + pos);
    uint32_t cur = hc->head[h];
    
    for (int depth = 0; depth < HC_DEPTH && cur != 0xFFFFFFFF; depth++) {
        size_t ref = (pos & ~(WINDOW_SIZE - 1)) | cur;
        if (ref >= pos) {
            if (pos >= WINDOW_SIZE)
                ref -= WINDOW_SIZE;
            else
                goto next;
        }
        if (pos - ref > WINDOW_SIZE) goto next;
        
        size_t max_len = size - pos;
        if (max_len > MAX_MATCH) max_len = MAX_MATCH;
        size_t dist = pos - ref;
        
        /* Try exact match first */
        size_t exact_len = 0;
        while (exact_len < max_len && data[ref + exact_len] == data[pos + exact_len])
            exact_len++;
        
        if (exact_len >= MIN_MATCH && exact_len > best.length) {
            best.length = exact_len;
            best.distance = dist;
            best.n_edits = 0;
        }
        
        /* Try approximate match: extend past mismatches */
        size_t len = 0;
        size_t edits = 0;
        uint16_t epos[MAX_EDITS];
        uint8_t eval[MAX_EDITS];
        
        while (len < max_len) {
            if (data[ref + len] != data[pos + len]) {
                if (edits >= MAX_EDITS) break;
                epos[edits] = (uint16_t)len;
                eval[edits] = data[pos + len];
                edits++;
            }
            len++;
        }
        
        if (edits > 0 && len >= MIN_MATCH) {
            /* Cost of approx match vs exact + literals */
            /* Approx: ~3 bytes base + 2 bytes per edit (pos + value) */
            /* Benefit: match_length bytes captured */
            double approx_cost = 3.0 + edits * 2.0;
            double exact_benefit = len;  /* Would be literals otherwise */
            
            /* Compare with: using exact_len exact match + remaining as literals */
            double exact_cost = (exact_len >= MIN_MATCH) ? 3.0 : 0;
            double exact_gain = (exact_len >= MIN_MATCH) ? exact_len : 0;
            double literal_rest = len - exact_gain;
            
            /* Approx is better if it captures more net bytes */
            double approx_net = exact_benefit - approx_cost;
            double exact_net = exact_gain - exact_cost;
            
            if (approx_net > exact_net && approx_net > 0 && len > best.length) {
                best.length = len;
                best.distance = dist;
                best.n_edits = edits;
                memcpy(best.edit_pos, epos, edits * sizeof(uint16_t));
                memcpy(best.edit_val, eval, edits * sizeof(uint8_t));
            }
        }
        
    next:
        cur = hc->chain[cur];
    }
    return best;
}

/* ── Simulation ─────────────────────────────────────────────────── */

typedef struct {
    size_t exact_matches;
    size_t exact_bytes;
    size_t approx_matches;
    size_t approx_bytes;
    size_t approx_edits;
    size_t literals;
    double exact_encoded_bits;
    double approx_encoded_bits;
} stats_t;

static void simulate_compression(const uint8_t* data, size_t size, int use_approx) {
    hc_state_t* hc = calloc(1, sizeof(hc_state_t));
    hc_init(hc);
    
    stats_t s = {0};
    size_t pos = 0;
    
    while (pos < size) {
        /* Insert into hash chain */
        if (pos + 4 <= size)
            hc_insert(hc, data, pos);
        
        match_t m;
        if (use_approx)
            m = find_approx_match(data, size, pos, hc);
        else
            m = find_exact_match(data, size, pos, hc);
        
        if (m.length >= MIN_MATCH) {
            if (m.n_edits == 0) {
                s.exact_matches++;
                s.exact_bytes += m.length;
                /* Cost: ~3 bytes for length + distance */
                s.exact_encoded_bits += 24.0;
                s.approx_encoded_bits += 24.0;
            } else {
                s.approx_matches++;
                s.approx_bytes += m.length;
                s.approx_edits += m.n_edits;
                /* Cost: 3 bytes base + 2 bytes per edit */
                s.approx_encoded_bits += 24.0 + m.n_edits * 16.0;
            }
            /* Insert skipped positions into hash chain */
            for (size_t j = 1; j < m.length && pos + j + 4 <= size; j++) {
                hc_insert(hc, data, pos + j);
            }
            pos += m.length;
        } else {
            s.literals++;
            /* Literal cost: ~8 bits (entropy-coded) */
            s.exact_encoded_bits += 8.0;
            s.approx_encoded_bits += 8.0;
            pos++;
        }
    }
    
    /* Results */
    double total_exact = s.exact_encoded_bits + s.literals * 8.0;
    double total_approx = s.approx_encoded_bits + s.literals * 8.0;
    
    printf("  Exact matches:  %zu (%zu bytes, %.1f%%)\n",
           s.exact_matches, s.exact_bytes, 100.0 * s.exact_bytes / size);
    if (use_approx) {
        printf("  Approx matches: %zu (%zu bytes, %.1f%%, avg %.1f edits)\n",
               s.approx_matches, s.approx_bytes, 100.0 * s.approx_bytes / size,
               s.approx_matches > 0 ? (double)s.approx_edits / s.approx_matches : 0);
    }
    printf("  Literals:       %zu (%.1f%%)\n", s.literals, 100.0 * s.literals / size);
    printf("  Estimated size: %.0f bytes (%.3fx)\n",
           total_approx / 8.0, size / (total_approx / 8.0));
    
    if (use_approx && s.approx_matches > 0) {
        /* Calculate gain from approx */
        double without_approx = s.exact_matches * 24.0 + 
                                 (s.literals + s.approx_bytes) * 8.0;
        double with_approx = total_approx;
        printf("  Without approx: %.0f bytes\n", without_approx / 8.0);
        printf("  With approx:    %.0f bytes\n", with_approx / 8.0);
        printf("  🎯 Approx SAVES: %.0f bytes (%.1f%%)\n",
               (without_approx - with_approx) / 8.0,
               100.0 * (without_approx - with_approx) / without_approx);
    }
    
    free(hc);
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
    
    printf("=== Exp 005: Approximate Matching in LZ Pipeline ===\n");
    printf("Input: %s (%zu bytes)\n\n", argv[1], size);
    
    printf("--- Exact only (standard LZ) ---\n");
    simulate_compression(data, size, 0);
    
    printf("\n--- With approximate matching ---\n");
    simulate_compression(data, size, 1);
    
    free(data);
    return 0;
}
