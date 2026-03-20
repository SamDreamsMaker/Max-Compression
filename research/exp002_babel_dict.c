/**
 * Experiment 002: Babel Dictionary Compression
 * 
 * HYPOTHESIS: English text has ~100 very frequent words/bigrams that account
 * for 50%+ of all bytes. Replacing them with single tokens (0x80-0xFF) before
 * BWT should dramatically improve BWT output quality:
 * - Fewer distinct "contexts" → better MTF
 * - More byte runs → better RLE2
 * - Smaller effective alphabet → better rANS
 * 
 * APPROACH:
 * 1. Scan input, count all 2-6 byte sequences
 * 2. Select top-N sequences by (freq × len) score (bytes saved)
 * 3. Replace them with tokens 0x80-0xFF (128 dictionary entries)
 * 4. Store dictionary in header (~200-500 bytes)
 * 5. Measure entropy reduction
 * 
 * KEY INSIGHT: This is NOT just a preprocessor — it changes the STRUCTURE
 * that BWT sees. BWT sorts suffixes, and shorter, more uniform suffixes
 * sort better.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define MAX_DICT_ENTRIES 128
#define MAX_WORD_LEN 16
#define MIN_WORD_LEN 2
#define HASH_SIZE (1 << 20) /* 1M entries */

typedef struct {
    uint8_t word[MAX_WORD_LEN];
    uint8_t len;
    uint32_t count;
    uint32_t score; /* count * (len - 1) = bytes saved */
} dict_entry_t;

typedef struct {
    uint32_t hash;
    uint32_t idx;
} hash_entry_t;

