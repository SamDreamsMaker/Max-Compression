/**
 * @file main.c
 * @brief MaxCompression CLI — Command-line interface for mcx.
 *
 * Usage:
 *   mcx compress [-l LEVEL] <input> [-o output.mcx]
 *   mcx decompress <input.mcx> [-o output]
 *   mcx info <input.mcx>
 *   mcx --version
 *   mcx --help
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <maxcomp/maxcomp.h>
#include "../lib/internal.h"

/* ─── Timing ─────────────────────────────────────────────────────────── */

static double get_time_sec(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

/* ─── Helpers ────────────────────────────────────────────────────────── */

static void print_usage(void)
{
    printf(
        "MaxCompression v%s\n"
        "\n"
        "Usage:\n"
        "  mcx compress  [-l LEVEL] [--fast|--default|--best] [-q] <input> [-o output.mcx]\n"
        "  mcx decompress [-q] <input.mcx> [-o output]\n"
        "  mcx info       <input.mcx>\n"
        "  mcx cat        <input.mcx>   # decompress to stdout\n"
        "  mcx bench      <input>       # benchmark all levels\n"
        "  mcx test                     # run self-tests\n"
        "  mcx version                  # detailed build info\n"
        "  mcx --version\n"
        "  mcx --help\n"
        "\n"
        "Levels (default: %d):\n"
        "  L1-L3   Fast LZ77 (speed priority)\n"
        "  L6      LZ77 with lazy matching + rANS\n"
        "  L7-L8   LZ77 lazy + adaptive arithmetic coding\n"
        "  L9      LZ77 deep chains + adaptive arithmetic coding\n"
        "  L12     BWT + genome optimizer + multi-table rANS\n"
        "  L20     Best: auto-routes text→BWT, binary→LZRC\n"
        "  L24     LZRC fast: hash chain (~3x faster than L26)\n"
        "  L26     LZRC direct: binary tree (best LZ ratio)\n"
        "\n"
        "Shortcuts:\n"
        "  --fast (-1)     L3: fast compression\n"
        "  --default (-6)  L6: balanced speed/ratio\n"
        "  --best (-9)     L20: maximum compression\n"
        "\n"
        "Options:\n"
        "  -f, --force     Overwrite existing output files\n"
        "  -k, --keep      Keep original file (default, for gzip compat)\n"
        "      --delete    Delete source file after successful operation\n"
        "  -c, --stdout    Write output to stdout\n"
        "  -q, --quiet     Suppress non-error output\n"
        "\n"
        "Examples:\n"
        "  mcx compress myfile.txt              # fast (L3)\n"
        "  mcx compress --best myfile.txt       # maximum compression\n"
        "  mcx compress -l 9 myfile.txt         # good ratio, decent speed\n"
        "  mcx compress -l 24 bigfile.bin       # fast LZRC (binary)\n"
        "  mcx decompress myfile.txt.mcx\n"
        "  mcx bench myfile.txt                 # compare all levels\n"
        "\n",
        mcx_version_string(),
        MCX_LEVEL_DEFAULT
    );
}

static uint8_t* read_file(const char* path, size_t* size_out)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        int err = errno;
        if (err == ENOENT)
            fprintf(stderr, "Error: file not found: '%s'\n", path);
        else if (err == EACCES)
            fprintf(stderr, "Error: permission denied: '%s'\n", path);
        else if (err == EISDIR)
            fprintf(stderr, "Error: '%s' is a directory, not a file\n", path);
        else
            fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(err));
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fprintf(stderr, "Error: '%s' is empty or unreadable (size=%ld)\n", path, file_size);
        fclose(f);
        return NULL;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)file_size);
    if (data == NULL) {
        fprintf(stderr, "Error: out of memory allocating %ld bytes for '%s'\n", file_size, path);
        fclose(f);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)file_size, f);
    fclose(f);

    if (read != (size_t)file_size) {
        fprintf(stderr, "Error: short read on '%s' (got %zu of %ld bytes)\n", path, read, file_size);
        free(data);
        return NULL;
    }

    *size_out = (size_t)file_size;
    return data;
}

