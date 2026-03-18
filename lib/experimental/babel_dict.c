/**
 * @file babel_dict.c
 * @brief Babel Dictionary Transform — Word-level text preprocessing.
 *
 * Algorithm:
 *   1. Scan input, extract "words" (sequences of letters [a-zA-Z] ≥ 2 chars)
 *   2. Count frequency of each unique word
 *   3. Sort by frequency (most frequent first)
 *   4. Assign indices: top 127 words get 1-byte codes (0x01-0x7F)
 *      Words 128-16510 get 2-byte codes (0x80+hi, lo)
 *   5. Encode: words → codes, non-word bytes → escaped (0x00, byte)
 *   6. Store dictionary in header
 *
 * Format:
 *   [4: magic "BDIC"]
 *   [4: original_size LE]
 *   [2: num_words LE]
 *   For each word: [1: len] [len: word bytes]
 *   [encoded data...]
 *
 * Escape mechanism:
 *   0x00 followed by a literal byte = that literal byte
 *   0x01-0x7F = word index 0-126 (1-byte code)
 *   0x80-0xFF = first byte of 2-byte word index
 */

#include "babel_dict.h"
#include <string.h>
#include <stdlib.h>

/* ── Configuration ─────────────────────────────────────────────── */
#define MAX_WORDS       16384  /* Max dictionary entries */
#define MAX_WORD_LEN    64     /* Max word length */
#define MIN_WORD_LEN    2      /* Min word length to consider */
#define MIN_WORD_FREQ   2      /* Min frequency to include */

/* ── Word entry ────────────────────────────────────────────────── */
typedef struct {
    char word[MAX_WORD_LEN];
    uint16_t len;
    uint32_t freq;
} DictWord;

/* ── Hash map for word counting ────────────────────────────────── */
#define DICT_HASH_BITS  16
#define DICT_HASH_SIZE  (1u << DICT_HASH_BITS)
#define DICT_HASH_MASK  (DICT_HASH_SIZE - 1)

typedef struct {
    uint32_t hash;
    int32_t word_idx;  /* -1 = empty */
} HashSlot;

static inline int is_alpha(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline uint32_t dict_hash(const uint8_t* s, size_t len) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) {
        h ^= s[i];
        h *= 0x01000193u;
    }
    return h;
}

/* ── Comparison for qsort (descending frequency) ──────────────── */
static int cmp_freq_desc(const void* a, const void* b) {
    const DictWord* wa = (const DictWord*)a;
    const DictWord* wb = (const DictWord*)b;
    if (wb->freq != wa->freq) return (int)(wb->freq - wa->freq);
    return strcmp(wa->word, wb->word);
}

/* ── Write LE uint16/uint32 ───────────────────────────────────── */
static inline void write_u16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}
static inline void write_u32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static inline uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ── Forward transform ─────────────────────────────────────────── */

