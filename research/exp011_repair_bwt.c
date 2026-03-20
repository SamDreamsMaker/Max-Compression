/**
 * Experiment 011: Re-Pair → BWT — the optimal combination
 * 
 * Re-Pair reduces the sequence, BWT sorts by context, rANS encodes.
 * 
 * Serialization strategy for 16-bit symbols → byte stream:
 * Method A: Split into high/low bytes (interleaved or sequential)
 * Method B: Remap symbols to bytes where possible (if < 256 distinct)
 * Method C: Use MTF on symbols before byte serialization
 * 
 * We test all methods, then BWT+MTF+rANS each one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define MAX_RULES 32000
#define PAIR_HASH_SIZE (1 << 20)

typedef struct { uint16_t left, right; } rule_t;
typedef struct node { uint16_t symbol; struct node *prev, *next; } node_t;

static uint32_t phash(uint32_t a, uint32_t b) {
    return ((a*2654435761u)^(b*2246822519u))&(PAIR_HASH_SIZE-1);
}

typedef struct {
    rule_t rules[MAX_RULES];
    uint16_t n_rules;
    uint16_t* sequence;
    size_t seq_len;
    uint16_t max_symbol;
} repair_t;

static repair_t do_repair(const uint8_t* data, size_t size) {
    repair_t r = {0};
    node_t* nodes = malloc(size * sizeof(node_t));
    for (size_t i = 0; i < size; i++) {
        nodes[i].symbol = data[i];
        nodes[i].prev = (i > 0) ? &nodes[i-1] : NULL;
        nodes[i].next = (i < size-1) ? &nodes[i+1] : NULL;
    }
    node_t* head = &nodes[0];
    size_t seq_len = size;
    uint16_t next_sym = 256;
    uint32_t* pc = calloc(PAIR_HASH_SIZE, sizeof(uint32_t));
    
    while (r.n_rules < MAX_RULES) {
        memset(pc, 0, PAIR_HASH_SIZE * sizeof(uint32_t));
        uint16_t ba = 0, bb = 0; uint32_t bc = 0;
        for (node_t* n = head; n && n->next; n = n->next) {
            uint32_t h = phash(n->symbol, n->next->symbol);
            pc[h]++;
            if (pc[h] > bc) { bc = pc[h]; ba = n->symbol; bb = n->next->symbol; }
        }
        bc = 0;
        for (node_t* n = head; n && n->next; n = n->next)
            if (n->symbol == ba && n->next->symbol == bb) bc++;
        if (bc < 2) break;
        
        r.rules[r.n_rules].left = ba;
        r.rules[r.n_rules].right = bb;
        r.n_rules++;
        
        for (node_t* n = head; n && n->next; ) {
            if (n->symbol == ba && n->next->symbol == bb) {
                n->symbol = next_sym;
                node_t* rm = n->next;
                n->next = rm->next;
                if (rm->next) rm->next->prev = n;
                seq_len--;
            } else n = n->next;
        }
        next_sym++;
    }
    
    r.sequence = malloc(seq_len * sizeof(uint16_t));
    r.seq_len = seq_len;
    r.max_symbol = next_sym;
    size_t idx = 0;
    for (node_t* n = head; n; n = n->next) r.sequence[idx++] = n->symbol;
    free(nodes); free(pc);
    return r;
}

static double entropy(const uint8_t* data, size_t size) {
    uint32_t c[256] = {0};
    for (size_t i = 0; i < size; i++) c[data[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++)
        if (c[i] > 0) { double p = (double)c[i]/size; h -= p*log2(p); }
    return h;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file> [max_bytes]\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t full = ftell(f); fseek(f, 0, SEEK_SET);
    size_t size = (argc >= 3) ? (size_t)atoi(argv[2]) : full;
    if (size > full) size = full;
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Exp 011: Re-Pair → Byte Serialization ===\n");
    printf("File: %s (%zu bytes)\n\n", argv[1], size);
    printf("Raw entropy: %.4f bpb → %.0f bytes (%.3fx)\n",
           entropy(data, size), entropy(data,size)*size/8, 8.0/entropy(data,size));
    
    repair_t rp = do_repair(data, size);
    printf("\nRe-Pair: %u rules, %zu→%zu symbols, alphabet %u\n",
           rp.n_rules, size, rp.seq_len, rp.max_symbol);
    
    /* Grammar cost: compact binary encoding */
    /* n_rules (2 bytes) + per rule: left (2 bytes) + right (2 bytes) = 4 bytes/rule */
    size_t grammar_bytes = 2 + rp.n_rules * 4;
    printf("Grammar cost: %zu bytes\n\n", grammar_bytes);
    
    /* Method A: Hi/Lo byte split */
    printf("=== Method A: Hi/Lo byte split ===\n");
    {
        size_t out_size = rp.seq_len * 2;
        uint8_t* hi = malloc(rp.seq_len);
        uint8_t* lo = malloc(rp.seq_len);
        for (size_t i = 0; i < rp.seq_len; i++) {
            hi[i] = rp.sequence[i] >> 8;
            lo[i] = rp.sequence[i] & 0xFF;
        }
        double h_hi = entropy(hi, rp.seq_len);
        double h_lo = entropy(lo, rp.seq_len);
        double total = grammar_bytes * 8 + h_hi * rp.seq_len + h_lo * rp.seq_len;
        printf("  Hi entropy: %.4f bpb, Lo entropy: %.4f bpb\n", h_hi, h_lo);
        printf("  Total: %.0f bytes (%.3fx)\n", total/8, size/(total/8));
        
        /* Write hi+lo concatenated to file for MCX test */
        char path[256];
        snprintf(path, sizeof(path), "/tmp/_repair_hilo_%zu.bin", size);
        FILE* out = fopen(path, "wb");
        if (out) {
            /* Grammar header */
            uint16_t nr = rp.n_rules;
            fwrite(&nr, 2, 1, out);
            for (int i = 0; i < rp.n_rules; i++) {
                fwrite(&rp.rules[i].left, 2, 1, out);
                fwrite(&rp.rules[i].right, 2, 1, out);
            }
            /* Hi bytes then Lo bytes */
            uint32_t slen = rp.seq_len;
            fwrite(&slen, 4, 1, out);
            fwrite(hi, 1, rp.seq_len, out);
            fwrite(lo, 1, rp.seq_len, out);
            fclose(out);
            printf("  Wrote: %s (%zu bytes)\n", path, grammar_bytes + 4 + rp.seq_len*2);
        }
        free(hi); free(lo);
    }
    
    /* Method B: MTF on 16-bit symbols → write as byte sequence */
    printf("\n=== Method B: Symbol MTF → bytes ===\n");
    {
        /* MTF on the symbol sequence: output will be small integers if locality is good */
        uint16_t* mtf_table = malloc(rp.max_symbol * sizeof(uint16_t));
        for (uint16_t i = 0; i < rp.max_symbol; i++) mtf_table[i] = i;
        
        uint8_t* mtf_out = malloc(rp.seq_len * 3); /* VLQ: max 3 bytes per symbol */
        size_t mtf_size = 0;
        
        for (size_t i = 0; i < rp.seq_len; i++) {
            uint16_t sym = rp.sequence[i];
            /* Find position in MTF table */
            int pos = 0;
            while (pos < rp.max_symbol && mtf_table[pos] != sym) pos++;
            
            /* VLQ encode the position */
            if (pos < 128) {
                mtf_out[mtf_size++] = pos;
            } else if (pos < 16384) {
                mtf_out[mtf_size++] = 0x80 | (pos >> 7);
                mtf_out[mtf_size++] = pos & 0x7F;
            } else {
                mtf_out[mtf_size++] = 0x80 | (pos >> 14);
                mtf_out[mtf_size++] = 0x80 | ((pos >> 7) & 0x7F);
                mtf_out[mtf_size++] = pos & 0x7F;
            }
            
            /* Move to front */
            memmove(mtf_table + 1, mtf_table, pos * sizeof(uint16_t));
            mtf_table[0] = sym;
        }
        
        double h = entropy(mtf_out, mtf_size);
        double total = grammar_bytes * 8 + h * mtf_size;
        printf("  MTF output: %zu bytes, entropy: %.4f bpb\n", mtf_size, h);
        printf("  Total: %.0f bytes (%.3fx)\n", total/8, size/(total/8));
        
        /* Write for MCX test */
        char path[256];
        snprintf(path, sizeof(path), "/tmp/_repair_mtf_%zu.bin", size);
        FILE* out = fopen(path, "wb");
        if (out) {
            uint16_t nr = rp.n_rules;
            fwrite(&nr, 2, 1, out);
            for (int i = 0; i < rp.n_rules; i++) {
                fwrite(&rp.rules[i].left, 2, 1, out);
                fwrite(&rp.rules[i].right, 2, 1, out);
            }
            uint32_t ms = mtf_size;
            fwrite(&ms, 4, 1, out);
            fwrite(mtf_out, 1, mtf_size, out);
            fclose(out);
            printf("  Wrote: %s (%zu bytes)\n", path, grammar_bytes + 4 + mtf_size);
        }
        
        free(mtf_table);
        free(mtf_out);
    }
    
    /* Method C: Interleaved 16-bit LE */
    printf("\n=== Method C: Raw 16-bit LE ===\n");
    {
        size_t out_size = rp.seq_len * 2;
        uint8_t* raw16 = malloc(out_size);
        for (size_t i = 0; i < rp.seq_len; i++) {
            raw16[i*2] = rp.sequence[i] & 0xFF;
            raw16[i*2+1] = rp.sequence[i] >> 8;
        }
        double h = entropy(raw16, out_size);
        double total = grammar_bytes * 8 + h * out_size;
        printf("  Raw 16-bit: %zu bytes, entropy: %.4f bpb\n", out_size, h);
        printf("  Total: %.0f bytes (%.3fx)\n", total/8, size/(total/8));
        free(raw16);
    }
    
    printf("\n=== NOW COMPRESS WITH MCX ===\n");
    printf("Run these commands to compare:\n");
    printf("  mcx compress -l 20 %s           → MCX(raw)\n", argv[1]);
    printf("  mcx compress -l 20 /tmp/_repair_hilo_%zu.bin  → MCX(Re-Pair hi/lo)\n", size);
    printf("  mcx compress -l 20 /tmp/_repair_mtf_%zu.bin   → MCX(Re-Pair MTF)\n", size);
    
    free(data);
    free(rp.sequence);
    return 0;
}
