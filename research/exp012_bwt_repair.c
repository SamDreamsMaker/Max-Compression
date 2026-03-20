/**
 * Experiment 012: BWT+MTF → Re-Pair → rANS
 * 
 * Standard:  data → BWT → MTF → RLE2 → rANS
 * Proposed:  data → BWT → MTF → Re-Pair → rANS
 * 
 * After BWT+MTF, the output has specific structure:
 * - Many 0s (MTF zero = repeated context byte)
 * - Small values (1, 2, 3 = recent symbols)
 * - Patterns like 0,0,0,1,0,0,0,1 (repeated structures)
 * 
 * Re-Pair can capture these patterns hierarchically.
 * R1 = (0,0), R2 = (R1,R1), R3 = (R2,0) → encodes long zero runs
 * But also captures non-trivial patterns like (0,1,0,2) that RLE2 misses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define MAX_RULES 16000
#define PAIR_HASH_SIZE (1 << 20)

/* Simple BWT */
static const uint8_t* g_d; static size_t g_s;
static int scmp(const void* a, const void* b) {
    size_t ia=*(const size_t*)a, ib=*(const size_t*)b;
    for (size_t k=0;k<g_s;k++) {
        uint8_t ca=g_d[(ia+k)%g_s], cb=g_d[(ib+k)%g_s];
        if (ca<cb) return -1; if (ca>cb) return 1;
    }
    return 0;
}
static void bwt(const uint8_t* in, uint8_t* out, size_t size) {
    size_t* sa=malloc(size*sizeof(size_t));
    for (size_t i=0;i<size;i++) sa[i]=i;
    g_d=in; g_s=size;
    qsort(sa, size, sizeof(size_t), scmp);
    for (size_t i=0;i<size;i++) out[i]=in[(sa[i]+size-1)%size];
    free(sa);
}

/* MTF */
static void mtf(uint8_t* data, size_t size) {
    uint8_t t[256]; for (int i=0;i<256;i++) t[i]=i;
    for (size_t i=0;i<size;i++) {
        uint8_t v=data[i]; int p=0;
        while (t[p]!=v) p++;
        data[i]=p;
        memmove(t+1,t,p); t[0]=v;
    }
}

/* Re-Pair */
typedef struct { uint16_t left, right; } rule_t;
typedef struct nd { uint16_t sym; struct nd *p, *n; } nd_t;

static uint32_t ph(uint32_t a, uint32_t b) {
    return ((a*2654435761u)^(b*2246822519u))&(PAIR_HASH_SIZE-1);
}

static double entropy(const uint8_t* d, size_t s) {
    uint32_t c[256]={0}; for (size_t i=0;i<s;i++) c[d[i]]++;
    double h=0; for (int i=0;i<256;i++) if (c[i]>0) { double p=(double)c[i]/s; h-=p*log2(p); }
    return h;
}

