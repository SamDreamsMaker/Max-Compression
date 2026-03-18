/**
 * @file babel_dict_v2.c
 * @brief Babel Dictionary v2 — Smarter word replacement.
 *
 * v1 problem: escaping every non-word byte doubled their size.
 * v2 approach: Use a TWO-STREAM design:
 *   Stream 1: Token stream (words as indices, literals flagged)
 *   Stream 2: Literal bytes
 * 
 * Encoding:
 *   Token types:
 *     0x00       = end marker
 *     0x01-0xFE  = word index 0-253 (1-byte, most common words)
 *     0xFF       = literal run follows: [1 byte: length-1] then 'length' bytes from literal stream
 *
 * The token stream has low entropy (mostly small word indices),
 * and the literal stream is unmodified original bytes.
 * Both compress independently very well.
 *
 * BUT: for integration with MCX, we interleave them:
 *   [0xFF][len-1][len literal bytes] for non-word runs
 *   [word_idx] for dictionary words
 *
 * This way the output is a single stream that an entropy coder can handle.
 * The key insight: we DON'T escape individual bytes. We batch consecutive
 * non-word bytes into literal runs (only 2 bytes overhead per run, not per byte).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_WORDS       254    /* Max dictionary entries (fit in 1 byte: 0x01-0xFE) */
#define MAX_WORD_LEN    64
#define MIN_WORD_LEN    3      /* Minimum word length to include */
#define MIN_WORD_FREQ   3      /* Minimum frequency */
#define MAX_LIT_RUN     256    /* Max literal run length */

typedef struct {
    char word[MAX_WORD_LEN];
    uint16_t len;
    uint32_t freq;
} DictWord;

#define DICT_HASH_BITS  16
#define DICT_HASH_SIZE  (1u << DICT_HASH_BITS)
#define DICT_HASH_MASK  (DICT_HASH_SIZE - 1)

typedef struct {
    int32_t word_idx;  /* -1 = empty */
} HashSlot;

static inline int is_word_char(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'';
}

static inline uint32_t fnv_hash(const uint8_t* s, size_t len) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) { h ^= s[i]; h *= 0x01000193u; }
    return h;
}

static int cmp_freq_desc(const void* a, const void* b) {
    return (int)((const DictWord*)b)->freq - (int)((const DictWord*)a)->freq;
}

static void write_u32(uint8_t* p, uint32_t v) {
    p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}
static uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

/* ── Forward ───────────────────────────────────────────────────── */

