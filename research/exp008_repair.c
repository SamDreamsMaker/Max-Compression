/**
 * Experiment 008: Re-Pair Grammar Compression
 * 
 * FUNDAMENTALLY DIFFERENT from both LZ and BWT.
 * 
 * Re-Pair builds a context-free grammar that generates the input:
 * 1. Find most frequent pair (a,b) in the sequence
 * 2. Create rule R → ab
 * 3. Replace all occurrences of (a,b) with R
 * 4. Repeat until no pair appears ≥ 2 times
 * 5. Output: grammar rules + reduced sequence
 * 
 * This is the same idea as BPE (tokenization in LLMs) but applied
 * to general data compression. It captures hierarchical structure
 * that both LZ and BWT miss.
 * 
 * Example: "abcabcabc" → R1=ab, R2=R1c → "R2R2R2" → R3=R2R2 → "R3R2"
 *   Grammar: R1→ab, R2→R1c, R3→R2R2
 *   Output: R3 R2
 *   Total: 3 rules (6 symbols) + 2 output symbols = 8
 *   Original: 9 bytes. Savings: 11%
 *   
 *   On real data with many repeated phrases, savings can be 40%+
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define MAX_SYMBOLS 65536   /* Max alphabet size (256 base + up to 65280 rules) */
#define PAIR_HASH_BITS 20
#define PAIR_HASH_SIZE (1 << PAIR_HASH_BITS)

typedef struct {
    uint32_t left, right; /* Rule: symbol → left right */
} rule_t;

typedef struct {
    uint32_t a, b;       /* Pair (a, b) */
    uint32_t count;      /* Frequency */
    uint32_t hash_next;  /* Hash chain */
} pair_entry_t;

/* Doubly-linked list for the sequence */
typedef struct node {
    uint32_t symbol;
    struct node* prev;
    struct node* next;
} node_t;

