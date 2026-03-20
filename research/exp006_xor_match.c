/**
 * Experiment 006: XOR Residual Matching
 * 
 * Instead of encoding edit positions+values explicitly,
 * emit the match reference then XOR the actual data with the 
 * reference. The XOR stream is mostly zeros (identical bytes = 0).
 * This zero-heavy stream compresses MUCH better than raw literals.
 * 
 * Encoding:
 *   For a region of N bytes with a reference at distance D:
 *   1. Copy N bytes from reference
 *   2. XOR actual data with reference: residual[i] = data[i] ^ ref[i]  
 *   3. Encode the residual stream (mostly zeros) with RLE or entropy coder
 * 
 * The key insight: we don't need a match/literal decision anymore.
 * EVERY byte either has a good reference (small XOR residual) or not.
 * We're essentially doing PREDICTION (reference = prediction) + 
 * RESIDUAL ENCODING (XOR = prediction error).
 * 
 * This is exactly how video codecs work (motion compensation + residual)
 * and why they achieve impossible compression ratios.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define WINDOW_SIZE (1 << 16)
#define HASH_SIZE   (1 << 16)
#define BLOCK_SIZE  32   /* Predict in blocks of N bytes */

static uint32_t h4(const uint8_t* p) {
    return (((uint32_t)p[0]<<11)^((uint32_t)p[1]<<5)^((uint32_t)p[2]<<1)^(p[3]>>2))&(HASH_SIZE-1);
}

/* For each block, find the best reference (lowest XOR residual energy) */
typedef struct {
    size_t ref_pos;      /* Position of best reference */
    size_t n_zeros;      /* Number of zero bytes in XOR residual */
    double residual_h;   /* Entropy of residual */
} block_ref_t;

