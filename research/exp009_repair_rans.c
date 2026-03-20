/**
 * Experiment 009: Re-Pair + rANS — Full Pipeline Test
 * 
 * Complete compressor prototype:
 * 1. Re-Pair: build grammar, reduce sequence
 * 2. Encode grammar rules compactly
 * 3. Encode reduced sequence with adaptive rANS
 * 4. Compare with MCX L20 on same files
 * 
 * Also test: Re-Pair output → BWT → MTF → rANS
 * (the reduced sequence might BWT better than the original)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define MAX_RULES 32000
#define PAIR_HASH_SIZE (1 << 20)

typedef struct {
    uint16_t left, right;
} rule_t;

typedef struct node {
    uint16_t symbol;
    struct node *prev, *next;
} node_t;

static uint32_t phash(uint32_t a, uint32_t b) {
    return ((a * 2654435761u) ^ (b * 2246822519u)) & (PAIR_HASH_SIZE - 1);
}

/* ── Re-Pair core ───────────────────────────────────────────────── */

typedef struct {
    rule_t rules[MAX_RULES];
    uint16_t n_rules;
    uint16_t* sequence;
    size_t seq_len;
    uint16_t max_symbol;
} repair_result_t;

static repair_result_t do_repair(const uint8_t* data, size_t size) {
    repair_result_t r = {0};
    
    /* Build linked list */
    node_t* nodes = malloc(size * sizeof(node_t));
    for (size_t i = 0; i < size; i++) {
        nodes[i].symbol = data[i];
        nodes[i].prev = (i > 0) ? &nodes[i-1] : NULL;
        nodes[i].next = (i < size-1) ? &nodes[i+1] : NULL;
    }
    node_t* head = &nodes[0];
    size_t seq_len = size;
    uint16_t next_sym = 256;
    
    /* Pair counting via hash */
    uint32_t* pair_counts = calloc(PAIR_HASH_SIZE, sizeof(uint32_t));
    
    while (r.n_rules < MAX_RULES) {
        /* Count all pairs */
        memset(pair_counts, 0, PAIR_HASH_SIZE * sizeof(uint32_t));
        
        uint16_t best_a = 0, best_b = 0;
        uint32_t best_count = 0;
        
        node_t* n = head;
        while (n && n->next) {
            uint32_t h = phash(n->symbol, n->next->symbol);
            pair_counts[h]++;
            if (pair_counts[h] > best_count) {
                best_count = pair_counts[h];
                best_a = n->symbol;
                best_b = n->next->symbol;
            }
            n = n->next;
        }
        
        /* Recount actual pair (hash collisions) */
        best_count = 0;
        n = head;
        while (n && n->next) {
            if (n->symbol == best_a && n->next->symbol == best_b)
                best_count++;
            n = n->next;
        }
        
        if (best_count < 2) break;
        
        /* Create rule and replace */
        r.rules[r.n_rules].left = best_a;
        r.rules[r.n_rules].right = best_b;
        r.n_rules++;
        
        n = head;
        while (n && n->next) {
            if (n->symbol == best_a && n->next->symbol == best_b) {
                n->symbol = next_sym;
                node_t* rm = n->next;
                n->next = rm->next;
                if (rm->next) rm->next->prev = n;
                seq_len--;
            } else {
                n = n->next;
            }
        }
        next_sym++;
    }
    
    /* Extract sequence */
    r.sequence = malloc(seq_len * sizeof(uint16_t));
    r.seq_len = seq_len;
    r.max_symbol = next_sym;
    size_t idx = 0;
    node_t* n = head;
    while (n) {
        r.sequence[idx++] = n->symbol;
        n = n->next;
    }
    
    free(nodes);
    free(pair_counts);
    return r;
}

/* ── Adaptive entropy coder (simulation) ────────────────────────── */

/* Estimate bits to encode a sequence with adaptive order-0 arithmetic coding */
static double adaptive_entropy_bits(const uint16_t* seq, size_t len, uint16_t max_sym) {
    /* Adaptive model: each symbol starts with count 1 */
    uint32_t* counts = calloc(max_sym, sizeof(uint32_t));
    for (uint16_t i = 0; i < max_sym; i++) counts[i] = 1;
    uint32_t total = max_sym;
    
    double bits = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t s = seq[i];
        /* Cost = -log2(count[s] / total) */
        bits += log2((double)total / counts[s]);
        counts[s]++;
        total++;
        /* Rescale */
        if (total > 60000) {
            total = 0;
            for (uint16_t j = 0; j < max_sym; j++) {
                counts[j] = (counts[j] + 1) >> 1;
                total += counts[j];
            }
        }
    }
    
    free(counts);
    return bits;
}

/* Same for byte sequences */
static double adaptive_entropy_bits_u8(const uint8_t* seq, size_t len) {
    uint32_t counts[256];
    for (int i = 0; i < 256; i++) counts[i] = 1;
    uint32_t total = 256;
    
    double bits = 0;
    for (size_t i = 0; i < len; i++) {
        bits += log2((double)total / counts[seq[i]]);
        counts[seq[i]]++;
        total++;
        if (total > 60000) {
            total = 0;
            for (int j = 0; j < 256; j++) {
                counts[j] = (counts[j] + 1) >> 1;
                total += counts[j];
            }
        }
    }
    return bits;
}

