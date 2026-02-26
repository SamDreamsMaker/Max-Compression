/**
 * @file test_mcx12_bwt.c
 * @brief Reproduces VERIFY FAIL for mcx-12 on Canterbury-type data.
 *
 * Tests all Canterbury file types at L12 (BWT+MTF+RLE+rANS path)
 * using synthetic data that mimics each file's characteristics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/maxcomp/maxcomp.h"

static int roundtrip(const char* name, const uint8_t* src, size_t n, int level)
{
    size_t cap  = mcx_compress_bound(n);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec  = (uint8_t*)malloc(n + 64);
    if (!comp || !dec) { free(comp); free(dec); return 0; }

    size_t cs = mcx_compress(comp, cap, src, n, level);
    if (mcx_is_error(cs)) {
        printf("  [ERR]  %-36s  compress error 0x%zx\n", name, cs);
        free(comp); free(dec); return 1;
    }

    size_t ds = mcx_decompress(dec, n + 64, comp, cs);
    if (ds != n) {
        printf("  [FAIL] %-36s  dec_size %zu != orig %zu\n", name, ds, n);
        free(comp); free(dec); return 1;
    }
    if (memcmp(src, dec, n) != 0) {
        /* find first mismatch byte */
        size_t pos = 0;
        while (pos < n && src[pos] == dec[pos]) pos++;
        printf("  [FAIL] %-36s  mismatch at byte %zu (0x%02X vs 0x%02X)\n",
               name, pos, src[pos], dec[pos]);
        free(comp); free(dec); return 1;
    }
    printf("  [OK]   %-36s  %6zu -> %6zu (%.2fx)\n",
           name, n, cs, (double)n / (double)cs);
    free(comp); free(dec); return 0;
}

/* Try to open a real Canterbury file if available, else use synthetic data */
static int test_file_or_synth(const char* label, const char* filepath,
                               const uint8_t* synth, size_t synth_n, int level)
{
    FILE* f = fopen(filepath, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t n = (size_t)ftell(f);
        rewind(f);
        uint8_t* buf = (uint8_t*)malloc(n);
        if (buf) {
            fread(buf, 1, n, f);
            fclose(f);
            int r = roundtrip(label, buf, n, level);
            free(buf);
            return r;
        }
        fclose(f);
    }
    /* Fall back to synthetic */
    return roundtrip(label, synth, synth_n, level);
}