static uint32_t pair_hash(uint32_t a, uint32_t b) {
    return ((a * 2654435761u) ^ (b * 2246822519u)) & (PAIR_HASH_SIZE - 1);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file> [max_bytes]\n", argv[0]); return 1; }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t full_size = ftell(f); fseek(f, 0, SEEK_SET);
    
    size_t max_size = (argc >= 3) ? (size_t)atoi(argv[2]) : 100000;
    size_t size = full_size < max_size ? full_size : max_size;
    
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Exp 008: Re-Pair Grammar Compression ===\n");
    printf("File: %s (%zu bytes, using %zu)\n\n", argv[1], full_size, size);
    
    /* Build linked list */
    node_t* nodes = malloc(size * sizeof(node_t));
    for (size_t i = 0; i < size; i++) {
        nodes[i].symbol = data[i];
        nodes[i].prev = (i > 0) ? &nodes[i-1] : NULL;
        nodes[i].next = (i < size-1) ? &nodes[i+1] : NULL;
    }
    node_t* head = &nodes[0];
    
    /* Count initial pairs */
    uint32_t* pair_counts = calloc(PAIR_HASH_SIZE, sizeof(uint32_t));
    /* Simple approach: for each pair, count via hash */
    
    rule_t* rules = malloc(MAX_SYMBOLS * sizeof(rule_t));
    uint32_t n_rules = 0;
    uint32_t next_symbol = 256; /* New symbols start at 256 */
    
    size_t seq_len = size;
    size_t total_rule_cost = 0;
    int iteration = 0;
    
    while (iteration < 10000) { /* Max iterations */
        /* Count all pairs */
        memset(pair_counts, 0, PAIR_HASH_SIZE * sizeof(uint32_t));
        
        uint32_t best_a = 0, best_b = 0, best_count = 0;
        
        node_t* n = head;
        while (n && n->next) {
            uint32_t h = pair_hash(n->symbol, n->next->symbol);
            pair_counts[h]++;
            if (pair_counts[h] > best_count) {
                /* Verify it's the same pair (hash collision possible) */
                /* For speed, just use the hash — slight inaccuracy OK for prototype */
                best_count = pair_counts[h];
                best_a = n->symbol;
                best_b = n->next->symbol;
            }
            n = n->next;
        }
        
        /* Recount the actual best pair (not just hash bucket) */
        best_count = 0;
        n = head;
        while (n && n->next) {
            if (n->symbol == best_a && n->next->symbol == best_b)
                best_count++;
            n = n->next;
        }
        
        if (best_count < 2) break; /* No more pairs worth replacing */
        
        /* Create new rule */
        uint32_t new_sym = next_symbol++;
        if (new_sym >= MAX_SYMBOLS) break;
        
        rules[n_rules].left = best_a;
        rules[n_rules].right = best_b;
        n_rules++;
        
        /* Cost of this rule: 2 symbols for the definition */
        total_rule_cost += 2;
        
        /* Replace all occurrences */
        size_t replaced = 0;
        n = head;
        while (n && n->next) {
            if (n->symbol == best_a && n->next->symbol == best_b) {
                n->symbol = new_sym;
                /* Remove n->next from list */
                node_t* removed = n->next;
                n->next = removed->next;
                if (removed->next) removed->next->prev = n;
                seq_len--;
                replaced++;
                /* Don't advance — check if new pair formed */
            } else {
                n = n->next;
            }
        }
        
        iteration++;
        
        if (iteration % 500 == 0 || iteration <= 10) {
            printf("  Iter %d: replaced (%u,%u) ×%zu, seq_len=%zu, rules=%u\n",
                   iteration, best_a, best_b, replaced, seq_len, n_rules);
        }
    }
    
    printf("\n=== Results ===\n");
    printf("Iterations: %d\n", iteration);
    printf("Rules: %u\n", n_rules);
    printf("Sequence length: %zu (was %zu)\n", seq_len, size);
    printf("Total rule cost: %zu symbols\n", total_rule_cost);
    
    /* Count distinct symbols in output */
    uint8_t* seen = calloc(next_symbol, 1);
    int distinct = 0;
    node_t* n = head;
    while (n) {
        if (n->symbol < next_symbol && !seen[n->symbol]) {
            seen[n->symbol] = 1;
            distinct++;
        }
        n = n->next;
    }
    free(seen);
    
    printf("Distinct symbols in output: %d\n", distinct);
    
    /* Estimate compressed size */
    /* Output sequence: seq_len symbols, each needs log2(next_symbol) bits */
    double bits_per_sym = log2(next_symbol);
    double seq_bits = seq_len * bits_per_sym;
    /* Rule table: n_rules entries, each is (left, right) */
    double rule_bits = n_rules * 2 * bits_per_sym;
    /* Overhead: number of rules (16 bits) */
    double overhead_bits = 16;
    double total_bits = seq_bits + rule_bits + overhead_bits;
    
    printf("\nEstimated output:\n");
    printf("  Sequence: %.0f bytes (%zu × %.1f bits)\n", seq_bits/8, seq_len, bits_per_sym);
    printf("  Rules:    %.0f bytes (%u × 2 × %.1f bits)\n", rule_bits/8, n_rules, bits_per_sym);
    printf("  Total:    %.0f bytes (%.3fx compression)\n", total_bits/8, size/(total_bits/8));
    
    /* Better estimate: entropy-code the output sequence */
    /* Build frequency table of output symbols */
    uint32_t* sym_freq = calloc(next_symbol, sizeof(uint32_t));
    n = head;
    while (n) {
        if (n->symbol < next_symbol) sym_freq[n->symbol]++;
        n = n->next;
    }
    
    double seq_entropy = 0;
    for (uint32_t s = 0; s < next_symbol; s++) {
        if (sym_freq[s] > 0) {
            double p = (double)sym_freq[s] / seq_len;
            seq_entropy -= p * log2(p);
        }
    }
    double entropy_bits = seq_entropy * seq_len + rule_bits + overhead_bits;
    
    printf("\nWith entropy coding:\n");
    printf("  Sequence entropy: %.4f bpb (across %zu symbols)\n", seq_entropy, seq_len);
    printf("  Total: %.0f bytes (%.3fx compression)\n", entropy_bits/8, size/(entropy_bits/8));
    
    /* Compare with raw byte entropy */
    double raw_h = 0;
    uint32_t byte_counts[256] = {0};
    for (size_t i = 0; i < size; i++) byte_counts[data[i]]++;
    for (int i = 0; i < 256; i++) {
        if (byte_counts[i] > 0) {
            double p = (double)byte_counts[i] / size;
            raw_h -= p * log2(p);
        }
    }
    printf("\nRaw byte entropy: %.4f bpb → %.0f bytes (%.3fx)\n",
           raw_h, raw_h*size/8, 8.0/raw_h);
    
    double delta = raw_h * size / 8 - entropy_bits / 8;
    if (delta > 0) {
        printf("🎯 Re-Pair BEATS raw entropy by %.0f bytes (%.1f%%)\n",
               delta, 100.0*delta/(raw_h*size/8));
    } else {
        printf("❌ Re-Pair loses to raw entropy by %.0f bytes\n", -delta);
    }
    
    free(data);
    free(nodes);
    free(pair_counts);
    free(rules);
    free(sym_freq);
    return 0;
}