/* ── Grammar encoding ───────────────────────────────────────────── */

static double encode_grammar_bits(const rule_t* rules, uint16_t n_rules, uint16_t max_sym) {
    /* Encode each rule as (left, right) pair */
    /* Use log2(max_sym) bits per symbol reference */
    double bits_per_ref = log2(max_sym);
    /* Header: n_rules (16 bits) */
    return 16.0 + n_rules * 2 * bits_per_ref;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file> [max_bytes]\n", argv[0]); return 1; }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t full_size = ftell(f); fseek(f, 0, SEEK_SET);
    
    size_t max_size = (argc >= 3) ? (size_t)atoi(argv[2]) : 150000;
    size_t size = full_size < max_size ? full_size : max_size;
    
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Exp 009: Re-Pair + rANS Full Pipeline ===\n");
    printf("File: %s (%zu bytes)\n\n", argv[1], size);
    
    /* ── Baseline: adaptive order-0 on raw bytes ─── */
    double raw_bits = adaptive_entropy_bits_u8(data, size);
    printf("Baseline (adaptive order-0): %.0f bytes (%.3fx)\n", raw_bits/8, size/(raw_bits/8));
    
    /* ── Re-Pair ─── */
    printf("\nRunning Re-Pair...\n");
    repair_result_t rp = do_repair(data, size);
    printf("  Rules: %u, Sequence: %zu (was %zu), Alphabet: %u\n",
           rp.n_rules, rp.seq_len, size, rp.max_symbol);
    
    /* ── Encode Re-Pair output ─── */
    double grammar_bits = encode_grammar_bits(rp.rules, rp.n_rules, rp.max_symbol);
    double seq_bits = adaptive_entropy_bits(rp.sequence, rp.seq_len, rp.max_symbol);
    double total_repair = grammar_bits + seq_bits;
    
    printf("\n=== Re-Pair + Adaptive Entropy ===\n");
    printf("  Grammar: %.0f bytes\n", grammar_bits/8);
    printf("  Sequence: %.0f bytes (%.3f bpb over %zu symbols)\n",
           seq_bits/8, seq_bits/rp.seq_len, rp.seq_len);
    printf("  Total: %.0f bytes (%.3fx)\n", total_repair/8, size/(total_repair/8));
    
    /* ── Compare ─── */
    printf("\n=== COMPARISON ===\n");
    printf("  Raw adaptive order-0:    %.0f bytes (%.3fx)\n", raw_bits/8, size/(raw_bits/8));
    printf("  Re-Pair + adaptive:      %.0f bytes (%.3fx)\n", total_repair/8, size/(total_repair/8));
    
    double gain = raw_bits - total_repair;
    if (gain > 0) {
        printf("  🎯 Re-Pair WINS: -%.0f bytes (%.1f%% better)\n", gain/8, 100*gain/raw_bits);
    } else {
        printf("  ❌ Re-Pair LOSES: +%.0f bytes\n", -gain/8);
    }
    
    /* ── Estimate vs MCX ─── */
    printf("\n=== vs MCX (external comparison) ===\n");
    printf("  Run: mcx compress -l 20 on same file and compare\n");
    printf("  Re-Pair total: %.0f bytes\n", total_repair/8);
    printf("  If MCX does %.0f bytes, Re-Pair needs to beat that.\n", size/3.5);
    printf("  Note: MCX uses BWT which Re-Pair can complement (not replace).\n");
    
    /* ── Key analysis: what does Re-Pair capture that BWT doesn't? ─── */
    printf("\n=== Structural Analysis ===\n");
    /* Show top rules by frequency */
    printf("Top 10 rules by expansion:\n");
    for (int i = 0; i < 10 && i < rp.n_rules; i++) {
        /* Expand rule to original bytes */
        printf("  R%d → ", i + 256);
        
        /* Simple expansion (non-recursive for display) */
        uint16_t stack[64];
        int sp = 0;
        stack[sp++] = rp.rules[i].right;
        stack[sp++] = rp.rules[i].left;
        
        int expanded = 0;
        while (sp > 0 && expanded < 20) {
            uint16_t s = stack[--sp];
            if (s < 256) {
                if (s >= 32 && s < 127) printf("%c", s);
                else printf("\\x%02x", s);
                expanded++;
            } else if (s - 256 < rp.n_rules) {
                int ridx = s - 256;
                if (sp < 62) {
                    stack[sp++] = rp.rules[ridx].right;
                    stack[sp++] = rp.rules[ridx].left;
                }
            }
        }
        if (expanded >= 20) printf("...");
        
        /* Count occurrences in sequence */
        uint32_t count = 0;
        uint16_t sym = 256 + i;
        for (size_t j = 0; j < rp.seq_len; j++)
            if (rp.sequence[j] == sym) count++;
        
        printf("  (×%u)\n", count);
    }
    
    free(data);
    free(rp.sequence);
    return 0;
}
