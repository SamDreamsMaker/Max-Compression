/**
 * @file bench_fast.c
 * @brief Phase J — Native C Speed Harness for mcx_lz_fast
 *
 * Measures true hardware throughput of the fast engine without
 * Python ctypes overhead. Loads the Calgary corpus once from disk,
 * then runs N iterations of compress + decompress to get stable MB/s.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* High-resolution timer: QueryPerformanceCounter on Windows, clock_gettime elsewhere */
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
static double mcx_now_sec(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}
#else
#  include <time.h>
static double mcx_now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

#include "../lib/lz_fast/mcx_lz_fast.h"
#include "../lib/lz/mcx_lz.h"   /* for mcx_lz_compress_bound (same bound formula) */

#define ITERS   5
#define MB(n)   ((n) / (1024.0 * 1024.0))

/* ── Load a whole directory into one flat buffer ──────────────────── */
static uint8_t* load_corpus(const char* dir, size_t* out_size) {
    /* Simple approach: load corpus/calgary/* concatenated */
    static const char* files[] = {
        "bib","book1","book2","geo","news","obj1","obj2",
        "paper1","paper2","paper3","paper4","paper5","paper6",
        "pic","progc","progl","progp","trans", NULL
    };

    /* Pre-pass to get total size */
    size_t total = 0;
    char path[512];
    for (int i = 0; files[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); total += (size_t)ftell(f); fclose(f);
    }

    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf) return NULL;

    size_t offset = 0;
    for (int i = 0; files[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        size_t sz = (size_t)ftell(f);
        rewind(f);
        fread(buf + offset, 1, sz, f);
        fclose(f);
        offset += sz;
    }

    *out_size = offset;
    return buf;
}

int main(int argc, char** argv) {
    const char* corpus_dir = (argc > 1) ? argv[1] : "corpus/calgary";

    printf("=== mcx_lz_fast Phase J — Native Speed Harness ===\n");
    printf("Corpus: %s\n\n", corpus_dir);

    size_t src_size = 0;
    uint8_t* src = load_corpus(corpus_dir, &src_size);
    if (!src || src_size == 0) {
        fprintf(stderr, "ERROR: could not load corpus from '%s'\n", corpus_dir);
        fprintf(stderr, "       Run from the MaxCompression root directory.\n");
        return 1;
    }
    printf("Loaded: %.2f MB\n\n", MB(src_size));

    size_t dst_cap = src_size + src_size / 4 + 64;
    uint8_t* comp_buf = (uint8_t*)malloc(dst_cap);
    uint8_t* dec_buf  = (uint8_t*)malloc(src_size + 64);
    if (!comp_buf || !dec_buf) { fputs("OOM\n", stderr); return 1; }

    mcx_lz_fast_ctx ctx;
    mcx_lz_fast_init(&ctx);

    /* ── Warm-up ───────────────────────────────────────────────────── */
    size_t comp_size = mcx_lz_fast_compress(comp_buf, dst_cap, src, src_size, &ctx);
    if (comp_size == 0) { fprintf(stderr, "Compress returned 0!\n"); return 1; }
    size_t dec_size = mcx_lz_fast_decompress(dec_buf, src_size + 64, comp_buf, comp_size);
    if (dec_size != src_size || memcmp(src, dec_buf, src_size) != 0) {
        fprintf(stderr, "ROUND-TRIP FAIL: dec_size=%zu expected=%zu\n", dec_size, src_size);
        return 1;
    }
    printf("[OK] Round-trip verified.\n");
    printf("     Compressed:   %zu bytes → %.2fx ratio\n\n", comp_size, (double)src_size / comp_size);

    /* ── Timing loop ───────────────────────────────────────────────── */
    double comp_total = 0.0, decomp_total = 0.0;
    for (int i = 0; i < ITERS; i++) {
        mcx_lz_fast_init(&ctx);  /* Fresh context each iter */

        double t0 = mcx_now_sec();
        comp_size = mcx_lz_fast_compress(comp_buf, dst_cap, src, src_size, &ctx);
        double t1 = mcx_now_sec();
        dec_size  = mcx_lz_fast_decompress(dec_buf, src_size + 64, comp_buf, comp_size);
        double t2 = mcx_now_sec();

        (void)dec_size;
        comp_total  += t1 - t0;
        decomp_total += t2 - t1;
    }

    double avg_comp   = comp_total  / ITERS;
    double avg_decomp = decomp_total / ITERS;
    double comp_mbs   = MB(src_size) / avg_comp;
    double decomp_mbs = MB(src_size) / avg_decomp;

    printf("=== Results (%d iterations, %.2f MB) ===\n", ITERS, MB(src_size));
    printf("  Compression   : %7.0f MB/s  (avg %.1f ms)\n", comp_mbs,   avg_comp   * 1000.0);
    printf("  Decompression : %7.0f MB/s  (avg %.1f ms)\n", decomp_mbs, avg_decomp * 1000.0);
    printf("  Ratio         : %.3fx\n", (double)src_size / comp_size);
    printf("\n");

    /* ── Comparison reference (existing mcx standard) ─────────────── */
    printf("  [For reference: LZ4-fast typically %d-%d MB/s comp / %d-%d MB/s decomp]\n",
           500, 800, 1500, 2200);

    free(src); free(comp_buf); free(dec_buf);
    return 0;
}
