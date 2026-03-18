/**
 * @file hc_match.c
 * @brief Hash chain match finder — fast alternative to binary tree.
 */

#include "hc_match.h"
#include <stdlib.h>
#include <string.h>

#define HC_NIL 0xFFFFFFFF

static inline uint32_t hc_hash4(const uint8_t* p, int bits) {
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (v * 2654435761u) >> (32 - bits);
}

int hc_init(HCMatchFinder* hc, const uint8_t* data, size_t size,
            uint32_t window_size, int hash_bits, int depth) {
    hc->data = data;
    hc->size = size;
    hc->pos = 0;
    hc->window_size = window_size;
    hc->window_mask = window_size - 1;
    hc->hash_bits = hash_bits;
    hc->depth = depth;
    
    size_t ht_size = (size_t)1 << hash_bits;
    hc->head = (uint32_t*)malloc(ht_size * sizeof(uint32_t));
    hc->chain = (uint32_t*)malloc(window_size * sizeof(uint32_t));
    
    if (!hc->head || !hc->chain) {
        free(hc->head); free(hc->chain);
        return -1;
    }
    
    memset(hc->head, 0xFF, ht_size * sizeof(uint32_t)); /* HC_NIL */
    memset(hc->chain, 0xFF, window_size * sizeof(uint32_t));
    
    return 0;
}

int hc_find(HCMatchFinder* hc, HCMatch* out, int max_matches) {
    uint32_t pos = hc->pos;
    
    if (pos + 4 > hc->size) {
        hc->pos++;
        return 0;
    }
    
    uint32_t h = hc_hash4(hc->data + pos, hc->hash_bits);
    uint32_t cur = hc->head[h];
    
    /* Insert current position into chain */
    hc->chain[pos & hc->window_mask] = cur;
    hc->head[h] = pos;
    hc->pos++;
    
    /* Search chain for best match */
    uint32_t best_len = 3; /* Min match - 1 */
    uint32_t best_dist = 0;
    uint32_t limit = (pos > hc->window_size) ? pos - hc->window_size : 0;
    int found = 0;
    
    for (int d = 0; d < hc->depth && cur != HC_NIL && cur >= limit; d++) {
        /* Quick check: compare last byte of current best first */
        if (hc->data[cur + best_len] == hc->data[pos + best_len]) {
            /* Full comparison */
            uint32_t len = 0;
            uint32_t max_len = hc->size - pos;
            if (max_len > 273) max_len = 273;
            
            while (len < max_len && hc->data[cur + len] == hc->data[pos + len])
                len++;
            
            if (len > best_len) {
                best_len = len;
                best_dist = pos - cur;
                found = 1;
                
                if (best_len >= 273) break; /* Can't improve */
            }
        }
        
        cur = hc->chain[cur & hc->window_mask];
    }
    
    if (found && best_len >= 4) {
        out[0].length = best_len;
        out[0].offset = best_dist;
        return 1;
    }
    
    return 0;
}

void hc_skip(HCMatchFinder* hc, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t pos = hc->pos;
        if (pos + 4 <= hc->size) {
            uint32_t h = hc_hash4(hc->data + pos, hc->hash_bits);
            hc->chain[pos & hc->window_mask] = hc->head[h];
            hc->head[h] = pos;
        }
        hc->pos++;
    }
}

void hc_free(HCMatchFinder* hc) {
    free(hc->head);
    free(hc->chain);
    hc->head = NULL;
    hc->chain = NULL;
}