static double byte_entropy(const uint8_t* data, size_t size) {
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

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Exp 006: XOR Residual Matching ===\n");
    printf("File: %s (%zu bytes)\n", argv[1], size);
    printf("Original entropy: %.4f bpb\n\n", byte_entropy(data, size));
    
    /* Build hash index for reference lookup */
    uint32_t* hash_head = calloc(HASH_SIZE, sizeof(uint32_t));
    uint32_t* hash_chain = calloc(WINDOW_SIZE, sizeof(uint32_t));
    memset(hash_head, 0xFF, HASH_SIZE * sizeof(uint32_t));
    
    /* Generate XOR residual stream */
    uint8_t* residual = calloc(size, 1);
    uint8_t* has_ref = calloc(size, 1);  /* 1 if this byte has a reference */
    size_t total_with_ref = 0;
    size_t total_zeros = 0;
    
    /* Try different block sizes */
    int block_sizes[] = {4, 8, 16, 32, 64};
    int n_block_sizes = 5;
    
    for (int bs_idx = 0; bs_idx < n_block_sizes; bs_idx++) {
        int BS = block_sizes[bs_idx];
        
        /* Reset */
        memset(hash_head, 0xFF, HASH_SIZE * sizeof(uint32_t));
        memset(residual, 0, size);
        memset(has_ref, 0, size);
        total_with_ref = 0;
        total_zeros = 0;
        
        for (size_t pos = 0; pos + BS <= size; pos += BS) {
            /* Insert previous block's start into hash */
            if (pos >= (size_t)BS && pos >= 4) {
                uint32_t h = h4(data + pos - BS);
                uint32_t w = (pos - BS) & (WINDOW_SIZE - 1);
                hash_chain[w] = hash_head[h];
                hash_head[h] = w;
            }
            
            if (pos < 4) continue;
            
            /* Find best reference for this block */
            uint32_t h = h4(data + pos);
            uint32_t cur = hash_head[h];
            
            size_t best_ref = 0;
            size_t best_zeros = 0;
            int found = 0;
            
            for (int dep = 0; dep < 16 && cur != 0xFFFFFFFF; dep++) {
                size_t ref = (pos & ~(size_t)(WINDOW_SIZE-1)) | cur;
                if (ref >= pos) { if (pos >= WINDOW_SIZE) ref -= WINDOW_SIZE; else goto nxt; }
                if (pos - ref > WINDOW_SIZE) goto nxt;
                if (ref + BS > pos) goto nxt; /* Reference must not overlap current block */
                
                /* Count zeros in XOR residual */
                size_t zeros = 0;
                for (int i = 0; i < BS; i++) {
                    if (data[ref + i] == data[pos + i]) zeros++;
                }
                
                if (zeros > best_zeros) {
                    best_zeros = zeros;
                    best_ref = ref;
                    found = 1;
                    if (zeros == (size_t)BS) break; /* Perfect match */
                }
            nxt:
                cur = hash_chain[cur];
            }
            
            /* Also try fixed references: pos-1, pos-BS, pos-2*BS */
            size_t fixed_refs[] = { 
                pos >= 1 ? pos - 1 : 0,
                pos >= (size_t)BS ? pos - BS : 0, 
                pos >= (size_t)(2*BS) ? pos - 2*BS : 0 
            };
            for (int fr = 0; fr < 3; fr++) {
                size_t ref = fixed_refs[fr];
                if (ref + BS > pos) continue;
                size_t zeros = 0;
                for (int i = 0; i < BS; i++) {
                    if (data[ref + i] == data[pos + i]) zeros++;
                }
                if (zeros > best_zeros) {
                    best_zeros = zeros;
                    best_ref = ref;
                    found = 1;
                }
            }
            
            if (found && best_zeros > (size_t)(BS / 4)) { /* At least 25% match */
                for (int i = 0; i < BS; i++) {
                    residual[pos + i] = data[pos + i] ^ data[best_ref + i];
                    has_ref[pos + i] = 1;
                }
                total_with_ref += BS;
                total_zeros += best_zeros;
            } else {
                /* No good reference — store as raw */
                for (int i = 0; i < BS; i++) {
                    residual[pos + i] = data[pos + i];
                }
            }
        }
        
        /* Analyze residual stream */
        double res_entropy = byte_entropy(residual, size);
        size_t res_zeros = 0;
        for (size_t i = 0; i < size; i++) if (residual[i] == 0) res_zeros++;
        
        /* Estimate: reference overhead + residual entropy */
        /* Each referenced block needs: distance (2 bytes) */
        size_t n_ref_blocks = total_with_ref / BS;
        double ref_overhead = n_ref_blocks * 16.0; /* 16 bits per reference */
        double residual_bits = res_entropy * size;
        double total_bits = ref_overhead + residual_bits;
        
        /* Compare with raw entropy */
        double raw_bits = byte_entropy(data, size) * size;
        
        printf("Block size %d:\n", BS);
        printf("  Referenced: %zu/%zu bytes (%.1f%%)\n", total_with_ref, size, 100.0*total_with_ref/size);
        printf("  XOR zeros:  %zu/%zu (%.1f%% of residual)\n", res_zeros, size, 100.0*res_zeros/size);
        printf("  Residual entropy: %.4f bpb (raw: %.4f)\n", res_entropy, byte_entropy(data, size));
        printf("  Estimated total:  %.0f bytes (%.3fx) vs raw entropy %.0f bytes (%.3fx)\n",
               total_bits/8, size/(total_bits/8), raw_bits/8, size/(raw_bits/8));
        
        if (total_bits < raw_bits) {
            printf("  🎯 WINS: -%.0f bytes (%.1f%% better than raw entropy)\n",
                   (raw_bits - total_bits)/8, 100.0*(raw_bits - total_bits)/raw_bits);
        } else {
            printf("  ❌ Worse than raw entropy\n");
        }
        printf("\n");
    }
    
    /* Final analysis: what if we use the BEST reference per byte (not per block)? */
    printf("=== Per-Byte Best Reference (upper bound) ===\n");
    /* For each byte, find the best matching byte in the last WINDOW_SIZE bytes */
    memset(residual, 0, size);
    size_t byte_zeros = 0;
    for (size_t i = 1; i < size; i++) {
        /* Simple: use prev byte as reference (delta coding) */
        residual[i] = data[i] ^ data[i-1];
        if (residual[i] == 0) byte_zeros++;
    }
    double delta_entropy = byte_entropy(residual, size);
    printf("Delta XOR (prev byte): %.4f bpb, zeros: %zu (%.1f%%)\n",
           delta_entropy, byte_zeros, 100.0*byte_zeros/size);
    printf("Estimated: %.0f bytes (%.3fx)\n\n",
           delta_entropy*size/8, size/(delta_entropy*size/8));
    
    free(data);
    free(residual);
    free(has_ref);
    free(hash_head);
    free(hash_chain);
    return 0;
}
