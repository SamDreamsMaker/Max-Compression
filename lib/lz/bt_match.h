/**
 * @file bt_match.h
 * @brief Binary Tree Match Finder for v2.0 LZ compression.
 *
 * Provides O(log n) match finding with large windows (up to 16MB).
 * Uses a binary search tree where nodes are positions in the input,
 * ordered by the string at that position.
 *
 * Based on the approach used in LZMA/7-zip.
 */

#ifndef MCX_BT_MATCH_H
#define MCX_BT_MATCH_H

#include <stdint.h>
#include <stddef.h>

/* Match result */
typedef struct {
    uint32_t offset;    /* Distance from current position */
    uint32_t length;    /* Match length */
} BTMatch;

/* Binary tree match finder state */
typedef struct {
    const uint8_t* data;
    size_t data_size;
    
    /* Hash table: maps 4-byte hash → tree root position */
    uint32_t* hash;
    int hash_log;
    uint32_t hash_mask;
    
    /* Binary tree: left[pos] and right[pos] are children */
    uint32_t* left;
    uint32_t* right;
    
    /* Window */
    uint32_t window_size;
    uint32_t pos;
    
    /* Configuration */
    int max_depth;       /* Max tree traversal depth */
    uint32_t min_match;  /* Minimum match length */
    uint32_t max_match;  /* Maximum match length */
    int use_ctx3_hash;   /* Use 3-byte context hash (bytes 0,1,3) */
} BTMatchFinder;

/* Initialize match finder.
 * window_size: max distance for matches (e.g., 16MB)
 * hash_log: hash table size = 2^hash_log (e.g., 20 = 1M entries)
 * max_depth: max tree depth per position (e.g., 32-128) */
int bt_init(BTMatchFinder* bt, const uint8_t* data, size_t size,
            uint32_t window_size, int hash_log, int max_depth);

/* Find best match at current position.
 * Returns number of matches found (0-max_matches).
 * matches[] is filled with (offset, length) pairs, longest first. */
int bt_find(BTMatchFinder* bt, BTMatch* matches, int max_matches);

/* Advance position by n bytes (skip without finding matches) */
void bt_skip(BTMatchFinder* bt, uint32_t n);

/* Enable 3-byte context hash (bytes 0,1,3 — skip byte 2).
 * Better for structured/columnar data with fixed-width fields.
 * Call after bt_init(), before bt_find(). */
void bt_set_ctx3_hash(BTMatchFinder* bt, int enable);

/* Free resources */
void bt_free(BTMatchFinder* bt);

#endif /* MCX_BT_MATCH_H */