size_t mcx_babel_dict2_forward(uint8_t* dst, size_t dst_cap,
                                const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 10) return 0;
    
    /* Phase 1: Count words */
    DictWord* all_words = calloc(65536, sizeof(DictWord));
    HashSlot* htab = calloc(DICT_HASH_SIZE, sizeof(HashSlot));
    if (!all_words || !htab) { free(all_words); free(htab); return 0; }
    for (size_t i = 0; i < DICT_HASH_SIZE; i++) htab[i].word_idx = -1;
    
    int num_words = 0;
    size_t i = 0;
    while (i < src_size) {
        if (is_word_char(src[i])) {
            size_t start = i;
            while (i < src_size && is_word_char(src[i]) && (i - start) < MAX_WORD_LEN - 1) i++;
            size_t wlen = i - start;
            
            if (wlen >= MIN_WORD_LEN) {
                uint32_t h = fnv_hash(src + start, wlen);
                uint32_t slot = h & DICT_HASH_MASK;
                int found = 0;
                for (int p = 0; p < 512; p++) {
                    uint32_t s = (slot + p) & DICT_HASH_MASK;
                    if (htab[s].word_idx == -1) {
                        if (num_words < 65536) {
                            htab[s].word_idx = num_words;
                            memcpy(all_words[num_words].word, src + start, wlen);
                            all_words[num_words].len = wlen;
                            all_words[num_words].freq = 1;
                            num_words++;
                        }
                        found = 1; break;
                    }
                    DictWord* w = &all_words[htab[s].word_idx];
                    if (w->len == wlen && memcmp(w->word, src + start, wlen) == 0) {
                        w->freq++;
                        found = 1; break;
                    }
                }
            }
        } else {
            i++;
        }
    }
    
    /* Phase 2: Sort, select profitable words */
    qsort(all_words, num_words, sizeof(DictWord), cmp_freq_desc);
    
    int dict_size = 0;
    for (int w = 0; w < num_words && dict_size < MAX_WORDS; w++) {
        if (all_words[w].freq < MIN_WORD_FREQ) break;
        /* Savings: each occurrence saves (word_len - 1) bytes (1 byte for code)
         * Cost: (1 + word_len) bytes in header for dictionary entry
         * Net: freq * (word_len - 1) - (1 + word_len) */
        int savings = (int)all_words[w].freq * ((int)all_words[w].len - 1) - (1 + (int)all_words[w].len);
        if (savings > 0) {
            dict_size++;
        }
    }
    
    if (dict_size < 5) { free(all_words); free(htab); return 0; }
    
    /* Phase 3: Rebuild hash for encoding */
    for (size_t j = 0; j < DICT_HASH_SIZE; j++) htab[j].word_idx = -1;
    for (int w = 0; w < dict_size; w++) {
        uint32_t h = fnv_hash((const uint8_t*)all_words[w].word, all_words[w].len);
        uint32_t slot = h & DICT_HASH_MASK;
        for (int p = 0; p < 512; p++) {
            uint32_t s = (slot + p) & DICT_HASH_MASK;
            if (htab[s].word_idx == -1) {
                htab[s].word_idx = w;
                break;
            }
        }
    }
    
    /* Phase 4: Encode
     * Header: "BD2\0" [4:orig_size] [1:dict_size] [dict entries: 1:len + bytes]
     * Data: interleaved word codes and literal runs
     */
    size_t pos = 0;
    
    /* Header */
    if (pos + 9 > dst_cap) { free(all_words); free(htab); return 0; }
    memcpy(dst, "BD2\0", 4); pos += 4;
    write_u32(dst + pos, (uint32_t)src_size); pos += 4;
    dst[pos++] = (uint8_t)dict_size;
    
    for (int w = 0; w < dict_size; w++) {
        if (pos + 1 + all_words[w].len > dst_cap) { free(all_words); free(htab); return 0; }
        dst[pos++] = (uint8_t)all_words[w].len;
        memcpy(dst + pos, all_words[w].word, all_words[w].len);
        pos += all_words[w].len;
    }
    
    /* Encode data */
    i = 0;
    /* Literal buffer */
    uint8_t lit_buf[MAX_LIT_RUN];
    int lit_count = 0;
    
    #define FLUSH_LITS() do { \
        if (lit_count > 0) { \
            if (pos + 2 + lit_count > dst_cap) { free(all_words); free(htab); return 0; } \
            dst[pos++] = 0xFF; \
            dst[pos++] = (uint8_t)(lit_count - 1); \
            memcpy(dst + pos, lit_buf, lit_count); \
            pos += lit_count; \
            lit_count = 0; \
        } \
    } while(0)
    
    while (i < src_size) {
        if (is_word_char(src[i])) {
            size_t start = i;
            while (i < src_size && is_word_char(src[i]) && (i - start) < MAX_WORD_LEN - 1) i++;
            size_t wlen = i - start;
            
            int word_idx = -1;
            if (wlen >= MIN_WORD_LEN) {
                uint32_t h = fnv_hash(src + start, wlen);
                uint32_t slot = h & DICT_HASH_MASK;
                for (int p = 0; p < 512; p++) {
                    uint32_t s = (slot + p) & DICT_HASH_MASK;
                    if (htab[s].word_idx == -1) break;
                    DictWord* w = &all_words[htab[s].word_idx];
                    if (w->len == wlen && memcmp(w->word, src + start, wlen) == 0) {
                        word_idx = htab[s].word_idx;
                        break;
                    }
                }
            }
            
            if (word_idx >= 0) {
                FLUSH_LITS();
                if (pos + 1 > dst_cap) { free(all_words); free(htab); return 0; }
                dst[pos++] = (uint8_t)(word_idx + 1); /* 0x01-0xFE */
            } else {
                /* Word not in dict — add to literals */
                for (size_t j = start; j < start + wlen; j++) {
                    lit_buf[lit_count++] = src[j];
                    if (lit_count == MAX_LIT_RUN) FLUSH_LITS();
                }
            }
        } else {
            lit_buf[lit_count++] = src[i++];
            if (lit_count == MAX_LIT_RUN) FLUSH_LITS();
        }
    }
    FLUSH_LITS();
    #undef FLUSH_LITS
    
    free(all_words);
    free(htab);
    
    /* Only return if we actually saved space */
    if (pos >= src_size) return 0;
    return pos;
}

/* ── Inverse ───────────────────────────────────────────────────── */

