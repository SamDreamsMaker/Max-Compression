/**
 * @file hc_match.h
 * @brief Hash chain match finder for LZRC compression.
 * 
 * Faster than binary tree but finds slightly shorter matches.
 * Best for speed-oriented compression where BT overhead isn't justified.
 */

#ifndef HC_MATCH_H
#define HC_MATCH_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const uint8_t* data;
    size_t size;
    uint32_t pos;
    
    uint32_t window_size;
    uint32_t window_mask;
    
    uint32_t* head;       /* Hash table → most recent position */
    uint32_t* chain;      /* Chain: chain[pos & mask] → previous pos with same hash */
    int hash_bits;
    int depth;            /* Max chain depth to search */
} HCMatchFinder;

typedef struct {
    uint32_t length;
    uint32_t offset;
} HCMatch;

/**
 * Initialize hash chain match finder.
 */
int hc_init(HCMatchFinder* hc, const uint8_t* data, size_t size,
            uint32_t window_size, int hash_bits, int depth);

/**
 * Find matches at current position and advance.
 * Returns number of matches found (0 or 1 — best match only).
 */
int hc_find(HCMatchFinder* hc, HCMatch* out, int max_matches);

/**
 * Skip positions (after emitting a match).
 */
void hc_skip(HCMatchFinder* hc, int count);

/**
 * Free resources.
 */
void hc_free(HCMatchFinder* hc);

#endif /* HC_MATCH_H */
