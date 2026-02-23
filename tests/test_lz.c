/**
 * @file test_lz.c
 * @brief Round-trip tests for LZ77 engine (standard + HC mode).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../lib/lz/mcx_lz.h"
#include "../lib/lz_fast/mcx_lz_fast.h"

typedef size_t (*compress_fn)(void*, size_t, const void*, size_t, int);

static int test_rt(const char* name, const uint8_t* data, size_t size, compress_fn cfn, int param) {
    size_t comp_cap = mcx_lz_compress_bound(size);
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec  = (uint8_t*)malloc(size + 16);
    if (!comp || !dec) { printf("  [FAIL] %s — alloc\n", name); free(comp); free(dec); return 1; }

    size_t comp_size = cfn(comp, comp_cap, data, size, param);
    if (comp_size == 0 && size > 0) { printf("  [FAIL] %s — compress=0\n", name); free(comp); free(dec); return 1; }

    memset(dec, 0xCC, size + 16);
    size_t dec_size = mcx_lz_decompress(dec, size + 16, comp, comp_size, size);
    if (dec_size != size) { printf("  [FAIL] %s — dec %zu != %zu\n", name, dec_size, size); free(comp); free(dec); return 1; }
    if (memcmp(data, dec, size) != 0) {
        for (size_t i = 0; i < size; i++)
            if (data[i] != dec[i]) { printf("  [FAIL] %s — byte %zu: 0x%02X!=0x%02X\n", name, i, data[i], dec[i]); break; }
        free(comp); free(dec); return 1;
    }
    double ratio = size > 0 ? (double)size / comp_size : 0;
    printf("  [PASS] %-35s %8zu -> %8zu  (%.2fx)\n", name, size, comp_size, ratio);
    free(comp); free(dec); return 0;
}

static int test_speed(const char* name, const uint8_t* data, size_t size, compress_fn cfn, int param) {
    size_t comp_cap = mcx_lz_compress_bound(size);
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec  = (uint8_t*)malloc(size + 16);
    if (!comp || !dec) { free(comp); free(dec); return 1; }

    clock_t t0 = clock();
    size_t comp_size = cfn(comp, comp_cap, data, size, param);
    clock_t t1 = clock();
    size_t dec_size = mcx_lz_decompress(dec, size + 16, comp, comp_size, size);
    clock_t t2 = clock();

    double cms = (double)(t1-t0)*1000.0/CLOCKS_PER_SEC;
    double dms = (double)(t2-t1)*1000.0/CLOCKS_PER_SEC;
    double cmbps = cms > 0 ? (size/1048576.0)/(cms/1000.0) : 0;
    double dmbps = dms > 0 ? (size/1048576.0)/(dms/1000.0) : 0;

    int ok = (dec_size == size && memcmp(data, dec, size) == 0);
    printf("  [%s] %-35s %8zu -> %8zu  (%.2fx)  C:%.0f MB/s  D:%.0f MB/s\n",
           ok?"PASS":"FAIL", name, size, comp_size, (double)size/comp_size, cmbps, dmbps);
    free(comp); free(dec); return ok ? 0 : 1;
}

static int test_speed_fast(const char* name, const uint8_t* data, size_t size) {
    size_t comp_cap = mcx_lz_compress_bound(size);
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec  = (uint8_t*)malloc(size + 64);
    if (!comp || !dec) { free(comp); free(dec); return 1; }

    mcx_lz_fast_ctx ctx;
    mcx_lz_fast_init(&ctx);

    clock_t t0 = clock();
    size_t comp_size = mcx_lz_fast_compress(comp, comp_cap, data, size, &ctx);
    clock_t t1 = clock();
    size_t dec_size = mcx_lz_fast_decompress(dec, size + 64, comp, comp_size);
    clock_t t2 = clock();

    double cms = (double)(t1-t0)*1000.0/CLOCKS_PER_SEC;
    double dms = (double)(t2-t1)*1000.0/CLOCKS_PER_SEC;
    double cmbps = cms > 0 ? (size/1048576.0)/(cms/1000.0) : 0;
    double dmbps = dms > 0 ? (size/1048576.0)/(dms/1000.0) : 0;

    int ok = (dec_size == size && memcmp(data, dec, size) == 0);
    printf("  [%s] %-35s %8zu -> %8zu  (%.2fx)  C:%.0f MB/s  D:%.0f MB/s\n",
           ok?"PASS":"FAIL", name, size, comp_size, (double)size/comp_size, cmbps, dmbps);
    free(comp); free(dec); return ok ? 0 : 1;
}

int main(void) {
    int f = 0;
    printf("=== LZ77 Engine Tests (Standard + HC) ===\n\n");
    printf("--- Standard Mode (accel=1) ---\n");

    f += test_rt("Empty", (const uint8_t*)"", 0, mcx_lz_compress, 1);
    f += test_rt("Tiny (3 bytes)", (const uint8_t*)"abc", 3, mcx_lz_compress, 1);
    { const char* s = "Hello, World! Hello, World! Hello, World!";
      f += test_rt("Hello x3", (const uint8_t*)s, strlen(s), mcx_lz_compress, 1); }
    { size_t n=100000; uint8_t* z=calloc(n,1); f += test_rt("100K zeros", z, n, mcx_lz_compress, 1); free(z); }
    { size_t n=100000; uint8_t* p=malloc(n); for(size_t i=0;i<n;i++) p[i]=(uint8_t)(i%4);
      f += test_rt("100K pattern", p, n, mcx_lz_compress, 1); free(p); }
    { const char* lorem = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
      f += test_rt("Lorem ipsum", (const uint8_t*)lorem, strlen(lorem), mcx_lz_compress, 1); }
    { size_t n=50000; uint8_t* r=malloc(n); srand(42); for(size_t i=0;i<n;i++) r[i]=(uint8_t)(rand()&0xFF);
      f += test_rt("50K random", r, n, mcx_lz_compress, 1); free(r); }
    { size_t n=1048576; uint8_t* d=malloc(n); for(size_t i=0;i<n;i++) d[i]=(uint8_t)((i*7+i/256)&0xFF);
      f += test_rt("1MB structured", d, n, mcx_lz_compress, 1); free(d); }

    printf("\n--- HC Mode (lazy matching) ---\n");
    { size_t n=100000; uint8_t* z=calloc(n,1); f += test_rt("HC: 100K zeros", z, n, mcx_lz_compress_hc, 9); free(z); }
    { size_t n=100000; uint8_t* p=malloc(n); for(size_t i=0;i<n;i++) p[i]=(uint8_t)(i%4);
      f += test_rt("HC: 100K pattern", p, n, mcx_lz_compress_hc, 9); free(p); }
    { size_t n=1048576; uint8_t* d=malloc(n); for(size_t i=0;i<n;i++) d[i]=(uint8_t)((i*7+i/256)&0xFF);
      f += test_rt("HC: 1MB structured", d, n, mcx_lz_compress_hc, 9); free(d); }

    printf("\n--- Speed Benchmark (10MB) ---\n");
    { size_t n=10*1048576; uint8_t* d=malloc(n); srand(123);
      for(size_t i=0;i<n;i++) d[i]=(uint8_t)(rand()&0xFF);
      f += test_speed("10MB random (std)", d, n, mcx_lz_compress, 1);
      f += test_speed("10MB random (HC)", d, n, mcx_lz_compress_hc, 9);
      free(d);
    }
    { size_t n=10*1048576; uint8_t* d=malloc(n);
      for(size_t i=0;i<n;i++) d[i]=(uint8_t)((i*7+i/256)&0xFF);
      f += test_speed("10MB structured (std)", d, n, mcx_lz_compress, 1);
      f += test_speed("10MB structured (HC)", d, n, mcx_lz_compress_hc, 9);
      free(d);
    }
    
    printf("\n--- Speed Benchmark FAST Mathematical (10MB) ---\n");
    { size_t n=10*1048576; uint8_t* d=malloc(n);
      for(size_t i=0;i<n;i++) d[i]=(uint8_t)((i*7+i/256)&0xFF);
      f += test_speed_fast("10MB structured (FAST)", d, n);
      free(d);
    }

    printf("\n%s: %d failure(s)\n", f==0 ? "ALL PASSED" : "FAILED", f);
    return f;
}
