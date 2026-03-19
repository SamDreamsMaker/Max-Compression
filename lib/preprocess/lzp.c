/**
 * @file lzp.c
 * @brief LZP (Lempel-Ziv Prediction) preprocessing for BWT compression.
 *
 * LZP removes long repeated sequences before BWT, reducing the input size
 * and improving BWT effectiveness on mixed code/text files with repeated blocks.
 *
 * Algorithm:
 * - Hash table maps 3-byte contexts to last occurrence position
 * - At each position, look up predicted match from context hash
 * - If predicted position matches (>= MIN_MATCH bytes), encode match length
 * - Otherwise output literal byte
 * - No offsets needed: decoder rebuilds the same hash table from decoded output
 *
 * Format:
 * - Byte 0: escape byte value
 * - Then stream of:
 *   - literal bytes (non-escape)
 *   - escape + 0x00 = literal escape byte
 *   - escape + N (1..253) = match of length N + MIN_MATCH - 1
 *   - escape + 0xFE + 2-byte LE length = match of length (value + MIN_MATCH - 1)
 */

#include "../internal.h"
#include <string.h>
#include <stdlib.h>

#define LZP_HASH_BITS   18
#define LZP_HASH_SIZE   (1 << LZP_HASH_BITS)
#define LZP_HASH_MASK   (LZP_HASH_SIZE - 1)
#define LZP_MIN_MATCH   4
#define LZP_MAX_MATCH   (253 + LZP_MIN_MATCH - 1)  /* Short encoding max */
#define LZP_MAX_LONG    (65535 + LZP_MIN_MATCH - 1) /* Long encoding max */
#define LZP_MIN_SIZE    64   /* Don't bother with tiny inputs */
#define LZP_CONTEXT     3    /* Context bytes for hash */

static inline uint32_t lzp_hash3(const uint8_t* p)
{
    /* Fast hash of 3 bytes */
    return ((uint32_t)p[0] * 2654435761u ^ 
            (uint32_t)p[1] * 2246822519u ^ 
            (uint32_t)p[2] * 3266489917u) & LZP_HASH_MASK;
}

/**
 * Find the least frequent byte in the input to use as escape.
 */
static uint8_t lzp_find_escape(const uint8_t* src, size_t size)
{
    size_t freq[256] = {0};
    size_t check = size > 65536 ? 65536 : size;
    
    for (size_t i = 0; i < check; i++)
        freq[src[i]]++;
    
    uint8_t best = 0xFF;
    size_t best_count = freq[0xFF];
    
    for (int i = 254; i >= 0; i--) {
        if (freq[i] < best_count) {
            best_count = freq[i];
            best = (uint8_t)i;
            if (best_count == 0) break;
        }
    }
    return best;
}

size_t mcx_lzp_encode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size)
{
    if (src_size < LZP_MIN_SIZE || !dst || !src)
        return 0;
    
    /* Allocate hash table */
    int32_t* htable = (int32_t*)calloc(LZP_HASH_SIZE, sizeof(int32_t));
    if (!htable) return 0;
    
    /* Initialize all entries to -1 (no match) */
    memset(htable, 0xFF, LZP_HASH_SIZE * sizeof(int32_t));
    
    uint8_t escape = lzp_find_escape(src, src_size);
    
    if (dst_cap < 1) { free(htable); return 0; }
    dst[0] = escape;
    size_t out_pos = 1;
    size_t i = 0;
    
    /* Process input - need at least LZP_CONTEXT bytes of history for hashing */
    while (i < src_size) {
        if (i < LZP_CONTEXT) {
            /* Not enough context yet - output literal */
            if (out_pos >= dst_cap) { free(htable); return 0; }
            if (src[i] == escape) {
                if (out_pos + 1 >= dst_cap) { free(htable); return 0; }
                dst[out_pos++] = escape;
                dst[out_pos++] = 0x00;
            } else {
                dst[out_pos++] = src[i];
            }
            i++;
            continue;
        }
        
        /* Compute context hash from previous 3 bytes */
        uint32_t h = lzp_hash3(src + i - LZP_CONTEXT);
        int32_t pred = htable[h];
        
        /* Update hash table with current position */
        htable[h] = (int32_t)i;
        
        /* Try to match at predicted position */
        size_t match_len = 0;
        if (pred >= 0 && (size_t)pred < i) {
            size_t max_len = src_size - i;
            if (max_len > LZP_MAX_LONG) max_len = LZP_MAX_LONG;
            
            const uint8_t* a = src + i;
            const uint8_t* b = src + pred;
            
            while (match_len < max_len && a[match_len] == b[match_len])
                match_len++;
        }
        
        if (match_len >= LZP_MIN_MATCH) {
            /* Encode match */
            if (match_len <= LZP_MAX_MATCH) {
                /* Short match: escape + length byte */
                if (out_pos + 1 >= dst_cap) { free(htable); return 0; }
                dst[out_pos++] = escape;
                dst[out_pos++] = (uint8_t)(match_len - LZP_MIN_MATCH + 1);
            } else {
                /* Long match: escape + 0xFE + 2-byte LE length */
                uint16_t len16 = (uint16_t)(match_len - LZP_MIN_MATCH + 1);
                if (out_pos + 3 >= dst_cap) { free(htable); return 0; }
                dst[out_pos++] = escape;
                dst[out_pos++] = 0xFE;
                memcpy(dst + out_pos, &len16, 2);
                out_pos += 2;
            }
            
            /* Advance position, updating hash table for each position */
            for (size_t j = 1; j < match_len && i + j + LZP_CONTEXT <= src_size; j++) {
                if (i + j >= LZP_CONTEXT) {
                    uint32_t h2 = lzp_hash3(src + i + j - LZP_CONTEXT);
                    htable[h2] = (int32_t)(i + j);
                }
            }
            i += match_len;
        } else {
            /* Output literal */
            if (out_pos >= dst_cap) { free(htable); return 0; }
            if (src[i] == escape) {
                if (out_pos + 1 >= dst_cap) { free(htable); return 0; }
                dst[out_pos++] = escape;
                dst[out_pos++] = 0x00;
            } else {
                dst[out_pos++] = src[i];
            }
            i++;
        }
    }
    
    free(htable);
    
    /* Only return success if LZP actually reduced the size */
    if (out_pos >= src_size) return 0;
    
    return out_pos;
}

