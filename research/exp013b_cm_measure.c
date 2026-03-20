/**
 * Exp 013b: CM-Lite — measure cross-entropy (theoretical compressed size)
 * Skip the AC implementation, just compute -log2(P(actual_bit)) for each bit.
 * This gives the EXACT compressed size a perfect AC would achieve.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define PROB_BITS 12
#define PROB_MAX  4096
#define PROB_HALF 2048
#define MAX_MODELS 8

typedef struct {
    uint16_t* prob;
    size_t n_ctx;
    int rate;
} model_t;

static void model_init(model_t* m, size_t n, int rate) {
    m->n_ctx = n; m->rate = rate;
    m->prob = malloc(n * sizeof(uint16_t));
    for (size_t i = 0; i < n; i++) m->prob[i] = PROB_HALF;
}
static void model_free(model_t* m) { free(m->prob); }

static uint16_t model_predict(model_t* m, uint32_t ctx) {
    return m->prob[ctx % m->n_ctx];
}

static void model_update(model_t* m, uint32_t ctx, int bit) {
    uint32_t c = ctx % m->n_ctx;
    uint16_t p = m->prob[c];
    m->prob[c] = bit ? p + ((PROB_MAX - p) >> m->rate) : p - (p >> m->rate);
}

/* Logistic mixing */
static double stretch(double p) {
    if (p < 0.0001) p = 0.0001;
    if (p > 0.9999) p = 0.9999;
    return log(p / (1.0 - p));
}

static double squash(double x) {
    if (x > 10) return 0.9999;
    if (x < -10) return 0.0001;
    return 1.0 / (1.0 + exp(-x));
}

typedef struct {
    double w[MAX_MODELS];
    int n;
    double lr;
} mixer_t;

static void mixer_init(mixer_t* mx, int n) {
    mx->n = n; mx->lr = 0.005;
    for (int i = 0; i < n; i++) mx->w[i] = 1.0 / n;
}

static double mixer_predict(mixer_t* mx, uint16_t* preds) {
    double sum = 0;
    for (int i = 0; i < mx->n; i++) {
        double p = (double)preds[i] / PROB_MAX;
        sum += mx->w[i] * stretch(p);
    }
    double r = squash(sum);
    if (r < 0.0001) r = 0.0001;
    if (r > 0.9999) r = 0.9999;
    return r;
}

static void mixer_update(mixer_t* mx, uint16_t* preds, int bit) {
    double sum = 0;
    double s[MAX_MODELS];
    for (int i = 0; i < mx->n; i++) {
        s[i] = stretch((double)preds[i] / PROB_MAX);
        sum += mx->w[i] * s[i];
    }
    double mixed = squash(sum);
    double err = bit - mixed;
    for (int i = 0; i < mx->n; i++)
        mx->w[i] += mx->lr * err * s[i];
}

/* Match model */
#define MATCH_HASH (1 << 18)
typedef struct {
    uint32_t tab[MATCH_HASH];
    const uint8_t* d;
    size_t mpos, mlen;
    int active;
} mmatch_t;

static void mm_init(mmatch_t* mm, const uint8_t* d) {
    memset(mm->tab, 0xFF, sizeof(mm->tab));
    mm->d = d; mm->active = 0; mm->mlen = 0;
}

static uint16_t mm_predict(mmatch_t* mm, size_t pos, int bp) {
    if (!mm->active || mm->mpos >= pos) return PROB_HALF;
    int pred = (mm->d[mm->mpos] >> (7 - bp)) & 1;
    int conf = mm->mlen > 16 ? 16 : (int)mm->mlen;
    int delta = conf * PROB_HALF / 20;
    return pred ? PROB_HALF + delta : PROB_HALF - delta;
}

