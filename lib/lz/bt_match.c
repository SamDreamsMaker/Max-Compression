/**
 * @file bt_match.c
 * @brief Binary Tree Match Finder implementation.
 *
 * At each position, we insert the current string into a binary tree
 * keyed by string comparison. While inserting, we find the longest
 * matches among existing tree nodes (positions).
 *
 * The tree is indexed by position. For each position p:
 * - left[p] = position with a string < string at p
 * - right[p] = position with a string > string at p
 *
 * Hash table maps 4-byte prefix → tree root for that prefix group.
 * This gives O(1) initial lookup + O(depth) tree traversal.
 *
 * Memory: 8 bytes per position (left + right) + hash table.
 * For 16MB window: 128MB + hash = ~130MB total.
 */

#include "bt_match.h"
#include <stdlib.h>
#include <string.h>

#define BT_EMPTY  0xFFFFFFFF
#define BT_MIN_MATCH  4

static inline uint32_t bt_hash4(const uint8_t* p, int hash_log) {
    uint32_t v;
    memcpy(&v, p, 4);
    return (v * 2654435761u) >> (32 - hash_log);
}

int bt_init(BTMatchFinder* bt, const uint8_t* data, size_t size,
            uint32_t window_size, int hash_log, int max_depth) {
    bt->data = data;
    bt->data_size = size;
    bt->hash_log = hash_log;
    bt->hash_mask = (1u << hash_log) - 1;
    bt->window_size = window_size;
    bt->pos = 0;
    bt->max_depth = max_depth;
    bt->min_match = BT_MIN_MATCH;
    bt->max_match = 273; /* Same as LZMA */
    
    size_t hash_size = (size_t)1 << hash_log;
    bt->hash = (uint32_t*)malloc(hash_size * sizeof(uint32_t));
    if (!bt->hash) return -1;
    memset(bt->hash, 0xFF, hash_size * sizeof(uint32_t)); /* BT_EMPTY */
    
    /* Allocate tree arrays only for window_size positions */
    size_t tree_size = (size < window_size) ? size : window_size;
    bt->left = (uint32_t*)malloc(tree_size * sizeof(uint32_t));
    bt->right = (uint32_t*)malloc(tree_size * sizeof(uint32_t));
    if (!bt->left || !bt->right) {
        bt_free(bt);
        return -1;
    }
    memset(bt->left, 0xFF, tree_size * sizeof(uint32_t));
    memset(bt->right, 0xFF, tree_size * sizeof(uint32_t));
    
    return 0;
}

/* Compare strings at positions p1 and p2, starting from offset `start`.
 * Returns the total match length (including start). */
static inline uint32_t bt_compare(const uint8_t* data, size_t size,
                                   uint32_t p1, uint32_t p2,
                                   uint32_t start, uint32_t max_len) {
    uint32_t len = start;
    uint32_t limit = size - (p1 > p2 ? p1 : p2);
    if (limit > max_len) limit = max_len;
    
    /* Fast 8-byte comparison */
    while (len + 8 <= limit) {
        uint64_t v1, v2;
        memcpy(&v1, data + p1 + len, 8);
        memcpy(&v2, data + p2 + len, 8);
        if (v1 != v2) {
            len += __builtin_ctzll(v1 ^ v2) >> 3;
            return (len > limit) ? limit : len;
        }
        len += 8;
    }
    while (len < limit && data[p1 + len] == data[p2 + len]) len++;
    return len;
}

int bt_find(BTMatchFinder* bt, BTMatch* matches, int max_matches) {
    if (bt->pos + bt->min_match > bt->data_size) {
        bt->pos++;
        return 0;
    }
    
    uint32_t cur = bt->pos;
    bt->pos++;
    
    uint32_t h = bt_hash4(bt->data + cur, bt->hash_log);
    uint32_t cur_idx = cur % bt->window_size;
    
    /* Current position becomes the new root for this hash bucket.
     * Walk old tree to find matches and rebuild tree with cur as root. */
    uint32_t old_root = bt->hash[h];
    bt->hash[h] = cur;
    
    /* left_ptr and right_ptr track where to attach subtrees */
    uint32_t* left_ptr = &bt->left[cur_idx];
    uint32_t* right_ptr = &bt->right[cur_idx];
    *left_ptr = BT_EMPTY;
    *right_ptr = BT_EMPTY;
    
    uint32_t best_len = bt->min_match - 1;
    int n_matches = 0;
    uint32_t len_left = 0, len_right = 0;
    int depth = bt->max_depth;
    
    uint32_t node = old_root;
    
    while (node != BT_EMPTY && depth > 0) {
        depth--;
        
        /* Check if node is within window */
        if (cur > node && (cur - node) > bt->window_size) {
            break;
        }
        
        uint32_t node_idx = node % bt->window_size;
        uint32_t start = (len_left < len_right) ? len_left : len_right;
        uint32_t len = bt_compare(bt->data, bt->data_size,
                                   cur, node, start, bt->max_match);
        
        if (len > best_len) {
            best_len = len;
            if (n_matches < max_matches) {
                matches[n_matches].offset = cur - node;
                matches[n_matches].length = len;
                n_matches++;
            }
            if (len >= bt->max_match) {
                /* Perfect match — steal subtrees */
                *left_ptr = bt->left[node_idx];
                *right_ptr = bt->right[node_idx];
                return n_matches;
            }
        }
        
        /* Decide which subtree to follow based on comparison at len */
        if (cur + len < bt->data_size && node + len < bt->data_size &&
            bt->data[cur + len] < bt->data[node + len]) {
            /* cur < node at mismatch → node goes to right subtree */
            *right_ptr = node;
            right_ptr = &bt->left[node_idx];
            node = bt->left[node_idx];
            len_right = len;
        } else {
            /* cur >= node at mismatch → node goes to left subtree */
            *left_ptr = node;
            left_ptr = &bt->right[node_idx];
            node = bt->right[node_idx];
            len_left = len;
        }
    }
    
    /* Terminate the subtrees */
    *left_ptr = BT_EMPTY;
    *right_ptr = BT_EMPTY;
    return n_matches;
}

void bt_skip(BTMatchFinder* bt, uint32_t n) {
    /* Insert positions into tree with reduced depth (we don't need matches) */
    int saved_depth = bt->max_depth;
    bt->max_depth = 4; /* Much shallower during skip — just maintain connectivity */
    
    BTMatch dummy[1];
    for (uint32_t i = 0; i < n && bt->pos < bt->data_size; i++) {
        bt_find(bt, dummy, 1);
    }
    
    bt->max_depth = saved_depth;
}

void bt_free(BTMatchFinder* bt) {
    if (bt->hash) { free(bt->hash); bt->hash = NULL; }
    if (bt->left) { free(bt->left); bt->left = NULL; }
    if (bt->right) { free(bt->right); bt->right = NULL; }
}