/* FNV-1a hash */
static uint32_t fnv_hash(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* ── Word/token extraction ──────────────────────────────────────── */

/* Check if byte is a "word" character (letters, common punctuation patterns) */
static int is_word_char(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'';
}

/* Extract words and frequent n-grams */
static size_t extract_candidates(const uint8_t* data, size_t size,
                                  dict_entry_t* entries, size_t max_entries) {
    /* Phase 1: Count all words */
    hash_entry_t* htab = calloc(HASH_SIZE, sizeof(hash_entry_t));
    dict_entry_t* all = calloc(max_entries * 100, sizeof(dict_entry_t));
    size_t n_entries = 0;
    
    size_t i = 0;
    while (i < size) {
        /* Try word extraction */
        if (is_word_char(data[i])) {
            size_t start = i;
            while (i < size && is_word_char(data[i]) && (i - start) < MAX_WORD_LEN)
                i++;
            size_t wlen = i - start;
            
            /* Also capture word+space (very common pattern) */
            for (int with_space = 0; with_space <= 1; with_space++) {
                size_t elen = wlen + with_space;
                if (with_space && (start + wlen >= size || data[start + wlen] != ' '))
                    continue;
                if (elen < MIN_WORD_LEN || elen > MAX_WORD_LEN)
                    continue;
                
                uint32_t h = fnv_hash(data + start, elen) & (HASH_SIZE - 1);
                /* Linear probe */
                int found = 0;
                for (int probe = 0; probe < 16; probe++) {
                    uint32_t slot = (h + probe) & (HASH_SIZE - 1);
                    if (htab[slot].hash == 0 && htab[slot].idx == 0) {
                        /* Empty slot */
                        if (n_entries < max_entries * 100) {
                            htab[slot].hash = fnv_hash(data + start, elen) | 1;
                            htab[slot].idx = n_entries;
                            memcpy(all[n_entries].word, data + start, elen);
                            all[n_entries].len = elen;
                            all[n_entries].count = 1;
                            n_entries++;
                        }
                        found = 1;
                        break;
                    }
                    if (htab[slot].idx < n_entries &&
                        all[htab[slot].idx].len == elen &&
                        memcmp(all[htab[slot].idx].word, data + start, elen) == 0) {
                        all[htab[slot].idx].count++;
                        found = 1;
                        break;
                    }
                }
                if (!found && n_entries < max_entries * 100) {
                    /* Hash collision, just add new entry */
                    all[n_entries].len = elen;
                    memcpy(all[n_entries].word, data + start, elen);
                    all[n_entries].count = 1;
                    n_entries++;
                }
            }
        } else {
            /* Also count common byte pairs/triples */
            for (int nglen = 2; nglen <= 4 && i + nglen <= size; nglen++) {
                /* Only for high-byte or common patterns */
                uint32_t h = fnv_hash(data + i, nglen) & (HASH_SIZE - 1);
                for (int probe = 0; probe < 8; probe++) {
                    uint32_t slot = (h + probe) & (HASH_SIZE - 1);
                    if (htab[slot].hash == 0 && htab[slot].idx == 0) {
                        if (n_entries < max_entries * 100) {
                            htab[slot].hash = fnv_hash(data + i, nglen) | 1;
                            htab[slot].idx = n_entries;
                            memcpy(all[n_entries].word, data + i, nglen);
                            all[n_entries].len = nglen;
                            all[n_entries].count = 1;
                            n_entries++;
                        }
                        break;
                    }
                    if (htab[slot].idx < n_entries &&
                        all[htab[slot].idx].len == (uint8_t)nglen &&
                        memcmp(all[htab[slot].idx].word, data + i, nglen) == 0) {
                        all[htab[slot].idx].count++;
                        break;
                    }
                }
            }
            i++;
        }
    }
    
    /* Phase 2: Score and select top entries */
    for (size_t j = 0; j < n_entries; j++) {
        all[j].score = all[j].count * (all[j].len - 1);
    }
    
    /* Selection sort for top-N */
    size_t selected = 0;
    for (size_t k = 0; k < max_entries && k < n_entries; k++) {
        size_t best = k;
        for (size_t j = k + 1; j < n_entries; j++) {
            if (all[j].score > all[best].score)
                best = j;
        }
        if (best != k) {
            dict_entry_t tmp = all[k];
            all[k] = all[best];
            all[best] = tmp;
        }
        if (all[k].score < 10) break; /* Not worth including */
        memcpy(&entries[selected], &all[k], sizeof(dict_entry_t));
        selected++;
    }
    
    free(htab);
    free(all);
    return selected;
}

/* ── Dictionary replacement ─────────────────────────────────────── */

/* Check if byte 0x80-0xFF is unused in original data */
static int find_unused_tokens(const uint8_t* data, size_t size,
                               uint8_t* tokens, int max_tokens) {
    uint8_t used[256] = {0};
    for (size_t i = 0; i < size; i++) used[data[i]] = 1;
    
    int n = 0;
    /* Prefer high bytes (0x80+) as they're less common in text */
    for (int b = 0x80; b <= 0xFF && n < max_tokens; b++) {
        if (!used[b]) tokens[n++] = (uint8_t)b;
    }
    /* Fall back to low control chars if needed */
    for (int b = 1; b < 0x20 && n < max_tokens; b++) {
        if (b == 0x0A || b == 0x0D || b == 0x09) continue; /* Keep newline/cr/tab */
        if (!used[b]) tokens[n++] = (uint8_t)b;
    }
    return n;
}

static size_t apply_dictionary(const uint8_t* in, size_t in_size,
                                uint8_t* out, size_t out_cap,
                                dict_entry_t* dict, size_t n_dict,
                                uint8_t* tokens) {
    size_t op = 0;
    size_t ip = 0;
    
    while (ip < in_size && op < out_cap) {
        int matched = 0;
        /* Try longest match first */
        for (int d = n_dict - 1; d >= 0; d--) {
            /* Sort by length desc for greedy longest-match */
        }
        /* Simple: try each dict entry */
        for (size_t d = 0; d < n_dict; d++) {
            if (ip + dict[d].len <= in_size &&
                memcmp(in + ip, dict[d].word, dict[d].len) == 0) {
                out[op++] = tokens[d];
                ip += dict[d].len;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            out[op++] = in[ip++];
        }
    }
    return op;
}

/* ── Entropy calculation ────────────────────────────────────────── */

static double calc_entropy(const uint8_t* data, size_t size) {
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

static int count_distinct(const uint8_t* data, size_t size) {
    uint8_t seen[256] = {0};
    for (size_t i = 0; i < size; i++) seen[data[i]] = 1;
    int n = 0;
    for (int i = 0; i < 256; i++) n += seen[i];
    return n;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== Babel Dictionary Experiment ===\n");
    printf("Input: %s (%zu bytes)\n", argv[1], size);
    printf("Original entropy: %.4f bpb\n", calc_entropy(data, size));
    printf("Distinct bytes: %d\n\n", count_distinct(data, size));
    
    /* Find unused tokens */
    uint8_t tokens[MAX_DICT_ENTRIES];
    int n_tokens = find_unused_tokens(data, size, tokens, MAX_DICT_ENTRIES);
    printf("Available tokens (unused bytes): %d\n", n_tokens);
    
    if (n_tokens < 10) {
        printf("Not enough unused bytes for dictionary. Aborting.\n");
        free(data);
        return 1;
    }
    
    /* Extract dictionary candidates */
    dict_entry_t dict[MAX_DICT_ENTRIES];
    size_t n_dict = extract_candidates(data, size, dict, n_tokens < MAX_DICT_ENTRIES ? n_tokens : MAX_DICT_ENTRIES);
    
    printf("\n=== Top 20 Dictionary Entries ===\n");
    size_t total_savings = 0;
    for (size_t i = 0; i < 20 && i < n_dict; i++) {
        printf("  0x%02X → '", tokens[i]);
        for (int j = 0; j < dict[i].len; j++) {
            uint8_t c = dict[i].word[j];
            if (c >= 32 && c < 127) putchar(c);
            else printf("\\x%02x", c);
        }
        printf("' (len=%d, count=%u, saves=%u bytes)\n",
               dict[i].len, dict[i].count, dict[i].score);
        total_savings += dict[i].score;
    }
    printf("\nTotal dictionary entries: %zu\n", n_dict);
    printf("Theoretical savings (greedy): %zu bytes (%.1f%%)\n",
           total_savings, 100.0 * total_savings / size);
    
    /* Apply dictionary */
    size_t out_cap = size;
    uint8_t* out = malloc(out_cap);
    size_t out_size = apply_dictionary(data, size, out, out_cap, dict, n_dict, tokens);
    
    /* Calculate dictionary header cost */
    size_t dict_header = 2; /* n_entries (2 bytes) */
    for (size_t i = 0; i < n_dict; i++) {
        dict_header += 1 + dict[i].len; /* token + len + word */
    }
    
    printf("\n=== After Dictionary Replacement ===\n");
    printf("Output size: %zu bytes (%.1f%% of original)\n", out_size, 100.0 * out_size / size);
    printf("Dictionary header cost: %zu bytes\n", dict_header);
    printf("Net size: %zu bytes (%.1f%% of original)\n", out_size + dict_header,
           100.0 * (out_size + dict_header) / size);
    printf("Entropy: %.4f bpb (was %.4f)\n", calc_entropy(out, out_size), calc_entropy(data, size));
    printf("Distinct bytes: %d (was %d)\n", count_distinct(out, out_size), count_distinct(data, size));
    
    /* Theoretical compressed size comparison */
    double orig_compressed = calc_entropy(data, size) * size / 8.0;
    double dict_compressed = calc_entropy(out, out_size) * out_size / 8.0 + dict_header;
    printf("\n=== Theoretical Compression (entropy × size) ===\n");
    printf("Original: %.0f bytes\n", orig_compressed);
    printf("After dict: %.0f bytes\n", dict_compressed);
    printf("Improvement: %.1f%%\n", 100.0 * (orig_compressed - dict_compressed) / orig_compressed);
    
    printf("\n=== BWT Benefit Estimate ===\n");
    /* Count runs (adjacent equal bytes) — proxy for BWT quality */
    size_t orig_runs = 0, dict_runs = 0;
    for (size_t i = 1; i < size; i++) if (data[i] != data[i-1]) orig_runs++;
    for (size_t i = 1; i < out_size; i++) if (out[i] != out[i-1]) dict_runs++;
    printf("Runs in original: %zu (%.2f avg run length)\n", orig_runs, (double)size / (orig_runs + 1));
    printf("Runs after dict:  %zu (%.2f avg run length)\n", dict_runs, (double)out_size / (dict_runs + 1));
    printf("Run reduction: %.1f%%\n", 100.0 * (1.0 - (double)dict_runs / orig_runs));
    
    /* Write transformed output to file if -o specified */
    if (argc >= 4 && strcmp(argv[2], "-o") == 0) {
        FILE* fout = fopen(argv[3], "wb");
        if (fout) {
            /* Write dict header: n_entries(2) + [token(1) + len(1) + word(len)]... */
            uint16_t n16 = (uint16_t)n_dict;
            fwrite(&n16, 2, 1, fout);
            for (size_t d = 0; d < n_dict; d++) {
                fwrite(&tokens[d], 1, 1, fout);
                fwrite(&dict[d].len, 1, 1, fout);
                fwrite(dict[d].word, 1, dict[d].len, fout);
            }
            /* Write transformed data */
            fwrite(out, 1, out_size, fout);
            fclose(fout);
            printf("\nWrote %zu bytes to %s (header=%zu + data=%zu)\n",
                   dict_header + out_size, argv[3], dict_header, out_size);
        }
    }
    
    free(data);
    free(out);
    return 0;
}
