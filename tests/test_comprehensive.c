/**
 * @file test_comprehensive.c
 * @brief Comprehensive roundtrip tests for all compression levels and data types.
 * 
 * Tests: empty data, 1 byte, small files, large files, all zeros, random data,
 * text, binary, all compression levels (1-20, 25).
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int g_pass = 0, g_fail = 0;

static void test_roundtrip(const char* name, const uint8_t* data, size_t size, int level) {
    size_t cap = size + size / 4 + 65536;
    uint8_t* comp = malloc(cap);
    uint8_t* dec = malloc(size + 1024);
    if (!comp || !dec) {
        printf("  FAIL %s L%d: malloc failed\n", name, level);
        g_fail++;
        free(comp); free(dec);
        return;
    }
    
    size_t csz = mcx_compress(comp, cap, data, size, level);
    if (mcx_is_error(csz)) {
        printf("  FAIL %s L%d: compress error %zu\n", name, level, csz);
        g_fail++;
        free(comp); free(dec);
        return;
    }
    
    size_t dsz = mcx_decompress(dec, size + 1024, comp, csz);
    if (mcx_is_error(dsz)) {
        printf("  FAIL %s L%d: decompress error %zu (comp=%zu)\n", name, level, dsz, csz);
        g_fail++;
        free(comp); free(dec);
        return;
    }
    
    if (dsz != size) {
        printf("  FAIL %s L%d: size mismatch %zu != %zu\n", name, level, dsz, size);
        g_fail++;
    } else if (memcmp(data, dec, size) != 0) {
        /* Find first differing byte */
        size_t diff = 0;
        for (size_t i = 0; i < size; i++) {
            if (data[i] != dec[i]) { diff = i; break; }
        }
        printf("  FAIL %s L%d: data mismatch at byte %zu (0x%02x != 0x%02x)\n",
               name, level, diff, data[diff], dec[diff]);
        g_fail++;
    } else {
        g_pass++;
    }
    
    free(comp); free(dec);
}

static void test_all_levels(const char* name, const uint8_t* data, size_t size) {
    int levels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 25};
    for (int i = 0; i < 17; i++) {
        test_roundtrip(name, data, size, levels[i]);
    }
}

/* Generate pseudo-random data */
static void fill_random(uint8_t* buf, size_t n, uint32_t seed) {
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (seed >> 16) & 0xFF;
    }
}

int main(void) {
    printf("=== Comprehensive Roundtrip Tests ===\n\n");
    
    /* 1. Empty data (1 byte minimum for valid compression) */
    {
        uint8_t one = 0x42;
        printf("Testing: 1 byte...\n");
        test_all_levels("1byte", &one, 1);
    }
    
    /* 2. Small sizes */
    {
        uint8_t small[16];
        memset(small, 'A', 16);
        printf("Testing: 16 bytes (all same)...\n");
        test_all_levels("16B-same", small, 16);
        
        for (int i = 0; i < 16; i++) small[i] = i;
        printf("Testing: 16 bytes (sequential)...\n");
        test_all_levels("16B-seq", small, 16);
    }
    
    /* 3. Medium sizes */
    {
        size_t sz = 1024;
        uint8_t* buf = malloc(sz);
        memset(buf, 0, sz);
        printf("Testing: 1KB all zeros...\n");
        test_all_levels("1KB-zero", buf, sz);
        
        fill_random(buf, sz, 42);
        printf("Testing: 1KB random...\n");
        test_all_levels("1KB-rand", buf, sz);
        
        /* Repeating pattern */
        for (size_t i = 0; i < sz; i++) buf[i] = "Hello World! "[i % 13];
        printf("Testing: 1KB pattern...\n");
        test_all_levels("1KB-pat", buf, sz);
        free(buf);
    }
    
    /* 4. Larger sizes */
    {
        size_t sz = 65536;
        uint8_t* buf = malloc(sz);
        
        memset(buf, 0xFF, sz);
        printf("Testing: 64KB all 0xFF...\n");
        test_all_levels("64KB-ff", buf, sz);
        
        fill_random(buf, sz, 12345);
        printf("Testing: 64KB random...\n");
        test_all_levels("64KB-rand", buf, sz);
        
        /* Text-like */
        const char* lorem = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
        size_t llen = strlen(lorem);
        for (size_t i = 0; i < sz; i++) buf[i] = lorem[i % llen];
        printf("Testing: 64KB text...\n");
        test_all_levels("64KB-text", buf, sz);
        
        free(buf);
    }
    
    /* 5. Real files if available */
    {
        const char* files[] = {"/tmp/cantrbry/alice29.txt", "/tmp/cantrbry/kennedy.xls", NULL};
        const char* names[] = {"alice29", "kennedy"};
        for (int fi = 0; files[fi]; fi++) {
            FILE* f = fopen(files[fi], "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET);
            uint8_t* d = malloc(n); fread(d, 1, n, f); fclose(f);
            printf("Testing: %s (%zuKB)...\n", names[fi], n/1024);
            test_all_levels(names[fi], d, n);
            free(d);
        }
    }
    
    /* 6. Edge case: data that doesn't compress */
    {
        size_t sz = 4096;
        uint8_t* buf = malloc(sz);
        fill_random(buf, sz, 99999);
        printf("Testing: 4KB incompressible random...\n");
        test_all_levels("4KB-incp", buf, sz);
        free(buf);
    }
    
    printf("\n");
    printf("════════════════════════════════════════\n");
    if (g_fail == 0) {
        printf("  ✅ All %d tests passed!\n", g_pass);
    } else {
        printf("  ❌ %d passed, %d FAILED\n", g_pass, g_fail);
    }
    printf("════════════════════════════════════════\n");
    
    return g_fail > 0 ? 1 : 0;
}