static double adaptive_bits_u16(const uint16_t* seq, size_t len, uint16_t max_sym) {
    uint32_t* c=calloc(max_sym, sizeof(uint32_t));
    for (uint16_t i=0;i<max_sym;i++) c[i]=1;
    uint32_t tot=max_sym;
    double bits=0;
    for (size_t i=0;i<len;i++) {
        bits += log2((double)tot/c[seq[i]]);
        c[seq[i]]++; tot++;
        if (tot>60000) { tot=0; for (uint16_t j=0;j<max_sym;j++) { c[j]=(c[j]+1)>>1; tot+=c[j]; } }
    }
    free(c);
    return bits;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file> [max]\n", argv[0]); return 1; }
    FILE* f=fopen(argv[1],"rb"); if (!f) { perror("open"); return 1; }
    fseek(f,0,SEEK_END); size_t full=ftell(f); fseek(f,0,SEEK_SET);
    size_t size = (argc>=3) ? (size_t)atoi(argv[2]) : full;
    if (size>full) size=full;
    if (size>50000) size=50000; /* BWT is O(n²) here */
    uint8_t* data=malloc(size);
    if (fread(data,1,size,f)!=size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Exp 012: BWT+MTF → Re-Pair ===\n");
    printf("File: %s (%zu bytes)\n\n", argv[1], size);
    
    /* BWT + MTF */
    uint8_t* bwt_out=malloc(size);
    bwt(data, bwt_out, size);
    uint8_t* mtf_out=malloc(size);
    memcpy(mtf_out, bwt_out, size);
    mtf(mtf_out, size);
    
    printf("After BWT+MTF:\n");
    printf("  Entropy: %.4f bpb\n", entropy(mtf_out, size));
    size_t zeros=0; for (size_t i=0;i<size;i++) if (mtf_out[i]==0) zeros++;
    printf("  Zeros: %zu (%.1f%%)\n", zeros, 100.0*zeros/size);
    double baseline_bits = entropy(mtf_out, size) * size;
    printf("  Estimated (order-0): %.0f bytes (%.3fx)\n\n", baseline_bits/8, size/(baseline_bits/8));
    
    /* Re-Pair on MTF output */
    nd_t* nodes=malloc(size*sizeof(nd_t));
    for (size_t i=0;i<size;i++) {
        nodes[i].sym=mtf_out[i]; nodes[i].p=(i>0)?&nodes[i-1]:NULL; nodes[i].n=(i<size-1)?&nodes[i+1]:NULL;
    }
    nd_t* head=&nodes[0];
    size_t seq_len=size;
    uint16_t next_sym=256;
    rule_t rules[MAX_RULES];
    uint16_t n_rules=0;
    uint32_t* pc=calloc(PAIR_HASH_SIZE, sizeof(uint32_t));
    
    while (n_rules < MAX_RULES) {
        memset(pc,0,PAIR_HASH_SIZE*sizeof(uint32_t));
        uint16_t ba=0,bb=0; uint32_t bc=0;
        for (nd_t* n=head;n&&n->n;n=n->n) {
            uint32_t h=ph(n->sym,n->n->sym); pc[h]++;
            if (pc[h]>bc) { bc=pc[h]; ba=n->sym; bb=n->n->sym; }
        }
        bc=0; for (nd_t* n=head;n&&n->n;n=n->n) if (n->sym==ba&&n->n->sym==bb) bc++;
        if (bc<2) break;
        rules[n_rules].left=ba; rules[n_rules].right=bb; n_rules++;
        for (nd_t* n=head;n&&n->n;) {
            if (n->sym==ba&&n->n->sym==bb) {
                n->sym=next_sym; nd_t* rm=n->n; n->n=rm->n; if (rm->n) rm->n->p=n; seq_len--;
            } else n=n->n;
        }
        next_sym++;
    }
    
    /* Extract sequence */
    uint16_t* seq=malloc(seq_len*sizeof(uint16_t));
    size_t idx=0;
    for (nd_t* n=head;n;n=n->n) seq[idx++]=n->sym;
    
    printf("After Re-Pair on MTF output:\n");
    printf("  Rules: %u, Sequence: %zu→%zu, Alphabet: %u\n", n_rules, size, seq_len, next_sym);
    
    /* Encode */
    double grammar_bits = 16.0 + n_rules * 2 * log2(next_sym);
    double seq_bits = adaptive_bits_u16(seq, seq_len, next_sym);
    double total = grammar_bits + seq_bits;
    
    printf("  Grammar: %.0f bytes\n", grammar_bits/8);
    printf("  Sequence: %.0f bytes (%.3f bps)\n", seq_bits/8, seq_bits/seq_len);
    printf("  Total: %.0f bytes (%.3fx)\n\n", total/8, size/(total/8));
    
    printf("=== COMPARISON ===\n");
    printf("  BWT+MTF → order-0 entropy: %.0f bytes (%.3fx)\n", baseline_bits/8, size/(baseline_bits/8));
    printf("  BWT+MTF → Re-Pair + adaptive: %.0f bytes (%.3fx)\n", total/8, size/(total/8));
    
    double gain = baseline_bits - total;
    if (gain > 0) {
        printf("  🎯 Re-Pair WINS: -%.0f bytes (%.1f%%)\n", gain/8, 100*gain/baseline_bits);
    } else {
        printf("  ❌ Re-Pair LOSES: +%.0f bytes\n", -gain/8);
    }
    
    /* Show top rules */
    printf("\nTop 5 Re-Pair rules on BWT+MTF output:\n");
    for (int i=0;i<5&&i<n_rules;i++) {
        printf("  R%d: (%u, %u)\n", 256+i, rules[i].left, rules[i].right);
    }
    
    free(data); free(bwt_out); free(mtf_out); free(nodes); free(pc); free(seq);
    return 0;
}