size_t mcx_babel_dict2_inverse(uint8_t* dst, size_t dst_cap,
                                const uint8_t* src, size_t src_size) {
    if (!dst || !src || src_size < 9) return 0;
    if (memcmp(src, "BD2\0", 4) != 0) return 0;
    
    uint32_t orig_size = read_u32(src + 4);
    uint8_t dict_size = src[8];
    if (dst_cap < orig_size) return 0;
    
    /* Read dictionary */
    DictWord* words = calloc(dict_size, sizeof(DictWord));
    if (!words) return 0;
    
    size_t pos = 9;
    for (int w = 0; w < dict_size; w++) {
        if (pos >= src_size) { free(words); return 0; }
        uint8_t wlen = src[pos++];
        if (pos + wlen > src_size) { free(words); return 0; }
        memcpy(words[w].word, src + pos, wlen);
        words[w].len = wlen;
        pos += wlen;
    }
    
    /* Decode */
    size_t out = 0;
    while (pos < src_size && out < orig_size) {
        uint8_t b = src[pos++];
        if (b == 0xFF) {
            /* Literal run */
            if (pos >= src_size) break;
            int run_len = (int)src[pos++] + 1;
            if (pos + run_len > src_size || out + run_len > dst_cap) { free(words); return 0; }
            memcpy(dst + out, src + pos, run_len);
            out += run_len;
            pos += run_len;
        } else if (b >= 0x01 && b <= 0xFE) {
            int idx = b - 1;
            if (idx >= dict_size) { free(words); return 0; }
            if (out + words[idx].len > dst_cap) { free(words); return 0; }
            memcpy(dst + out, words[idx].word, words[idx].len);
            out += words[idx].len;
        } else {
            /* 0x00 shouldn't appear */
            break;
        }
    }
    
    free(words);
    return (out == orig_size) ? out : 0;
}

/* ── Standalone test ───────────────────────────────────────────── */

#ifdef BABEL_DICT2_TEST

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    *out_size = fread(buf, 1, sz, f);
    fclose(f); return buf;
}

static double entropy_h0(const uint8_t* data, size_t n) {
    size_t freq[256] = {0};
    for (size_t i = 0; i < n; i++) freq[data[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / n;
        h -= p * __builtin_log2(p);
    }
    return h;
}

static void test(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src) return;
    
    size_t cap = src_size * 2 + 65536;
    uint8_t* enc = malloc(cap);
    uint8_t* dec = malloc(src_size + 1024);
    
    size_t enc_size = mcx_babel_dict2_forward(enc, cap, src, src_size);
    if (enc_size == 0) {
        printf("  %-25s %8zu → no savings\n", name, src_size);
        free(src); free(enc); free(dec); return;
    }
    
    size_t dec_size = mcx_babel_dict2_inverse(dec, src_size + 1024, enc, enc_size);
    int ok = (dec_size == src_size && memcmp(src, dec, src_size) == 0);
    
    double h0_orig = entropy_h0(src, src_size);
    double h0_enc = entropy_h0(enc, enc_size);
    double raw_ratio = (double)src_size / enc_size;
    double theo_ratio = (double)src_size / (enc_size * h0_enc / 8.0);
    
    printf("  %-25s %8zu → %8zu (%.2fx raw, H0:%.2f→%.2f, theo:%.2fx) %s\n",
           name, src_size, enc_size, raw_ratio, h0_orig, h0_enc, theo_ratio,
           ok ? "✓" : "✗");
    
    free(src); free(enc); free(dec);
}

int main(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  Babel Dict v2 — Literal-run encoding\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    test("corpora/alice29.txt", "alice29.txt");
    test("corpora/asyoulik.txt", "asyoulik.txt");
    test("corpora/cp.html", "cp.html");
    test("corpora/fields.c", "fields.c");
    test("corpora/lcet10.txt", "lcet10.txt");
    test("corpora/plrabn12.txt", "plrabn12.txt");
    test("corpora/kennedy.xls", "kennedy.xls");
    test("corpora/ptt5", "ptt5");
    test("corpora/silesia/dickens", "sil/dickens");
    test("corpora/silesia/xml", "sil/xml");
    test("corpora/silesia/samba", "sil/samba");
    test("corpora/silesia/webster", "sil/webster");
    test("corpora/silesia/nci", "sil/nci");
    test("corpora/silesia/mozilla", "sil/mozilla");
    test("lib/core.c", "core.c");
    test("README.md", "README.md");
    
    return 0;
}

#endif