static int write_file(const char* path, const uint8_t* data, size_t size)
{
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        int err = errno;
        if (err == EACCES)
            fprintf(stderr, "Error: permission denied writing '%s'\n", path);
        else if (err == ENOENT)
            fprintf(stderr, "Error: directory does not exist for '%s'\n", path);
        else if (err == ENOSPC)
            fprintf(stderr, "Error: no space left on device for '%s'\n", path);
        else
            fprintf(stderr, "Error: cannot create '%s': %s\n", path, strerror(err));
        return -1;
    }

    size_t written = fwrite(data, 1, size, f);
    if (fclose(f) != 0 || written != size) {
        int err = errno;
        if (err == ENOSPC)
            fprintf(stderr, "Error: disk full while writing '%s' (wrote %zu of %zu bytes)\n", path, written, size);
        else
            fprintf(stderr, "Error: write failed on '%s': %s (wrote %zu of %zu bytes)\n", path, strerror(err), written, size);
        return -1;
    }

    return 0;
}

/* ─── Default output name ────────────────────────────────────────────── */

static void make_compress_output(char* out, size_t out_cap, const char* input)
{
    snprintf(out, out_cap, "%s.mcx", input);
}

static void make_decompress_output(char* out, size_t out_cap, const char* input)
{
    size_t len = strlen(input);
    if (len > 4 && strcmp(input + len - 4, ".mcx") == 0) {
        snprintf(out, out_cap, "%.*s", (int)(len - 4), input);
    } else {
        snprintf(out, out_cap, "%s.out", input);
    }
}

/* ─── Commands ───────────────────────────────────────────────────────── */

static int g_quiet = 0;  /* Suppress non-error output */
static int g_force = 0;  /* Overwrite existing output files */
static int g_stdout = 0; /* Write to stdout instead of file */
static int g_delete = 0; /* Delete source file after success */

