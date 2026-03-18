/**
 * @file test_stress.c
 * @brief Stress tests for edge cases and performance.
 * 
 * Tests:
 * 1. All-zeros (highly compressible)
 * 2. Random data (incompressible)
 * 3. Alternating patterns
 * 4. Single byte repeated
 * 5. Large block (1MB)
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define OK(msg) printf("  %-40s OK\n", msg)

static void fill_random(uint8_t* buf, size_t n, uint32_t seed) {
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (uint8_t)(seed >> 16);
    }
}

static void test_roundtrip(const char* name, const uint8_t* data, size_t size, int level) {
    size_t bound = mcx_compress_bound(size);
    uint8_t* comp = malloc(bound);
    uint8_t* dec = malloc(size + 1);
    assert(comp && dec);
    
    size_t cs = mcx_compress(comp, bound, data, size, level);
    assert(!mcx_is_error(cs));
    
    size_t ds = mcx_decompress(dec, size + 1, comp, cs);
    assert(!mcx_is_error(ds));
    assert(ds == size);
    assert(memcmp(data, dec, size) == 0);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "%s L%d: %zu→%zu (%.1fx)", name, level, size, cs, (double)size/cs);
    OK(msg);
    
    free(comp);
    free(dec);
}

int main(void) {
    printf("Stress tests\n");
    
    /* 1. All zeros — 64KB */
    {
        size_t n = 65536;
        uint8_t* buf = calloc(n, 1);
        test_roundtrip("zeros-64K", buf, n, 1);
        test_roundtrip("zeros-64K", buf, n, 12);
        test_roundtrip("zeros-64K", buf, n, 20);
        free(buf);
    }
    
    /* 2. Random data — 64KB */
    {
        size_t n = 65536;
        uint8_t* buf = malloc(n);
        fill_random(buf, n, 42);
        test_roundtrip("random-64K", buf, n, 1);
        test_roundtrip("random-64K", buf, n, 12);
        test_roundtrip("random-64K", buf, n, 20);
        free(buf);
    }
    
    /* 3. Alternating 0xAA/0x55 — 32KB */
    {
        size_t n = 32768;
        uint8_t* buf = malloc(n);
        for (size_t i = 0; i < n; i++) buf[i] = (i & 1) ? 0x55 : 0xAA;
        test_roundtrip("alt-32K", buf, n, 1);
        test_roundtrip("alt-32K", buf, n, 12);
        free(buf);
    }
    
    /* 4. Single byte — 16KB */
    {
        size_t n = 16384;
        uint8_t* buf = malloc(n);
        memset(buf, 0x42, n);
        test_roundtrip("single-16K", buf, n, 1);
        test_roundtrip("single-16K", buf, n, 20);
        free(buf);
    }
    
    /* 5. Large block — 1MB */
    {
        size_t n = 1024 * 1024;
        uint8_t* buf = malloc(n);
        /* Mix: first half zeros, second half random */
        memset(buf, 0, n / 2);
        fill_random(buf + n / 2, n / 2, 123);
        test_roundtrip("mixed-1M", buf, n, 6);
        test_roundtrip("mixed-1M", buf, n, 12);
        test_roundtrip("mixed-1M", buf, n, 20);
        free(buf);
    }
    
    /* 6. Tiny data — 1 byte */
    {
        uint8_t one = 0xFF;
        test_roundtrip("tiny-1B", &one, 1, 1);
        test_roundtrip("tiny-1B", &one, 1, 20);
    }
    
    /* 7. Repetitive text pattern */
    {
        const char* pattern = "the quick brown fox jumps over the lazy dog ";
        size_t plen = strlen(pattern);
        size_t n = plen * 1000;
        uint8_t* buf = malloc(n);
        for (size_t i = 0; i < n; i++) buf[i] = pattern[i % plen];
        test_roundtrip("text-repeat", buf, n, 6);
        test_roundtrip("text-repeat", buf, n, 9);
        test_roundtrip("text-repeat", buf, n, 20);
        free(buf);
    }
    
    printf("\nAll stress tests passed!\n");
    return 0;
}