size_t mcx_lzp_decode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size)
{
    if (src_size < 1 || !dst || !src)
        return 0;
    
    int32_t* htable = (int32_t*)calloc(LZP_HASH_SIZE, sizeof(int32_t));
    if (!htable) return 0;
    memset(htable, 0xFF, LZP_HASH_SIZE * sizeof(int32_t));
    
    uint8_t escape = src[0];
    size_t in_pos = 1;
    size_t out_pos = 0;
    
    while (in_pos < src_size) {
        /* Update hash table if we have enough context */
        uint32_t h = 0;
        int32_t pred = -1;
        
        if (out_pos >= LZP_CONTEXT) {
            h = lzp_hash3(dst + out_pos - LZP_CONTEXT);
            pred = htable[h];
            htable[h] = (int32_t)out_pos;
        }
        
        if (src[in_pos] == escape) {
            in_pos++;
            if (in_pos >= src_size) { free(htable); return 0; }
            
            if (src[in_pos] == 0x00) {
                /* Escaped literal */
                if (out_pos >= dst_cap) { free(htable); return 0; }
                dst[out_pos++] = escape;
                in_pos++;
            } else if (src[in_pos] == 0xFE) {
                /* Long match */
                in_pos++;
                if (in_pos + 1 >= src_size) { free(htable); return 0; }
                uint16_t len16;
                memcpy(&len16, src + in_pos, 2);
                in_pos += 2;
                
                size_t match_len = (size_t)len16 + LZP_MIN_MATCH - 1;
                if (pred < 0 || out_pos + match_len > dst_cap) { free(htable); return 0; }
                
                /* Copy from predicted position */
                for (size_t j = 0; j < match_len; j++)
                    dst[out_pos + j] = dst[pred + j];
                
                /* Update hash table for positions within the match */
                for (size_t j = 1; j < match_len && out_pos + j >= LZP_CONTEXT; j++) {
                    uint32_t h2 = lzp_hash3(dst + out_pos + j - LZP_CONTEXT);
                    htable[h2] = (int32_t)(out_pos + j);
                }
                out_pos += match_len;
            } else if (src[in_pos] == 0xFF) {
                /* This shouldn't happen in our format - treat as error */
                free(htable);
                return 0;
            } else {
                /* Short match */
                size_t match_len = (size_t)src[in_pos] + LZP_MIN_MATCH - 1;
                in_pos++;
                
                if (pred < 0 || out_pos + match_len > dst_cap) { free(htable); return 0; }
                
                /* Copy from predicted position */
                for (size_t j = 0; j < match_len; j++)
                    dst[out_pos + j] = dst[pred + j];
                
                /* Update hash table for positions within the match */
                for (size_t j = 1; j < match_len && out_pos + j >= LZP_CONTEXT; j++) {
                    uint32_t h2 = lzp_hash3(dst + out_pos + j - LZP_CONTEXT);
                    htable[h2] = (int32_t)(out_pos + j);
                }
                out_pos += match_len;
            }
        } else {
            /* Literal byte */
            if (out_pos >= dst_cap) { free(htable); return 0; }
            dst[out_pos++] = src[in_pos++];
        }
    }
    
    free(htable);
    return out_pos;
}