static void mm_update_byte(mmatch_t* mm, size_t pos) {
    if (pos < 4) return;
    if (mm->active && mm->mpos < pos) {
        if (mm->d[mm->mpos] == mm->d[pos-1]) { mm->mpos++; mm->mlen++; }
        else { mm->active = 0; mm->mlen = 0; }
    }
    if (!mm->active) {
        uint32_t h = ((uint32_t)mm->d[pos-3]<<16)|((uint32_t)mm->d[pos-2]<<8)|mm->d[pos-1];
        uint32_t s = (h * 2654435761u) >> (32 - 18);
        if (mm->tab[s] != 0xFFFFFFFF) {
            size_t ref = mm->tab[s];
            if (ref+3 <= pos && ref >= 3 &&
                mm->d[ref-3]==mm->d[pos-3] && mm->d[ref-2]==mm->d[pos-2] && mm->d[ref-1]==mm->d[pos-1]) {
                mm->active = 1; mm->mpos = ref; mm->mlen = 3;
            }
        }
        mm->tab[s] = pos;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== CM-Lite: Theoretical Compressed Size ===\n");
    printf("File: %s (%zu bytes)\n\n", argv[1], size);
    
    /* Models */
    model_t o1, o2, o4, sparse, o0, o6, word_ctx;
    model_init(&o0, 256 * 8, 4);         /* Order-0: just bit position */
    model_init(&o1, 256 * 256, 5);       /* Order-1 */
    model_init(&o2, 65536 * 8, 5);       /* Order-2 */
    model_init(&o4, 1 << 20, 6);         /* Order-4 hashed */
    model_init(&sparse, 65536 * 8, 5);   /* Sparse (-1,-3) */
    model_init(&o6, 1 << 22, 7);         /* Order-6 hashed (4M contexts) */
    model_init(&word_ctx, 1 << 20, 6);   /* Word context model */
    
    mmatch_t match;
    mm_init(&match, data);
    
    mixer_t mixer;
    mixer_init(&mixer, 8);
    
    /* Word context: hash of last "word" (alpha chars) */
    uint32_t word_hash = 0;
    
    double total_bits = 0;
    uint8_t prev[4] = {0};
    
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = data[i];
        mm_update_byte(&match, i);
        uint8_t partial = 1; /* 1-bit prefix for context */
        
        for (int bit = 7; bit >= 0; bit--) {
            int b = (byte >> bit) & 1;
            int bp = 7 - bit;
            
            uint16_t preds[MAX_MODELS];
            
            /* Order-0 */
            preds[0] = model_predict(&o0, partial * 8 + bp);
            
            /* Order-1 */
            uint32_t c1 = (uint32_t)prev[0] * 256 + partial;
            preds[1] = model_predict(&o1, c1);
            
            /* Order-2 */
            uint32_t c2 = ((uint32_t)prev[1] << 8 | prev[0]) * 8 + bp;
            preds[2] = model_predict(&o2, c2);
            
            /* Order-4 hashed */
            uint32_t c4 = (((uint32_t)prev[3]<<24)|((uint32_t)prev[2]<<16)|
                           ((uint32_t)prev[1]<<8)|prev[0]) * 2654435761u;
            c4 = ((c4 >> 12) * 8 + bp) % o4.n_ctx;
            preds[3] = model_predict(&o4, c4);
            
            /* Sparse */
            uint32_t cs = ((uint32_t)prev[0] << 8 | prev[2]) * 8 + bp;
            preds[4] = model_predict(&sparse, cs);
            
            /* Match */
            preds[5] = mm_predict(&match, i, bp);
            
            /* Order-6 hashed */
            uint32_t c6 = c4;
            if (i >= 6) c6 ^= ((uint32_t)data[i-5] << 20) ^ ((uint32_t)data[i-6] << 28);
            c6 = ((c6 * 2654435761u >> 10) * 8 + bp) % o6.n_ctx;
            preds[6] = model_predict(&o6, c6);
            
            /* Word context */
            uint32_t wc = (word_hash * 8 + bp) % word_ctx.n_ctx;
            preds[7] = model_predict(&word_ctx, wc);
            
            /* Mix */
            double mixed = mixer_predict(&mixer, preds);
            
            /* Cross-entropy: -log2(P(actual bit)) */
            double p_actual = b ? mixed : (1.0 - mixed);
            total_bits += -log2(p_actual);
            
            /* Update */
            model_update(&o0, partial * 8 + bp, b);
            model_update(&o1, c1, b);
            model_update(&o2, c2, b);
            model_update(&o4, c4, b);
            model_update(&sparse, cs, b);
            model_update(&o6, c6, b);
            model_update(&word_ctx, wc, b);
            mixer_update(&mixer, preds, b);
            
            partial = (partial << 1) | b;
        }
        
        prev[3]=prev[2]; prev[2]=prev[1]; prev[1]=prev[0]; prev[0]=byte;
        
        /* Update word hash */
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'))
            word_hash = word_hash * 31 + byte;
        else
            word_hash = 0;
    }
    
    double comp_bytes = total_bits / 8.0;
    printf("CM-Lite compressed: %.0f bytes (%.4f bpb, %.3fx)\n",
           comp_bytes, total_bits / size, size / comp_bytes);
    
    printf("\nMixer weights: ");
    const char* names[] = {"o0", "o1", "o2", "o4", "sparse", "match", "o6", "word"};
    for (int i = 0; i < mixer.n; i++)
        printf("%s=%.3f ", names[i], mixer.w[i]);
    printf("\n");
    
    /* Raw entropy baseline */
    uint32_t counts[256] = {0};
    for (size_t i = 0; i < size; i++) counts[data[i]]++;
    double raw_h = 0;
    for (int i = 0; i < 256; i++)
        if (counts[i] > 0) { double p = (double)counts[i]/size; raw_h -= p*log2(p); }
    
    printf("\nBaselines:\n");
    printf("  Raw entropy (order-0): %.0f bytes (%.3fx)\n", raw_h*size/8, 8.0/raw_h);
    printf("  MCX L20 (BWT+rANS):   43154 bytes (3.524x) [alice29]\n");
    printf("  PAQ8 (full CM):        ~40000 bytes (~3.8x) [alice29]\n");
    
    model_free(&o0); model_free(&o1); model_free(&o2); model_free(&o4); model_free(&sparse); model_free(&o6); model_free(&word_ctx);
    free(data);
    return 0;
}