int main(void)
{
    int fail = 0;
    const int LEVEL = 12;
    printf("=== mcx-%d BWT round-trip (Canterbury types) ===\n\n", LEVEL);

    /* 1. English text (alice29 style) */
    {
        const char chunk[] =
            "Alice was beginning to get very tired of sitting by her sister "
            "on the bank, and of having nothing to do: once or twice she had "
            "peeped into the book her sister was reading, but it had no pictures "
            "or conversations in it. 'What is the use of a book,' thought Alice, "
            "'without pictures or conversations?' ";
        size_t clen = strlen(chunk);
        size_t n = 150000;
        uint8_t* d = (uint8_t*)malloc(n);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)chunk[i % clen];
        fail += test_file_or_synth("alice29.txt",
            "benchmarks/corpus/canterbury/alice29.txt", d, n, LEVEL);
        free(d);
    }

    /* 2. Shakespeare (asyoulik style — similar to text) */
    {
        size_t n = 125000;
        uint8_t* d = (uint8_t*)malloc(n);
        const char chunk[] = "All the world's a stage, and all the men and women "
            "merely players: they have their exits and their entrances; and one "
            "man in his time plays many parts. ";
        size_t clen = strlen(chunk);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)chunk[i % clen];
        fail += test_file_or_synth("asyoulik.txt",
            "benchmarks/corpus/canterbury/asyoulik.txt", d, n, LEVEL);
        free(d);
    }

    /* 3. HTML (cp.html style) */
    {
        size_t n = 24603;
        uint8_t* d = (uint8_t*)malloc(n);
        const char chunk[] = "<html><head><title>Test</title></head><body>"
            "<p>This is a paragraph with some text content.</p>"
            "<ul><li>Item one</li><li>Item two</li></ul></body></html>";
        size_t clen = strlen(chunk);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)chunk[i % clen];
        fail += test_file_or_synth("cp.html",
            "benchmarks/corpus/canterbury/cp.html", d, n, LEVEL);
        free(d);
    }

    /* 4. Source code (fields.c style) */
    {
        size_t n = 11150;
        uint8_t* d = (uint8_t*)malloc(n);
        const char chunk[] =
            "static int compute(int* arr, int n) {\n"
            "    int sum = 0;\n"
            "    for (int i = 0; i < n; i++) {\n"
            "        sum += arr[i];\n"
            "    }\n"
            "    return sum;\n"
            "}\n";
        size_t clen = strlen(chunk);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)chunk[i % clen];
        fail += test_file_or_synth("fields.c",
            "benchmarks/corpus/canterbury/fields.c", d, n, LEVEL);
        free(d);
    }

    /* 5. Binary/executable (sum style — mixed bytes) */
    {
        size_t n = 38240;
        uint8_t* d = (uint8_t*)malloc(n);
        srand(12345);
        /* ELF-like: mostly low bytes with some high-entropy sections */
        for (size_t i = 0; i < n; i++) {
            if (i < 64) d[i] = (uint8_t)(i & 0xFF); /* header */
            else if (i % 256 < 200) d[i] = (uint8_t)(rand() % 16); /* code */
            else d[i] = (uint8_t)(rand() & 0xFF); /* data */
        }
        fail += test_file_or_synth("sum (binary)",
            "benchmarks/corpus/canterbury/sum", d, n, LEVEL);
        free(d);
    }

    /* 6. Fax/bitmap (ptt5 style — mostly 0x00 and 0xFF runs) */
    {
        size_t n = 513216;
        uint8_t* d = (uint8_t*)malloc(n);
        /* Simulate bi-level fax: alternating runs of 0x00 and 0xFF */
        size_t pos = 0;
        int val = 0;
        srand(99);
        while (pos < n) {
            size_t run = 4 + (size_t)(rand() % 120);
            if (pos + run > n) run = n - pos;
            memset(d + pos, val ? 0xFF : 0x00, run);
            pos += run;
            val = 1 - val;
        }
        fail += test_file_or_synth("ptt5 (fax bitmap)",
            "benchmarks/corpus/canterbury/ptt5", d, n, LEVEL);
        free(d);
    }

    /* 7. Spreadsheet binary (kennedy.xls style — structured binary) */
    {
        size_t n = 1029744;
        uint8_t* d = (uint8_t*)malloc(n);
        for (size_t i = 0; i < n; i++) {
            /* XLS: mostly zeros with periodic non-zero structure */
            if (i % 512 < 8) d[i] = (uint8_t)(i & 0xFF);
            else if (i % 64 == 0) d[i] = 0x09; /* tab-like separator */
            else d[i] = 0x00;
        }
        fail += test_file_or_synth("kennedy.xls (spreadsheet binary)",
            "benchmarks/corpus/canterbury/kennedy.xls", d, n, LEVEL);
        free(d);
    }

    /* 8. Long English text (lcet10 / plrabn12 style) */
    {
        size_t n = 426754;
        uint8_t* d = (uint8_t*)malloc(n);
        const char chunk[] =
            "In the beginning God created the heavens and the earth. "
            "Now the earth was formless and empty, darkness was over the surface "
            "of the deep, and the Spirit of God was hovering over the waters. ";
        size_t clen = strlen(chunk);
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)chunk[i % clen];
        fail += test_file_or_synth("lcet10.txt",
            "benchmarks/corpus/canterbury/lcet10.txt", d, n, LEVEL);
        free(d);
    }

    printf("\n%s: %d failure(s)\n",
           fail == 0 ? "ALL PASSED" : "FAILED", fail);
    return fail;
}
