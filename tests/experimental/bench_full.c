/**
 * Comprehensive benchmark: all MCX strategies on Canterbury corpus.
 * L3 (LZ77), L12 (BWT+CM-rANS), L20 (Babel XOR+rANS)
 * Plus: Babel Stride-Delta → MCX L12, and standalone Babel Dict.
 */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Include stride and dict transforms directly for testing */
#include "../lib/babel/babel_stride.h"
#include "../lib/babel/babel_dict.h"

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

typedef struct {
    const char* name;
    size_t orig_size;
    size_t comp_size;
    double ratio;
    double time_ms;
    int ok;
} result_t;

static result_t bench_mcx(const char* label, const uint8_t* data, size_t size, int level) {
    result_t r = {label, size, 0, 0, 0, 0};
    size_t cap = mcx_compress_bound(size) + 131072; /* extra for babel overhead */
    uint8_t* comp = malloc(cap);
    uint8_t* dec = malloc(size);
    if (!comp || !dec) { free(comp); free(dec); return r; }

    double t0 = now_sec();
    size_t cs = mcx_compress(comp, cap, data, size, level);
    r.time_ms = (now_sec() - t0) * 1000.0;

    if (mcx_is_error(cs)) { free(comp); free(dec); return r; }
    r.comp_size = cs;
    r.ratio = (double)size / cs;

    /* Verify roundtrip */
    size_t ds = mcx_decompress(dec, size, comp, cs);
    if (!mcx_is_error(ds) && ds == size && memcmp(data, dec, size) == 0)
        r.ok = 1;

    free(comp); free(dec);
    return r;
}

static result_t bench_stride_mcx(const char* label, const uint8_t* data, size_t size, int level) {
    result_t r = {label, size, 0, 0, 0, 0};

    /* Step 1: Stride-delta transform */
    size_t stride_cap = mcx_babel_stride_bound(size);
    uint8_t* stride_buf = malloc(stride_cap);
    if (!stride_buf) return r;

    double t0 = now_sec();
    size_t stride_size = mcx_babel_stride_forward(stride_buf, stride_cap, data, size);

    if (stride_size == 0) {
        /* Stride didn't help, fall back to raw MCX */
        free(stride_buf);
        return bench_mcx(label, data, size, level);
    }

    /* Step 2: Compress stride output with MCX */
    size_t cap = mcx_compress_bound(stride_size) + 131072;
    uint8_t* comp = malloc(cap);
    if (!comp) { free(stride_buf); return r; }

    size_t cs = mcx_compress(comp, cap, stride_buf, stride_size, level);
    r.time_ms = (now_sec() - t0) * 1000.0;

    if (mcx_is_error(cs)) { free(stride_buf); free(comp); return r; }
    r.comp_size = cs;
    r.ratio = (double)size / cs;

    /* Verify roundtrip */
    uint8_t* dec_stride = malloc(stride_size);
    uint8_t* dec = malloc(size);
    if (dec_stride && dec) {
        size_t ds = mcx_decompress(dec_stride, stride_size, comp, cs);
        if (!mcx_is_error(ds) && ds == stride_size) {
            size_t orig = mcx_babel_stride_inverse(dec, size, dec_stride, stride_size);
            if (orig == size && memcmp(data, dec, size) == 0)
                r.ok = 1;
        }
    }

    free(stride_buf); free(comp); free(dec_stride); free(dec);
    return r;
}

static void bench_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("  SKIP %s (not found)\n", path); return; }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    if (!data || fread(data, 1, size, f) != size) {
        fclose(f); free(data); return;
    }
    fclose(f);

    /* Extract filename */
    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;

    result_t results[6];
    results[0] = bench_mcx("L3-LZ77", data, size, 3);
    results[1] = bench_mcx("L12-BWT", data, size, 12);
    results[2] = bench_mcx("L20-Babel", data, size, 20);
    results[3] = bench_stride_mcx("Stride+L12", data, size, 12);
    results[4] = bench_stride_mcx("Stride+L20", data, size, 20);
    results[5] = bench_stride_mcx("Stride+L3", data, size, 3);

    /* Find best */
    int best_idx = 0;
    for (int i = 1; i < 6; i++) {
        if (results[i].ok && results[i].ratio > results[best_idx].ratio)
            best_idx = i;
    }

    printf("  %-16s %7zuB |", name, size);
    for (int i = 0; i < 6; i++) {
        if (results[i].ok) {
            printf(" %5.2fx%s", results[i].ratio, i == best_idx ? "*" : " ");
        } else {
            printf("  FAIL ");
        }
    }
    printf("\n");

    free(data);
}

int main(void) {
    printf("╔═══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║  MaxCompression — Full Pipeline Benchmark (Canterbury Corpus)                ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════════════════╣\n");
    printf("  %-16s %8s | %-7s %-7s %-7s %-7s %-7s %-7s\n",
           "File", "Size", "L3-LZ", "L12-BWT", "L20-Bab", "St+L12", "St+L20", "St+L3");
    printf("  %-16s %8s | %-7s %-7s %-7s %-7s %-7s %-7s\n",
           "----", "----", "------", "------", "------", "------", "------", "------");

    const char* files[] = {
        "/tmp/cantrbry/alice29.txt",
        "/tmp/cantrbry/asyoulik.txt",
        "/tmp/cantrbry/cp.html",
        "/tmp/cantrbry/fields.c",
        "/tmp/cantrbry/grammar.lsp",
        "/tmp/cantrbry/kennedy.xls",
        "/tmp/cantrbry/lcet10.txt",
        "/tmp/cantrbry/plrabn12.txt",
        "/tmp/cantrbry/ptt5",
        "/tmp/cantrbry/sum",
        "/tmp/cantrbry/xargs.1",
        NULL
    };

    for (int i = 0; files[i]; i++) {
        bench_file(files[i]);
    }

    printf("╚═══════════════════════════════════════════════════════════════════════════════╝\n");
    printf("  * = best ratio for that file\n");
    return 0;
}