size_t mcx_babel_dict_forward(uint8_t* dst, size_t dst_cap,
                               const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 4) return 0;
    
    /* Phase 1: Count words */
    DictWord* words = (DictWord*)calloc(MAX_WORDS, sizeof(DictWord));
    HashSlot* htab = (HashSlot*)calloc(DICT_HASH_SIZE, sizeof(HashSlot));
    if (!words || !htab) { free(words); free(htab); return 0; }
    
    /* Initialize hash table */
    for (size_t i = 0; i < DICT_HASH_SIZE; i++) htab[i].word_idx = -1;
    
    int num_words = 0;
    size_t i = 0;
    while (i < src_size) {
        if (is_alpha(src[i])) {
            size_t start = i;
            while (i < src_size && is_alpha(src[i]) && (i - start) < MAX_WORD_LEN - 1) i++;
            size_t wlen = i - start;
            
            if (wlen >= MIN_WORD_LEN) {
                uint32_t h = dict_hash(src + start, wlen);
                uint32_t slot = h & DICT_HASH_MASK;
                
                /* Linear probe */
                int found = 0;
                for (int probe = 0; probe < 256; probe++) {
                    uint32_t s = (slot + probe) & DICT_HASH_MASK;
                    if (htab[s].word_idx == -1) {
                        /* New word */
                        if (num_words < MAX_WORDS) {
                            htab[s].hash = h;
                            htab[s].word_idx = num_words;
                            memcpy(words[num_words].word, src + start, wlen);
                            words[num_words].word[wlen] = 0;
                            words[num_words].len = (uint16_t)wlen;
                            words[num_words].freq = 1;
                            num_words++;
                        }
                        found = 1;
                        break;
                    } else {
                        DictWord* w = &words[htab[s].word_idx];
                        if (w->len == wlen && memcmp(w->word, src + start, wlen) == 0) {
                            w->freq++;
                            found = 1;
                            break;
                        }
                    }
                }
                if (!found) {
                    /* Hash table full, skip this word */
                }
            }
        } else {
            i++;
        }
    }
    
    /* Phase 2: Sort by frequency, keep top entries */
    qsort(words, num_words, sizeof(DictWord), cmp_freq_desc);
    
    /* Only keep words with freq >= MIN_WORD_FREQ and that save space */
    int dict_size = 0;
    for (int w = 0; w < num_words && dict_size < MAX_WORDS; w++) {
        if (words[w].freq < MIN_WORD_FREQ) break;
        /* Cost of dictionary entry: 1 + word_len bytes in header
         * Savings per occurrence: word_len - code_len bytes
         * code_len: 1 byte for first 127, 2 bytes for rest */
        int code_len = (dict_size < 127) ? 1 : 2;
        int savings = (int)words[w].len - code_len;
        int total_savings = savings * (int)words[w].freq - (1 + (int)words[w].len);
        if (total_savings > 0) {
            dict_size++;
        } else {
            break;
        }
    }
    
    if (dict_size == 0) {
        free(words);
        free(htab);
        return 0; /* No useful dictionary */
    }
    
    /* Phase 3: Build lookup hash for encoding */
    /* Reset hash table for word → index mapping */
    for (size_t j = 0; j < DICT_HASH_SIZE; j++) htab[j].word_idx = -1;
    for (int w = 0; w < dict_size; w++) {
        uint32_t h = dict_hash((const uint8_t*)words[w].word, words[w].len);
        uint32_t slot = h & DICT_HASH_MASK;
        for (int probe = 0; probe < 256; probe++) {
            uint32_t s = (slot + probe) & DICT_HASH_MASK;
            if (htab[s].word_idx == -1) {
                htab[s].hash = h;
                htab[s].word_idx = w;
                break;
            }
        }
    }
    
    /* Phase 4: Write output */
    size_t pos = 0;
    
    /* Header: magic + orig_size + num_words + dictionary */
    if (pos + 10 > dst_cap) { free(words); free(htab); return 0; }
    memcpy(dst + pos, "BDIC", 4); pos += 4;
    write_u32(dst + pos, (uint32_t)src_size); pos += 4;
    write_u16(dst + pos, (uint16_t)dict_size); pos += 2;
    
    for (int w = 0; w < dict_size; w++) {
        if (pos + 1 + words[w].len > dst_cap) { free(words); free(htab); return 0; }
        dst[pos++] = (uint8_t)words[w].len;
        memcpy(dst + pos, words[w].word, words[w].len);
        pos += words[w].len;
    }
    
    /* Phase 5: Encode data */
    i = 0;
    while (i < src_size) {
        if (is_alpha(src[i])) {
            size_t start = i;
            while (i < src_size && is_alpha(src[i]) && (i - start) < MAX_WORD_LEN - 1) i++;
            size_t wlen = i - start;
            
            int word_idx = -1;
            if (wlen >= MIN_WORD_LEN) {
                uint32_t h = dict_hash(src + start, wlen);
                uint32_t slot = h & DICT_HASH_MASK;
                for (int probe = 0; probe < 256; probe++) {
                    uint32_t s = (slot + probe) & DICT_HASH_MASK;
                    if (htab[s].word_idx == -1) break;
                    DictWord* w = &words[htab[s].word_idx];
                    if (w->len == wlen && memcmp(w->word, src + start, wlen) == 0) {
                        word_idx = htab[s].word_idx;
                        break;
                    }
                }
            }
            
            if (word_idx >= 0 && word_idx < dict_size) {
                /* Encode as word reference */
                if (word_idx < 127) {
                    if (pos + 1 > dst_cap) { free(words); free(htab); return 0; }
                    dst[pos++] = (uint8_t)(word_idx + 1); /* 0x01-0x7F */
                } else {
                    if (pos + 2 > dst_cap) { free(words); free(htab); return 0; }
                    int idx = word_idx - 127;
                    dst[pos++] = 0x80 | (uint8_t)((idx >> 8) & 0x7F);
                    dst[pos++] = (uint8_t)(idx & 0xFF);
                }
            } else {
                /* Word not in dictionary — emit as escaped literal bytes */
                for (size_t j = start; j < start + wlen; j++) {
                    if (pos + 2 > dst_cap) { free(words); free(htab); return 0; }
                    dst[pos++] = 0x00;
                    dst[pos++] = src[j];
                }
            }
        } else {
            /* Non-alpha byte — always escaped */
            if (pos + 2 > dst_cap) { free(words); free(htab); return 0; }
            dst[pos++] = 0x00;
            dst[pos++] = src[i++];
        }
    }
    
    free(words);
    free(htab);
    return pos;
}