static int cmd_compress(const char* input, const char* output, int level)
{
    FILE* fin = fopen(input, "rb");
    if (!fin) {
        int err = errno;
        if (err == ENOENT)
            fprintf(stderr, "Error: file not found: '%s'\n", input);
        else if (err == EACCES)
            fprintf(stderr, "Error: permission denied: '%s'\n", input);
        else if (err == EISDIR)
            fprintf(stderr, "Error: '%s' is a directory, not a file\n", input);
        else
            fprintf(stderr, "Error: cannot open '%s': %s\n", input, strerror(err));
        return 1;
    }
    
    char auto_output[1024];
    if (!g_stdout && output == NULL) {
        make_compress_output(auto_output, sizeof(auto_output), input);
        output = auto_output;
    }
    
    /* Check if output exists (unless --force or --stdout) */
    if (!g_stdout && !g_force && output) {
        FILE* check = fopen(output, "rb");
        if (check) {
            fclose(check);
            fprintf(stderr, "Error: '%s' already exists (use -f to overwrite)\n", output);
            fclose(fin);
            return 1;
        }
    }
    
    FILE* fout = g_stdout ? stdout : fopen(output, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot create '%s': %s\n", output, strerror(errno));
        fclose(fin);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    size_t src_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    size_t total_out = 0;
    double t0 = get_time_sec();

    /* Use one-shot API for files that fit in memory (full pipeline with
     * multi-trial, E8/E9, multi-table rANS, etc.).
     * Fall back to streaming API for very large files (>256MB). */
    size_t ONE_SHOT_LIMIT = 256ULL * 1024 * 1024;

    if (src_size > 0 && src_size <= ONE_SHOT_LIMIT) {
        /* ── One-shot compression (full pipeline) ── */
        uint8_t* src_buf = (uint8_t*)malloc(src_size);
        size_t dst_cap = mcx_compress_bound(src_size);
        uint8_t* dst_buf = (uint8_t*)malloc(dst_cap);

        if (!src_buf || !dst_buf) {
            fprintf(stderr, "Error: out of memory (%zu bytes)\n", src_size);
            if (src_buf) free(src_buf);
            if (dst_buf) free(dst_buf);
            fclose(fin); fclose(fout);
            return 1;
        }

        size_t bytes_read = fread(src_buf, 1, src_size, fin);
        if (bytes_read != src_size) {
            fprintf(stderr, "Error: could not read full file\n");
            free(src_buf); free(dst_buf);
            fclose(fin); fclose(fout);
            return 1;
        }

        if (!g_quiet) {
            if (src_size > 1024 * 1024)
                printf("Compressing '%s' (%.1f MB) at level %d...\n", input, (double)src_size / (1024*1024), level);
            else
                printf("Compressing '%s' (%zu bytes) at level %d...\n", input, src_size, level);
            fflush(stdout);
        }

        size_t comp_size = mcx_compress(dst_buf, dst_cap, src_buf, src_size, level);
        free(src_buf);

        if (mcx_is_error(comp_size)) {
            fprintf(stderr, "Error: compression failed: %s\n", mcx_get_error_name(comp_size));
            free(dst_buf);
            fclose(fin); fclose(fout);
            return 1;
        }

        fwrite(dst_buf, 1, comp_size, fout);
        total_out = comp_size;
        free(dst_buf);
    } else {
        /* ── Streaming compression (for very large files or empty files) ── */
        size_t IN_CHUNK_SIZE = 64 * 1024;
        size_t OUT_CHUNK_SIZE = 256 * 1024;

        uint8_t* in_buf = (uint8_t*)malloc(IN_CHUNK_SIZE);
        uint8_t* out_buf = (uint8_t*)malloc(OUT_CHUNK_SIZE);

        mcx_cctx* cctx = mcx_create_cctx();
        if (!in_buf || !out_buf || !cctx) {
            fprintf(stderr, "Error: out of memory during CLI init\n");
            if (in_buf) free(in_buf); if (out_buf) free(out_buf);
            if (cctx) mcx_free_cctx(cctx);
            fclose(fin); fclose(fout);
            return 1;
        }

        fseek(fin, 0, SEEK_SET);

        if (!g_quiet) printf("Compressing '%s' (%zu bytes) at level %d... (Streaming API)\n", input, src_size, level);

        mcx_in_buffer in_b = { in_buf, 0, 0 };
        mcx_out_buffer out_b = { out_buf, OUT_CHUNK_SIZE, 0 };

        int is_eof = 0;
        size_t result = 1;
        size_t total_in = 0;
        double last_progress = 0;

        while (result != 0) {
            if (in_b.pos >= in_b.size && !is_eof) {
                size_t bytes_read = fread(in_buf, 1, IN_CHUNK_SIZE, fin);
                in_b.size = bytes_read;
                in_b.pos = 0;
                total_in += bytes_read;
                if (bytes_read == 0) is_eof = 1;
                /* Show progress every ~1 second */
                if (!g_quiet && src_size > 1024 * 1024) {
                    double now = get_time_sec();
                    if (now - last_progress >= 1.0) {
                        double pct = (double)total_in / (double)src_size * 100.0;
                        fprintf(stderr, "\r  Progress: %.0f%%", pct);
                        fflush(stderr);
                        last_progress = now;
                    }
                }
            }

            if (is_eof) {
                in_b.src = NULL;
                in_b.size = 0;
                in_b.pos = 0;
            }

            result = mcx_compress_stream(cctx, &out_b, &in_b, level);

            if (mcx_is_error(result)) {
                fprintf(stderr, "Error: Stream compression failed: %s\n", mcx_get_error_name(result));
                free(in_buf); free(out_buf); mcx_free_cctx(cctx);
                fclose(fin); fclose(fout);
                return 1;
            }

            if (out_b.pos > 0) {
                fwrite(out_buf, 1, out_b.pos, fout);
                total_out += out_b.pos;
                out_b.pos = 0;
            }
        }

        free(in_buf);
        free(out_buf);
        mcx_free_cctx(cctx);
    }

    double ratio = (double)src_size / (double)total_out;
    double savings = (1.0 - (double)total_out / (double)src_size) * 100.0;
    double elapsed = get_time_sec() - t0;
    double speed = (elapsed > 0.001) ? (double)src_size / (1024*1024) / elapsed : 0;

    if (!g_quiet) printf("Done!\n");
    if (!g_quiet) printf("  Input:  %zu bytes\n", src_size);
    if (!g_quiet) {
        if (g_stdout) printf("  Output: %zu bytes -> stdout\n", total_out);
        else printf("  Output: %zu bytes -> '%s'\n", total_out, output);
        printf("  Ratio:  %.2fx (%.1f%% smaller)\n", ratio, savings);
        if (elapsed >= 0.01)
            printf("  Time:   %.2fs (%.1f MB/s)\n", elapsed, speed);
    }

    fclose(fin);
    fclose(fout);

    /* Delete source file if --delete was specified */
    if (g_delete && !g_stdout) {
        if (remove(input) != 0) {
            fprintf(stderr, "Warning: compressed OK but could not delete '%s': %s\n", input, strerror(errno));
        } else if (!g_quiet) {
            printf("  Deleted: '%s'\n", input);
        }
    }

    return 0;
}

static int cmd_decompress(const char* input, const char* output)
{
    FILE* fin = fopen(input, "rb");
    if (!fin) {
        int err = errno;
        if (err == ENOENT)
            fprintf(stderr, "Error: file not found: '%s'\n", input);
        else if (err == EACCES)
            fprintf(stderr, "Error: permission denied: '%s'\n", input);
        else
            fprintf(stderr, "Error: cannot open '%s': %s\n", input, strerror(err));
        return 1;
    }
    
    char auto_output[1024];
    if (!g_stdout && output == NULL) {
        make_decompress_output(auto_output, sizeof(auto_output), input);
        output = auto_output;
    }
    
    /* Check if output exists (unless --force or --stdout) */
    if (!g_stdout && !g_force && output) {
        FILE* check = fopen(output, "rb");
        if (check) {
            fclose(check);
            fprintf(stderr, "Error: '%s' already exists (use -f to overwrite)\n", output);
            fclose(fin);
            return 1;
        }
    }
    
    FILE* fout = g_stdout ? stdout : fopen(output, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot create '%s': %s\n", output, strerror(errno));
        fclose(fin);
        return 1;
    }

    /* Read entire compressed file into memory */
    fseek(fin, 0, SEEK_END);
    size_t src_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t* src_buf = (uint8_t*)malloc(src_size);
    if (!src_buf) {
        fprintf(stderr, "Error: out of memory (%zu bytes)\n", src_size);
        fclose(fin); fclose(fout);
        return 1;
    }

    size_t bytes_read = fread(src_buf, 1, src_size, fin);
    if (bytes_read != src_size) {
        fprintf(stderr, "Error: could not read full file\n");
        free(src_buf); fclose(fin); fclose(fout);
        return 1;
    }

    /* Read original size from frame header */
    uint64_t orig_size = 0;
    if (src_size >= 16) {
        memcpy(&orig_size, src_buf + 8, 8);
    }
    if (orig_size == 0 || orig_size > 1024ULL * 1024 * 1024) {
        /* Fallback: use 10x compressed size as estimate */
        orig_size = src_size * 10;
    }

    uint8_t* dst_buf = (uint8_t*)malloc((size_t)orig_size + 1024);
    if (!dst_buf) {
        fprintf(stderr, "Error: out of memory for decompression (%llu bytes)\n",
                (unsigned long long)orig_size);
        free(src_buf); fclose(fin); fclose(fout);
        return 1;
    }

    if (!g_quiet) {
        if (src_size > 1024 * 1024)
            printf("Decompressing '%s' (%.1f MB)...\n", input, (double)src_size / (1024*1024));
        else
            printf("Decompressing '%s' (%zu bytes)...\n", input, src_size);
        fflush(stdout);
    }

    double dt0 = get_time_sec();
    size_t total_out = mcx_decompress(dst_buf, (size_t)orig_size + 1024, src_buf, src_size);
    double dt_elapsed = get_time_sec() - dt0;
    free(src_buf);

    if (mcx_is_error(total_out)) {
        fprintf(stderr, "Error: decompression failed: %s\n", mcx_get_error_name(total_out));
        fprintf(stderr, "  Hint: is '%s' a valid .mcx file? Try 'mcx info %s' to check.\n", input, input);
        free(dst_buf); fclose(fin); fclose(fout);
        return 1;
    }

    fwrite(dst_buf, 1, total_out, fout);
    free(dst_buf);

    double dt_speed = (dt_elapsed > 0.001) ? (double)total_out / (1024*1024) / dt_elapsed : 0;
    if (!g_quiet) printf("Done!\n");
    if (!g_quiet) {
        printf("  Decompressed: %zu bytes -> '%s'\n", total_out, output);
        if (dt_elapsed >= 0.01)
            printf("  Time:   %.2fs (%.1f MB/s)\n", dt_elapsed, dt_speed);
    }

    fclose(fin);
    fclose(fout);

    /* Delete source file if --delete was specified */
    if (g_delete && !g_stdout) {
        if (remove(input) != 0) {
            fprintf(stderr, "Warning: decompressed OK but could not delete '%s': %s\n", input, strerror(errno));
        } else if (!g_quiet) {
            printf("  Deleted: '%s'\n", input);
        }
    }

    return 0;
}

static const char* strategy_name(uint8_t s) {
    switch (s) {
        case 0: return "STORE (no compression)";
        case 1: return "FAST (RLE+Huffman)";
        case 2: return "DEFAULT (BWT+rANS)";
        case 3: return "BEST (BWT+CM-rANS)";
        case 4: return "LZ_FAST (LZ77 greedy)";
        case 5: return "LZ_HC (LZ77 lazy)";
        case 6: return "BABEL (XOR predict)";
        case 7: return "STRIDE (stride-delta)";
        case 8: return "LZ24 (LZ77 24-bit)";
        case 9: return "LZRC (LZ+Range Coder v2.0)";
        default: return "UNKNOWN";
    }
}

static int cmd_info(const char* input)
{
    size_t src_size;
    uint8_t* src = read_file(input, &src_size);
    if (src == NULL) return 1;

    printf("File:             %s\n", input);
    printf("Compressed size:  %zu bytes (%.1f KB)\n", src_size, src_size / 1024.0);

    mcx_frame_info info;
    size_t r = mcx_get_frame_info(&info, src, src_size);
    if (mcx_is_error(r)) {
        printf("Error: Not a valid MCX file\n");
        free(src);
        return 1;
    }

    printf("Format:           MCX v%u\n", info.version);
    printf("Level:            %u\n", info.level);
    printf("Strategy:         %s\n", strategy_name(info.strategy));

    if (info.original_size > 0) {
        printf("Original size:    %llu bytes (%.1f KB)\n",
               info.original_size, info.original_size / 1024.0);
        printf("Ratio:            %.2fx (%.1f%% smaller)\n",
               (double)info.original_size / src_size,
               100.0 * (1.0 - (double)src_size / info.original_size));
    }
    if (info.flags & 0x04) printf("Filters:          E8/E9 x86\n");

    free(src);
    return 0;
}

/* ─── Bench command ──────────────────────────────────────────────────── */

#ifdef _WIN32
#include <windows.h>
static double bench_time(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / freq.QuadPart;
}
#else
#include <time.h>
static double bench_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

static int cmd_bench(const char* input)
{
    size_t src_size;
    uint8_t* src = read_file(input, &src_size);
    if (!src) return 1;

    printf("Benchmarking '%s' (%zu bytes / %zu KB)\n\n", input, src_size, src_size / 1024);
    printf("%-6s %10s %10s %8s %10s %10s\n",
           "Level", "Compressed", "Ratio", "Saving", "Comp MB/s", "Dec MB/s");
    printf("─────────────────────────────────────────────────────────────\n");

    int levels[] = {1, 3, 6, 9, 12, 20, 24, 26};
    int n_levels = sizeof(levels) / sizeof(levels[0]);

    size_t comp_cap = mcx_compress_bound(src_size);
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec = (uint8_t*)malloc(src_size + 64);
    if (!comp || !dec) {
        fprintf(stderr, "Error: out of memory\n");
        free(src); free(comp); free(dec);
        return 1;
    }

    for (int i = 0; i < n_levels; i++) {
        int level = levels[i];

        /* Compress */
        double t0 = bench_time();
        size_t comp_size = mcx_compress(comp, comp_cap, src, src_size, level);
        double t1 = bench_time();

        if (mcx_is_error(comp_size)) {
            printf("L%-5d  ERROR: %s\n", level, mcx_get_error_name(comp_size));
            continue;
        }

        double comp_time = t1 - t0;
        double comp_speed = src_size / comp_time / 1048576.0;

        /* Decompress */
        t0 = bench_time();
        size_t dec_size = mcx_decompress(dec, src_size + 64, comp, comp_size);
        t1 = bench_time();

        double dec_time = t1 - t0;
        double dec_speed = src_size / dec_time / 1048576.0;

        /* Verify */
        int ok = (!mcx_is_error(dec_size) && dec_size == src_size &&
                  memcmp(src, dec, src_size) == 0);

        double ratio = (double)src_size / comp_size;
        double saving = 100.0 * (1.0 - (double)comp_size / src_size);

        printf("L%-5d %10zu %9.2fx %7.1f%% %9.1f %9.1f %s\n",
               level, comp_size, ratio, saving, comp_speed, dec_speed,
               ok ? "" : " FAIL!");
    }

    printf("\n");
    free(src); free(comp); free(dec);
    return 0;
}

/* ─── Main ───────────────────────────────────────────────────────────── */

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    /* --help / --version */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("mcx %s\n", mcx_version_string());
        return 0;
    }

    /* version subcommand — detailed build info */
    if (strcmp(argv[1], "version") == 0) {
        printf("MaxCompression v%s\n", mcx_version_string());
        printf("  Library:    libmaxcomp %s\n", mcx_version_string());
#ifdef __GNUC__
        printf("  Compiler:   GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__clang__)
        printf("  Compiler:   Clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
        printf("  Compiler:   MSVC %d\n", _MSC_VER);
#else
        printf("  Compiler:   unknown\n");
#endif
        printf("  Build:      %s %s\n", __DATE__, __TIME__);
#ifdef _OPENMP
        printf("  OpenMP:     yes\n");
#else
        printf("  OpenMP:     no\n");
#endif
        printf("  Platform:   %s\n",
#if defined(__linux__)
            "Linux"
#elif defined(__APPLE__)
            "macOS"
#elif defined(_WIN32)
            "Windows"
#else
            "unknown"
#endif
        );
        printf("  Arch:       %s\n",
#if defined(__x86_64__) || defined(_M_X64)
            "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
            "aarch64"
#elif defined(__i386__) || defined(_M_IX86)
            "x86"
#elif defined(__arm__) || defined(_M_ARM)
            "arm"
#else
            "unknown"
#endif
        );
        printf("  Levels:     1-26 (BWT up to L20, LZRC L24/L26)\n");
        return 0;
    }

    /* Commands */
    if (strcmp(argv[1], "compress") == 0) {
        int level = MCX_LEVEL_DEFAULT;
        const char* input = NULL;
        const char* output = NULL;

        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0) && i + 1 < argc) {
                level = atoi(argv[++i]);
                if (level < 1 || level > 26) {
                    fprintf(stderr, "Error: invalid level %d (must be 1-26)\n"
                            "  Recommended: --fast (L3), --default (L6), --best (L20)\n", level);
                    return 1;
                }
            } else if (strcmp(argv[i], "--fast") == 0 || strcmp(argv[i], "-1") == 0) {
                level = 3;
            } else if (strcmp(argv[i], "--default") == 0 || strcmp(argv[i], "-6") == 0) {
                level = 6;
            } else if (strcmp(argv[i], "--best") == 0 || strcmp(argv[i], "-9") == 0) {
                level = 20;
            } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
                output = argv[++i];
            } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
                g_quiet = 1;
            } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
                g_force = 1;
            } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--stdout") == 0) {
                g_stdout = 1; g_quiet = 1;
            } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep") == 0) {
                /* No-op: MCX keeps original files by default (gzip/xz compat) */
                g_delete = 0;
            } else if (strcmp(argv[i], "--delete") == 0) {
                g_delete = 1;
            } else if (argv[i][0] != '-') {
                input = argv[i];
            }
        }

        if (input == NULL) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s <file>\n", argv[1]);
            return 1;
        }
        return cmd_compress(input, output, level);

    } else if (strcmp(argv[1], "decompress") == 0) {
        const char* input = NULL;
        const char* output = NULL;

        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
                output = argv[++i];
            } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
                g_quiet = 1;
            } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
                g_force = 1;
            } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--stdout") == 0) {
                g_stdout = 1; g_quiet = 1;
            } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep") == 0) {
                /* No-op: MCX keeps original files by default (gzip/xz compat) */
                g_delete = 0;
            } else if (strcmp(argv[i], "--delete") == 0) {
                g_delete = 1;
            } else if (argv[i][0] != '-') {
                input = argv[i];
            }
        }

        if (input == NULL) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s <file>\n", argv[1]);
            return 1;
        }
        return cmd_decompress(input, output);

    } else if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s <file>\n", argv[1]);
            return 1;
        }
        return cmd_info(argv[2]);

    } else if (strcmp(argv[1], "cat") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s <file>\n", argv[1]);
            return 1;
        }
        /* Decompress to stdout */
        size_t src_size;
        uint8_t* src = read_file(argv[2], &src_size);
        if (!src) return 1;
        unsigned long long orig = mcx_get_decompressed_size(src, src_size);
        if (orig == 0) { fprintf(stderr, "Error: not a valid MCX file\n"); free(src); return 1; }
        uint8_t* dec = (uint8_t*)malloc((size_t)orig + 1024);
        if (!dec) { fprintf(stderr, "Error: out of memory\n"); free(src); return 1; }
        size_t dsz = mcx_decompress(dec, (size_t)orig + 1024, src, src_size);
        free(src);
        if (mcx_is_error(dsz)) { fprintf(stderr, "Error: decompression failed\n"); free(dec); return 1; }
        fwrite(dec, 1, dsz, stdout);
        free(dec);
        return 0;

    } else if (strcmp(argv[1], "bench") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s <file>\n", argv[1]);
            return 1;
        }
        return cmd_bench(argv[2]);

    } else if (strcmp(argv[1], "test") == 0) {
        printf("MaxCompression v%s — Self-test\n\n", mcx_version_string());
        int pass = 0, fail = 0;
        int levels[] = {1, 3, 6, 7, 9, 12, 20, 24, 26};
        int n_levels = sizeof(levels) / sizeof(levels[0]);
        
        /* Test pattern: repeated text */
        const char* text = "The quick brown fox jumps over the lazy dog. ";
        size_t tlen = strlen(text);
        uint8_t src[4096];
        for (size_t i = 0; i < sizeof(src); i++)
            src[i] = text[i % tlen];
        
        uint8_t comp[8192], dec[4096];
        
        for (int i = 0; i < n_levels; i++) {
            int lv = levels[i];
            size_t csz = mcx_compress(comp, sizeof(comp), src, sizeof(src), lv);
            if (mcx_is_error(csz)) {
                printf("  L%-2d  FAIL (compress error)\n", lv);
                fail++;
                continue;
            }
            size_t dsz = mcx_decompress(dec, sizeof(dec), comp, csz);
            if (mcx_is_error(dsz) || dsz != sizeof(src) || memcmp(src, dec, sizeof(src)) != 0) {
                printf("  L%-2d  FAIL (roundtrip error)\n", lv);
                fail++;
                continue;
            }
            printf("  L%-2d  OK  %zu → %zu (%.2fx)\n", lv, sizeof(src), csz,
                   (double)sizeof(src) / csz);
            pass++;
        }
        
        printf("\n%d/%d passed%s\n", pass, pass + fail,
               fail ? " — FAILURES DETECTED" : " — all OK");
        return fail > 0 ? 1 : 0;

    } else {
        fprintf(stderr, "Error: unknown command '%s'\n", argv[1]);
        /* Suggest closest match for common typos */
        if (strstr(argv[1], "comp") || strcmp(argv[1], "c") == 0 || strcmp(argv[1], "zip") == 0)
            fprintf(stderr, "  Did you mean: mcx compress?\n");
        else if (strstr(argv[1], "decomp") || strcmp(argv[1], "d") == 0 || strcmp(argv[1], "unzip") == 0 || strstr(argv[1], "extract"))
            fprintf(stderr, "  Did you mean: mcx decompress?\n");
        else if (strcmp(argv[1], "i") == 0 || strstr(argv[1], "header") || strstr(argv[1], "stat"))
            fprintf(stderr, "  Did you mean: mcx info?\n");
        else
            fprintf(stderr, "  Available commands: compress, decompress, info, cat, bench, test\n");
        return 1;
    }
}