/* ── Inverse transform ─────────────────────────────────────────── */

size_t mcx_babel_dict_inverse(uint8_t* dst, size_t dst_cap,
                               const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 10) return 0;
    
    /* Read header */
    if (memcmp(src, "BDIC", 4) != 0) return 0;
    uint32_t orig_size = read_u32(src + 4);
    uint16_t num_words = read_u16(src + 8);
    
    if (dst_cap < orig_size) return 0;
    
    /* Read dictionary */
    DictWord* words = (DictWord*)calloc(num_words, sizeof(DictWord));
    if (!words) return 0;
    
    size_t pos = 10;
    for (int w = 0; w < num_words; w++) {
        if (pos >= src_size) { free(words); return 0; }
        uint8_t wlen = src[pos++];
        if (pos + wlen > src_size || wlen > MAX_WORD_LEN - 1) { free(words); return 0; }
        memcpy(words[w].word, src + pos, wlen);
        words[w].word[wlen] = 0;
        words[w].len = wlen;
        pos += wlen;
    }
    
    /* Decode */
    size_t out = 0;
    while (pos < src_size && out < orig_size) {
        uint8_t b = src[pos++];
        
        if (b == 0x00) {
            /* Escaped literal */
            if (pos >= src_size) break;
            if (out >= dst_cap) { free(words); return 0; }
            dst[out++] = src[pos++];
        } else if (b < 0x80) {
            /* 1-byte word reference */
            int idx = b - 1;
            if (idx >= num_words) { free(words); return 0; }
            if (out + words[idx].len > dst_cap) { free(words); return 0; }
            memcpy(dst + out, words[idx].word, words[idx].len);
            out += words[idx].len;
        } else {
            /* 2-byte word reference */
            if (pos >= src_size) break;
            int idx = ((b & 0x7F) << 8) | src[pos++];
            idx += 127;
            if (idx >= num_words) { free(words); return 0; }
            if (out + words[idx].len > dst_cap) { free(words); return 0; }
            memcpy(dst + out, words[idx].word, words[idx].len);
            out += words[idx].len;
        }
    }
    
    free(words);
    return (out == orig_size) ? out : 0;
}
