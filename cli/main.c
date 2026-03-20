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
#include <sys/stat.h>
#include <sys/resource.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#include <fnmatch.h>
#include <maxcomp/maxcomp.h>
#include "../lib/internal.h"
#include "../lib/optimizer/genetic.h"
#include "../lib/analyzer/analyzer.h"
#include "../lib/babel/babel_stride.h"
#ifdef _OPENMP
#include <omp.h>
#endif

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
        "  mcx compress   [-l LEVEL] [-s STRATEGY] [--fast|--default|--best] [-q] <input> [-o output.mcx]\n"
        "  mcx decompress [-q] <input.mcx> [-o output]\n"
        "  mcx verify     <file.mcx> [original]   # verify integrity\n"
        "  mcx diff       <a.mcx> <b.mcx>         # compare two archives\n"
        "  mcx info       [--blocks] <input.mcx>    # show frame header (--blocks for per-block details)\n"
        "  mcx ls/list    [--json] <file.mcx> [file2.mcx ...]\n"
        "  mcx stat       <file>                     # file statistics (entropy, bytes)\n"
        "  mcx hash       <file.mcx> [file2.mcx ...] # CRC32/FNV hash of content\n"
        "  mcx checksum   <file.mcx> [file2.mcx ...] # verify header CRC32 integrity\n"
        "  mcx cat        <input.mcx>              # decompress to stdout\n"
        "  mcx bench      [-l LEVEL] [--compare] [--format table|csv|json|markdown] [--csv] [--json] [--warmup] [--decode-only] [--iterations N] [--median] [--percentile] [--histogram] [--brief] [--size SIZE] [--all-levels] [--ratio-only] [--sort ratio|speed|level] [--top N] <input>\n"
        "  mcx compare    <input>                   # alias for bench\n"
        "  mcx upgrade    [-l LEVEL] [--in-place] <file.mcx>  # recompress at different level\n"
        "  mcx pipe       [-l LEVEL] [-d]          # compress/decompress stdin→stdout\n"
        "  mcx test                                # run self-tests\n"
        "  mcx version [--build]                   # detailed build info\n"
        "  mcx --version\n"
        "  mcx --help\n"
        "\n"
        "Aliases:\n"
        "  decompress → extract, x, d\n"
        "  bench → compare\n"
        "  upgrade → recompress\n"
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
        "  -r, --recursive Recurse into directories\n"
        "  --exclude GLOB  Skip files matching glob pattern (with -r)\n"
        "  -c, --stdout    Write output to stdout\n"
        "  -q, --quiet     Suppress non-error output\n"
        "  -v, --verbose   Show extra info (peak memory usage)\n"
        "  -s, --strategy  Force strategy: lz, bwt, cm, smart, lzrc\n"
        "  -t, -T, --threads N  Use N threads (default: all cores)\n"
        "      --block-size SIZE  Override block size (e.g. 1M, 4M, 32M)\n"
        "  -n, --dry-run   Analyze file without compressing\n"
        "      --estimate  Estimate compressed size via sample (faster)\n"
        "      --no-trials Skip multi-strategy trials at L20+ (faster)\n"
        "      --level-scan Try L1-L20, pick best ratio automatically\n"
        "      --level-range LO-HI Try a range of levels (e.g. 1-6, L3-L12) and pick best\n"
        "      --filter F  Force preprocessing filter: auto, delta, nibble, none\n"
        "      --min-ratio N     Only write output if ratio >= N (else skip)\n"
        "      --atomic          Write to temp file, rename on success (crash-safe)\n"
        "      --preserve-mtime  Set output file mtime to match input\n"
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

static int g_quiet = 0;     /* Suppress non-error output */
static int g_force = 0;     /* Overwrite existing output files */
static int g_stdout = 0;    /* Write to stdout instead of file */
static int g_delete = 0;    /* Delete source file after success */
static int g_threads = 0;   /* Thread count (0 = auto) */
static int g_recursive = 0; /* Recurse into directories */
static int g_verify = 0;    /* Verify after compress (decompress+compare) */
static int g_verbose = 0;   /* Show extra info (peak memory, timings) */
static int g_dryrun = 0;    /* Dry-run: analyze only, don't compress */
static int g_force_filter = 0; /* 0=auto, 1=delta, 2=nibble, 3=none */
static int g_estimate = 0;  /* Estimate compressed size via sample compress */
static int g_level_scan = 0; /* Try L1-L12, pick best ratio automatically */
static int g_level_range_lo = 0; /* --level-range lower bound (0=unused) */
static int g_level_range_hi = 0; /* --level-range upper bound */
static size_t g_split_size = 0; /* Split output into chunks of this size (0=disabled) */
static int g_preserve_mtime = 0; /* Preserve source file mtime on output */
static double g_min_ratio = 0.0; /* Minimum ratio threshold (0=disabled) */
static int g_atomic = 0;        /* Atomic write: use temp file + rename */
static int g_keep_broken = 0;   /* Keep partial output on error (for debugging) */
static const char* g_exclude_pattern = NULL; /* Glob pattern for --exclude */

/** Get peak RSS in KB via getrusage. Returns 0 if unavailable. */
static long get_peak_rss_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return ru.ru_maxrss; /* KB on Linux */
    return 0;
}

/** Format memory size for display */
static const char* fmt_mem(long kb, char* buf, size_t bufsz) {
    if (kb >= 1024)
        snprintf(buf, bufsz, "%.1f MB", kb / 1024.0);
    else
        snprintf(buf, bufsz, "%ld KB", kb);
    return buf;
}

/* ─── Recursive directory traversal ──────────────────────────────────── */

typedef struct {
    char** paths;
    int count;
    int capacity;
} file_list_t;

static void file_list_init(file_list_t* fl) {
    fl->paths = NULL; fl->count = 0; fl->capacity = 0;
}

static void file_list_add(file_list_t* fl, const char* path) {
    if (fl->count >= fl->capacity) {
        fl->capacity = fl->capacity ? fl->capacity * 2 : 64;
        fl->paths = realloc(fl->paths, fl->capacity * sizeof(char*));
    }
    fl->paths[fl->count++] = strdup(path);
}

static void file_list_free(file_list_t* fl) {
    for (int i = 0; i < fl->count; i++) free(fl->paths[i]);
    free(fl->paths);
    fl->paths = NULL; fl->count = 0; fl->capacity = 0;
}

static int is_directory(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static void collect_files_recursive(const char* dir_path, file_list_t* fl,
                                     const char* skip_ext,
                                     const char* exclude_pattern) {
    DIR* dir = opendir(dir_path);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; /* skip hidden + . and .. */
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect_files_recursive(full_path, fl, skip_ext, exclude_pattern);
        } else if (S_ISREG(st.st_mode)) {
            /* Skip files with the given extension (e.g., skip .mcx when compressing) */
            if (skip_ext) {
                size_t plen = strlen(full_path);
                size_t elen = strlen(skip_ext);
                if (plen > elen && strcmp(full_path + plen - elen, skip_ext) == 0)
                    continue;
            }
            /* Skip files matching --exclude glob pattern */
            if (exclude_pattern && fnmatch(exclude_pattern, entry->d_name, 0) == 0)
                continue;
            file_list_add(fl, full_path);
        }
    }
    closedir(dir);
}

/* Remove partial output on error (unless --keep-broken) */
static void cleanup_partial(const char* path) {
    if (!g_keep_broken && path && path[0]) {
        remove(path);
    }
}

static int cmd_compress(const char* input, const char* output, int level)
{
    /* Warn if trying to compress an already-compressed .mcx file */
    size_t ilen = strlen(input);
    if (ilen > 4 && strcmp(input + ilen - 4, ".mcx") == 0) {
        fprintf(stderr, "Warning: '%s' appears to already be compressed.\n"
                "  Did you mean: mcx decompress %s\n", input, input);
    }

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
    
    /* Capture source file mtime for --preserve-mtime */
    struct stat src_stat;
    int have_src_stat = (g_preserve_mtime && fstat(fileno(fin), &src_stat) == 0);

    fseek(fin, 0, SEEK_END);
    size_t src_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    /* ── Dry-run mode: analyze and predict, don't compress ── */
    if (g_dryrun) {
        size_t sample = src_size < 65536 ? src_size : 65536;
        uint8_t* sample_buf = (uint8_t*)malloc(sample);
        if (sample_buf) {
            size_t nr = fread(sample_buf, 1, sample, fin);
            fclose(fin);

            mcx_analysis_t a = mcx_analyze(sample_buf, nr);
            const char* type_str = "unknown";
            switch (a.type) {
                case MCX_DTYPE_TEXT_ASCII:   type_str = "ASCII text"; break;
                case MCX_DTYPE_TEXT_UTF8:    type_str = "UTF-8 text"; break;
                case MCX_DTYPE_BINARY:       type_str = "binary"; break;
                case MCX_DTYPE_STRUCTURED:   type_str = "structured (JSON/XML/CSV)"; break;
                case MCX_DTYPE_EXECUTABLE:   type_str = "executable (x86)"; break;
                case MCX_DTYPE_NUMERIC:      type_str = "numeric"; break;
                case MCX_DTYPE_HIGH_ENTROPY: type_str = "high entropy (compressed/encrypted)"; break;
                default: break;
            }

            /* Determine which strategy the level would pick */
            const char* strat_str = "auto";
            if (level == 26) strat_str = "LZRC (binary tree)";
            else if (level == 24) strat_str = "LZRC (hash chain)";
            else if (level <= 3) strat_str = "LZ77 fast";
            else if (level <= 9) strat_str = "LZ77 lazy + rANS";
            else if (a.type == MCX_DTYPE_HIGH_ENTROPY) strat_str = "STORE (incompressible)";
            else if (level <= 14) strat_str = "BWT + MTF + rANS";
            else if (level <= 19) strat_str = "BWT + MTF + CM-rANS";
            else strat_str = "Smart (auto-select BWT/stride/LZRC)";

            int stride = mcx_babel_stride_detect(sample_buf, nr);

            printf("Dry-run analysis for '%s' (%zu bytes)\n\n", input, src_size);
            printf("  Data type:    %s\n", type_str);
            printf("  Entropy:      %.2f bits/byte (%.1f%% of maximum)\n", a.entropy, a.entropy / 8.0 * 100.0);
            printf("  Level:        %d\n", level);
            printf("  Strategy:     %s\n", strat_str);
            if (stride > 0)
                printf("  Stride:       %d bytes detected (stride-delta likely beneficial)\n", stride);
            printf("  Est. ratio:   ~%.1fx (based on entropy)\n", 8.0 / a.entropy);
            printf("\n  (No output file written)\n");

            free(sample_buf);
        } else {
            fclose(fin);
            fprintf(stderr, "Error: out of memory\n");
        }
        return 0;
    }

    /* ── Estimate mode: compress a sample to predict full file ratio ── */
    if (g_estimate) {
        /* Use up to 128KB sample, compress it, extrapolate */
        size_t sample_max = 131072;
        size_t sample = src_size < sample_max ? src_size : sample_max;
        uint8_t* sample_buf = (uint8_t*)malloc(sample);
        if (sample_buf) {
            size_t nr = fread(sample_buf, 1, sample, fin);
            fclose(fin);

            double t0 = get_time_sec();
            size_t comp_cap = mcx_compress_bound(nr);
            uint8_t* comp_buf = (uint8_t*)malloc(comp_cap);
            if (comp_buf) {
                size_t csz = mcx_compress(comp_buf, comp_cap, sample_buf, nr, level);
                double dt = get_time_sec() - t0;

                if (!mcx_is_error(csz)) {
                    double ratio = (double)nr / csz;
                    size_t est_compressed = (size_t)((double)src_size / ratio);
                    /* For small files where sample == full file, estimate is exact */
                    if (nr == src_size) est_compressed = csz;

                    printf("Estimate for '%s' (%zu bytes) at level %d\n\n", input, src_size, level);
                    printf("  Sample:       %zu bytes compressed → %zu bytes (%.2fx)\n", nr, csz, ratio);
                    printf("  Est. output:  ~%zu bytes (~%.2fx ratio)\n", est_compressed, ratio);
                    printf("  Est. savings: ~%zu bytes (~%.1f%%)\n",
                           src_size > est_compressed ? src_size - est_compressed : 0,
                           src_size > est_compressed ? 100.0 * (src_size - est_compressed) / src_size : 0.0);
                    printf("  Sample speed: %.1f MB/s (%.3fs for %zu bytes)\n",
                           nr / dt / 1048576.0, dt, nr);
                    if (src_size > nr) {
                        double est_time = dt * ((double)src_size / nr);
                        printf("  Est. time:    ~%.1fs for full file\n", est_time);
                    }
                    printf("\n  (No output file written)\n");
                } else {
                    fprintf(stderr, "Error: sample compression failed: %s\n", mcx_get_error_name(csz));
                }
                free(comp_buf);
            }
            free(sample_buf);
        } else {
            fclose(fin);
            fprintf(stderr, "Error: out of memory\n");
        }
        return 0;
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

    /* --atomic: write to temp file, rename on success (crash-safe) */
    char atomic_tmp[1088];
    const char* write_path = output;
    if (g_atomic && !g_stdout && output) {
        snprintf(atomic_tmp, sizeof(atomic_tmp), "%s.tmp.%d", output, (int)getpid());
        write_path = atomic_tmp;
    }

    FILE* fout = g_stdout ? stdout : fopen(write_path, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot create '%s': %s\n", write_path, strerror(errno));
        fclose(fin);
        return 1;
    }

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
            cleanup_partial(write_path);
            return 1;
        }

        size_t bytes_read = fread(src_buf, 1, src_size, fin);
        if (bytes_read != src_size) {
            fprintf(stderr, "Error: could not read full file\n");
            free(src_buf); free(dst_buf);
            fclose(fin); fclose(fout);
            cleanup_partial(write_path);
            return 1;
        }

        size_t comp_size;
        
        if (g_level_scan || g_level_range_lo > 0) {
            /* --level-scan or --level-range: try multiple levels, pick best ratio */
            int scan_levels[] = {1, 3, 6, 9, 12, 20};
            int range_levels[MCX_LEVEL_MAX + 1];
            int n_scan;
            if (g_level_range_lo > 0) {
                /* --level-range: try every level in range */
                n_scan = 0;
                for (int lv = g_level_range_lo; lv <= g_level_range_hi && n_scan < MCX_LEVEL_MAX; lv++)
                    range_levels[n_scan++] = lv;
            } else {
                n_scan = sizeof(scan_levels) / sizeof(scan_levels[0]);
                for (int si = 0; si < n_scan; si++) range_levels[si] = scan_levels[si];
            }
            int *levels_to_scan = (g_level_range_lo > 0) ? range_levels : scan_levels;
            size_t best_size = SIZE_MAX;
            int best_level = levels_to_scan[0];
            
            if (!g_quiet) {
                printf("Scanning levels for '%s' (%zu bytes)...\n", input, src_size);
                fflush(stdout);
            }
            
            uint8_t* scan_buf = (uint8_t*)malloc(dst_cap);
            if (!scan_buf) {
                fprintf(stderr, "Error: out of memory for level scan\n");
                free(src_buf); free(dst_buf);
                fclose(fin); fclose(fout);
                return 1;
            }
            
            for (int si = 0; si < n_scan; si++) {
                int lv = levels_to_scan[si];
                size_t sz = mcx_compress(scan_buf, dst_cap, src_buf, src_size, lv);
                if (!mcx_is_error(sz)) {
                    if (!g_quiet) {
                        printf("  L%-2d: %zu bytes (%.2fx)\n", lv, sz, (double)src_size / sz);
                        fflush(stdout);
                    }
                    if (sz < best_size) {
                        best_size = sz;
                        best_level = lv;
                        /* Copy best result to dst_buf */
                        memcpy(dst_buf, scan_buf, sz);
                    }
                }
            }
            free(scan_buf);
            
            if (best_size == SIZE_MAX) {
                fprintf(stderr, "Error: all levels failed\n");
                free(src_buf); free(dst_buf);
                fclose(fin); fclose(fout);
                return 1;
            }
            
            if (!g_quiet) {
                printf("Best: L%d (%.2fx, %zu bytes)\n", best_level,
                       (double)src_size / best_size, best_size);
            }
            comp_size = best_size;
            free(src_buf);
        } else {
            /* Normal single-level compression */
            if (!g_quiet) {
                if (src_size > 1024 * 1024)
                    printf("Compressing '%s' (%.1f MB) at level %d...\n", input, (double)src_size / (1024*1024), level);
                else
                    printf("Compressing '%s' (%zu bytes) at level %d...\n", input, src_size, level);
                fflush(stdout);
            }

            comp_size = mcx_compress(dst_buf, dst_cap, src_buf, src_size, level);
            free(src_buf);
        }

        if (mcx_is_error(comp_size)) {
            fprintf(stderr, "Error: compression failed: %s\n", mcx_get_error_name(comp_size));
            free(dst_buf);
            fclose(fin); fclose(fout);
            cleanup_partial(write_path);
            return 1;
        }

        /* --min-ratio: skip writing if ratio is below threshold */
        if (g_min_ratio > 0.0 && src_size > 0) {
            double actual_ratio = (double)src_size / comp_size;
            if (actual_ratio < g_min_ratio) {
                if (!g_quiet) {
                    printf("Skipped '%s': ratio %.2fx below minimum %.2fx (compressed %zu → %zu bytes)\n",
                           input, actual_ratio, g_min_ratio, src_size, comp_size);
                }
                free(dst_buf);
                fclose(fin);
                if (fout != stdout) { fclose(fout); remove(write_path); }
                return 0;
            }
        }

        if (g_split_size > 0 && !g_stdout && comp_size > g_split_size) {
            /* Split output into multiple chunk files */
            fclose(fout); /* Close the main output file, we'll write chunks instead */
            remove(write_path); /* Remove the empty main file */
            
            size_t offset = 0;
            int chunk_num = 1;
            int n_chunks = (int)((comp_size + g_split_size - 1) / g_split_size);
            
            while (offset < comp_size) {
                size_t chunk_len = comp_size - offset;
                if (chunk_len > g_split_size) chunk_len = g_split_size;
                
                char chunk_name[1088];
                snprintf(chunk_name, sizeof(chunk_name), "%s.%03d", output, chunk_num);
                
                FILE* fc = fopen(chunk_name, "wb");
                if (!fc) {
                    fprintf(stderr, "Error: cannot create chunk '%s': %s\n", chunk_name, strerror(errno));
                    free(dst_buf);
                    fclose(fin);
                    return 1;
                }
                fwrite(dst_buf + offset, 1, chunk_len, fc);
                fclose(fc);
                
                if (!g_quiet) {
                    printf("  Chunk %d/%d: %s (%zu bytes)\n", chunk_num, n_chunks, chunk_name, chunk_len);
                }
                
                offset += chunk_len;
                chunk_num++;
            }
            total_out = comp_size;
            fout = NULL; /* Already closed */
        } else {
            fwrite(dst_buf, 1, comp_size, fout);
            total_out = comp_size;
        }
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
                cleanup_partial(write_path);
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
        if (g_verbose) {
            char membuf[32];
            printf("  Memory: %s peak RSS\n", fmt_mem(get_peak_rss_kb(), membuf, sizeof(membuf)));
        }
    }

    fclose(fin);
    if (fout) fclose(fout);

    /* --atomic: rename temp file to final output */
    if (g_atomic && !g_stdout && output && write_path != output) {
        if (rename(write_path, output) != 0) {
            fprintf(stderr, "Error: atomic rename '%s' → '%s' failed: %s\n",
                    write_path, output, strerror(errno));
            remove(write_path);
            return 1;
        }
    }

    /* Verify: decompress the output and compare with original */
    if (g_verify && !g_stdout && output) {
        size_t comp_sz;
        uint8_t* comp_data = read_file(output, &comp_sz);
        if (!comp_data) {
            fprintf(stderr, "  Verify FAILED: cannot read compressed output\n");
            return 1;
        }

        /* Read original for comparison */
        size_t orig_sz;
        uint8_t* orig_data = read_file(input, &orig_sz);
        if (!orig_data) {
            fprintf(stderr, "  Verify FAILED: cannot re-read original\n");
            free(comp_data);
            return 1;
        }

        uint8_t* dec_buf = (uint8_t*)malloc(orig_sz + 1024);
        if (!dec_buf) {
            fprintf(stderr, "  Verify FAILED: out of memory\n");
            free(comp_data); free(orig_data);
            return 1;
        }

        size_t dec_sz = mcx_decompress(dec_buf, orig_sz + 1024, comp_data, comp_sz);
        free(comp_data);

        if (mcx_is_error(dec_sz)) {
            fprintf(stderr, "  Verify FAILED: decompression error: %s\n", mcx_get_error_name(dec_sz));
            free(orig_data); free(dec_buf);
            return 1;
        }
        if (dec_sz != orig_sz || memcmp(orig_data, dec_buf, orig_sz) != 0) {
            fprintf(stderr, "  Verify FAILED: decompressed data does not match original!\n");
            free(orig_data); free(dec_buf);
            return 1;
        }
        free(orig_data); free(dec_buf);
        if (!g_quiet) printf("  Verified: roundtrip OK ✓\n");
    }

    /* Preserve source file mtime on output if --preserve-mtime */
    if (have_src_stat && !g_stdout && output) {
        struct utimbuf ut;
        ut.actime = src_stat.st_atime;
        ut.modtime = src_stat.st_mtime;
        if (g_split_size > 0) {
            /* Set mtime on all chunk files */
            for (int cn = 1; ; cn++) {
                char cname[1088];
                snprintf(cname, sizeof(cname), "%s.%03d", output, cn);
                if (access(cname, F_OK) != 0) break;
                utime(cname, &ut);
            }
        } else {
            utime(output, &ut);
        }
        if (!g_quiet) printf("  Preserved mtime from source\n");
    }

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

    /* Capture source file mtime for --preserve-mtime */
    struct stat dec_src_stat;
    int have_dec_src_stat = (g_preserve_mtime && fstat(fileno(fin), &dec_src_stat) == 0);

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

    double dt_speed = (dt_elapsed > 0.001) ? (double)total_out / (1024*1024) / dt_elapsed : 0;
    if (!g_quiet) printf("Done!\n");
    if (!g_quiet) {
        printf("  Decompressed: %zu bytes -> '%s'\n", total_out, output);
        if (dt_elapsed >= 0.01)
            printf("  Time:   %.2fs (%.1f MB/s)\n", dt_elapsed, dt_speed);
        if (g_verbose) {
            char membuf[32];
            printf("  Memory: %s peak RSS\n", fmt_mem(get_peak_rss_kb(), membuf, sizeof(membuf)));
        }
    }

    /* Verify: re-compress and re-decompress, compare with decompressed output */
    if (g_verify && total_out > 0) {
        size_t vcomp_cap = mcx_compress_bound(total_out);
        uint8_t* vcomp = (uint8_t*)malloc(vcomp_cap);
        if (vcomp) {
            size_t vcsz = mcx_compress(vcomp, vcomp_cap, dst_buf, total_out, MCX_LEVEL_DEFAULT);
            if (!mcx_is_error(vcsz)) {
                uint8_t* vdec = (uint8_t*)malloc(total_out + 1024);
                if (vdec) {
                    size_t vdsz = mcx_decompress(vdec, total_out + 1024, vcomp, vcsz);
                    if (!mcx_is_error(vdsz) && vdsz == total_out &&
                        memcmp(dst_buf, vdec, total_out) == 0) {
                        if (!g_quiet) printf("  Verify: OK (re-compress roundtrip passed)\n");
                    } else {
                        fprintf(stderr, "  Verify: FAILED — re-compress roundtrip mismatch!\n");
                        free(vdec); free(vcomp); free(dst_buf);
                        fclose(fin); fclose(fout);
                        return 1;
                    }
                    free(vdec);
                }
            } else {
                fprintf(stderr, "  Verify: FAILED — re-compression error: %s\n",
                        mcx_get_error_name(vcsz));
                free(vcomp); free(dst_buf);
                fclose(fin); fclose(fout);
                return 1;
            }
            free(vcomp);
        }
    }

    free(dst_buf);
    fclose(fin);
    fclose(fout);

    /* Preserve source file mtime on output if --preserve-mtime */
    if (have_dec_src_stat && !g_stdout && output) {
        struct utimbuf ut;
        ut.actime = dec_src_stat.st_atime;
        ut.modtime = dec_src_stat.st_mtime;
        utime(output, &ut);
        if (!g_quiet) printf("  Preserved mtime from source\n");
    }

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

/* ─── ls command ─────────────────────────────────────────────────────── */

static void json_print_string(const char* s);

static int cmd_ls(int argc, char** argv, int json)
{
    /* Collect non-flag arguments as files */
    const char* files[256];
    int n_files = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) continue;
        if (n_files < 256) files[n_files++] = argv[i];
    }

    if (n_files == 0) {
        fprintf(stderr, "Usage: mcx ls [--json] <file.mcx> [file2.mcx ...]\n");
        return 1;
    }

    if (json) {
        printf("[\n");
        int first = 1;
        int errors = 0;
        for (int i = 0; i < n_files; i++) {
            size_t src_size;
            uint8_t* src = read_file(files[i], &src_size);
            if (!src) { errors++; continue; }

            mcx_frame_info info;
            size_t r = mcx_get_frame_info(&info, src, src_size);
            if (mcx_is_error(r)) {
                free(src); errors++; continue;
            }

            double ratio = (info.original_size > 0)
                ? (double)info.original_size / src_size : 0;

            if (!first) printf(",\n");
            first = 0;
            printf("  {\n");
            printf("    \"file\": "); json_print_string(files[i]); printf(",\n");
            printf("    \"compressed_size\": %zu,\n", src_size);
            printf("    \"original_size\": %llu,\n", (unsigned long long)info.original_size);
            printf("    \"ratio\": %.4f,\n", ratio);
            printf("    \"level\": %u,\n", info.level);
            printf("    \"strategy\": \"%s\"\n", strategy_name(info.strategy));
            printf("  }");
            free(src);
        }
        printf("\n]\n");
        return errors > 0 ? 1 : 0;
    }

    printf("%-40s %10s %10s %7s %5s %-12s\n",
           "FILE", "COMPRESSED", "ORIGINAL", "RATIO", "LEVEL", "STRATEGY");
    printf("────────────────────────────────────────────────────────────────────────────────────────\n");

    int errors = 0;
    for (int i = 0; i < n_files; i++) {
        size_t src_size;
        uint8_t* src = read_file(files[i], &src_size);
        if (!src) { errors++; continue; }

        mcx_frame_info info;
        size_t r = mcx_get_frame_info(&info, src, src_size);
        if (mcx_is_error(r)) {
            fprintf(stderr, "%-40s  (not a valid MCX file)\n", files[i]);
            free(src); errors++; continue;
        }

        double ratio = (info.original_size > 0)
            ? (double)info.original_size / src_size : 0;

        printf("%-40s %10zu %10llu %6.2fx  L%-3u %-12s\n",
               files[i], src_size, (unsigned long long)info.original_size,
               ratio, info.level, strategy_name(info.strategy));
        free(src);
    }
    return errors > 0 ? 1 : 0;
}

static const char* entropy_coder_name(uint8_t ec) {
    switch (ec) {
        case 0: return "Huffman";
        case 1: return "rANS";
        case 2: return "CM-rANS";
        default: return "unknown";
    }
}

/* JSON string escaper (backslash, quotes) */
static void json_print_string(const char* s)
{
    putchar('"');
    for (; *s; s++) {
        switch (*s) {
            case '"':  printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n");  break;
            case '\t': printf("\\t");  break;
            default:   putchar(*s);    break;
        }
    }
    putchar('"');
}

static int cmd_info(const char* input, int json, int show_blocks)
{
    size_t src_size;
    uint8_t* src = read_file(input, &src_size);
    if (src == NULL) return 1;

    mcx_frame_info info;
    size_t r = mcx_get_frame_info(&info, src, src_size);
    if (mcx_is_error(r)) {
        if (json)
            printf("{\"error\":\"Not a valid MCX file\"}\n");
        else
            printf("Error: Not a valid MCX file\n");
        free(src);
        return 1;
    }

    if (json) {
        /* JSON output */
        printf("{\n");
        printf("  \"file\": "); json_print_string(input); printf(",\n");
        printf("  \"compressed_size\": %zu,\n", src_size);
        printf("  \"format_version\": %u,\n", info.version);
        printf("  \"level\": %u,\n", info.level);
        printf("  \"strategy\": \"%s\",\n", strategy_name(info.strategy));
        printf("  \"original_size\": %llu,\n", (unsigned long long)info.original_size);
        if (info.original_size > 0) {
            printf("  \"ratio\": %.4f,\n", (double)info.original_size / src_size);
            printf("  \"savings_pct\": %.2f,\n", 100.0 * (1.0 - (double)src_size / info.original_size));
        }
        printf("  \"flags\": {\n");
        printf("    \"e8e9\": %s,\n", (info.flags & MCX_FLAG_E8E9) ? "true" : "false");
        printf("    \"int_delta\": %s,\n", (info.flags & MCX_FLAG_INT_DELTA) ? "true" : "false");
        printf("    \"streaming\": %s,\n", (info.flags & MCX_FLAG_STREAMING) ? "true" : "false");
        printf("    \"adaptive_blocks\": %s\n", (info.flags & MCX_FLAG_ADAPTIVE_BLOCKS) ? "true" : "false");
        printf("  },\n");

        /* Parse blocks */
        uint8_t strat = info.strategy;
        int has_blocks = 0;
        if (strat != MCX_STRATEGY_STORE && strat != MCX_STRATEGY_FAST) {
            size_t offset = MCX_FRAME_HEADER_SIZE;
            if (offset + 4 <= src_size) {
                uint32_t num_blocks;
                memcpy(&num_blocks, src + offset, 4);
                offset += 4;
                printf("  \"num_blocks\": %u,\n", num_blocks);

                if (num_blocks > 0 && num_blocks <= 10000 &&
                    offset + (size_t)num_blocks * 4 <= src_size) {
                    uint32_t* bsizes = (uint32_t*)malloc(num_blocks * sizeof(uint32_t));
                    if (bsizes) {
                        for (uint32_t b = 0; b < num_blocks; b++)
                            memcpy(&bsizes[b], src + offset + b * 4, 4);
                        offset += (size_t)num_blocks * 4;
                        if (info.flags & MCX_FLAG_ADAPTIVE_BLOCKS)
                            offset += (size_t)num_blocks * 4;

                        if (show_blocks) {
                            printf("  \"blocks\": [\n");
                            for (uint32_t b = 0; b < num_blocks; b++) {
                                if (offset < src_size) {
                                    mcx_genome g = mcx_decode_genome(src[offset]);
                                    printf("    {\"id\": %u, \"compressed_size\": %u, "
                                           "\"bwt\": %s, \"mtf\": %s, \"delta\": %s, "
                                           "\"entropy\": \"%s\", \"cm_learning\": %u}%s\n",
                                           b, bsizes[b],
                                           g.use_bwt ? "true" : "false",
                                           g.use_mtf_rle ? "true" : "false",
                                           g.use_delta ? "true" : "false",
                                           entropy_coder_name(g.entropy_coder),
                                           g.cm_learning,
                                           (b + 1 < num_blocks) ? "," : "");
                                }
                                offset += bsizes[b];
                            }
                            printf("  ],\n");
                        } else {
                            for (uint32_t b = 0; b < num_blocks; b++)
                                offset += bsizes[b];
                        }
                        has_blocks = 1;
                        free(bsizes);
                    }
                }
            }
        }
        if (!has_blocks)
            printf("  \"num_blocks\": 0\n");
        printf("}\n");

        free(src);
        return 0;
    }

    /* Human-readable output (original) */
    printf("File:             %s\n", input);
    printf("Compressed size:  %zu bytes (%.1f KB)\n", src_size, src_size / 1024.0);
    printf("Format:           MCX v%u\n", info.version);
    printf("Level:            %u\n", info.level);
    printf("Strategy:         %s\n", strategy_name(info.strategy));

    if (info.original_size > 0) {
        printf("Original size:    %llu bytes (%.1f KB)\n",
               (unsigned long long)info.original_size, info.original_size / 1024.0);
        printf("Ratio:            %.2fx (%.1f%% smaller)\n",
               (double)info.original_size / src_size,
               100.0 * (1.0 - (double)src_size / info.original_size));
    }

    /* Flags */
    if (info.flags & MCX_FLAG_E8E9)
        printf("Filters:          E8/E9 x86\n");
    if (info.flags & MCX_FLAG_INT_DELTA)
        printf("Filters:          Int-delta (%d-bit)\n",
               (info.flags & MCX_FLAG_INT_DELTA_W4) ? 32 : 16);
    if (info.flags & MCX_FLAG_STREAMING)
        printf("Mode:             Streaming\n");

    /* Parse block table (for strategies that have blocks) */
    uint8_t strat = info.strategy;
    if (strat != MCX_STRATEGY_STORE && strat != MCX_STRATEGY_FAST) {
        size_t offset = MCX_FRAME_HEADER_SIZE;
        if (offset + 4 <= src_size) {
            uint32_t num_blocks;
            memcpy(&num_blocks, src + offset, 4);
            offset += 4;

            printf("Blocks:           %u%s\n", num_blocks,
                   (info.flags & MCX_FLAG_ADAPTIVE_BLOCKS) ? " (adaptive)" : "");

            if (num_blocks > 0 && num_blocks <= 10000 &&
                offset + (size_t)num_blocks * 4 <= src_size) {
                /* Read block size table */
                uint32_t* bsizes = (uint32_t*)malloc(num_blocks * sizeof(uint32_t));
                if (bsizes) {
                    for (uint32_t b = 0; b < num_blocks; b++) {
                        memcpy(&bsizes[b], src + offset + b * 4, 4);
                    }
                    offset += (size_t)num_blocks * 4;

                    /* Skip original block sizes array if adaptive blocks */
                    if (info.flags & MCX_FLAG_ADAPTIVE_BLOCKS) {
                        offset += (size_t)num_blocks * 4;
                    }

                    /* Parse each block's genome byte */
                    if (show_blocks) {
                        printf("\n  %-6s %10s  %-6s %-5s %-5s %-7s %-8s\n",
                               "Block", "Comp.Size", "BWT", "MTF", "Delta", "Entropy", "CM-LR");
                        printf("  ──────────────────────────────────────────────────────────\n");
                    }

                    for (uint32_t b = 0; b < num_blocks; b++) {
                        if (offset < src_size) {
                            mcx_genome g = mcx_decode_genome(src[offset]);
                            if (show_blocks) {
                                const char* ent = entropy_coder_name(g.entropy_coder);
                                /* cm_learning=6 means multi-table rANS, 7 means RLE2 pre-pass */
                                const char* note = "";
                                if (g.cm_learning == 6) note = "+multi";
                                else if (g.cm_learning == 7) note = "+rle2";

                                printf("  %-6u %10u  %-6s %-5s %-5s %-7s %u%s\n",
                                       b, bsizes[b],
                                       g.use_bwt ? "yes" : "no",
                                       g.use_mtf_rle ? "yes" : "no",
                                       g.use_delta ? "yes" : "no",
                                       ent,
                                       g.cm_learning, note);
                            }
                        }
                        offset += bsizes[b];
                    }
                    free(bsizes);
                }
            }
        }
    }

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

/* Run an external compressor and return compressed size, or 0 on failure */
static size_t run_external_compressor(const char* cmd_fmt, const char* input, double* elapsed)
{
    char cmd[1024];
    char tmpfile[] = "/tmp/mcx_bench_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) return 0;
    close(fd);

    snprintf(cmd, sizeof(cmd), cmd_fmt, input, tmpfile);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int ret = system(cmd);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    if (elapsed) *elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    size_t size = 0;
    if (ret == 0) {
        FILE* f = fopen(tmpfile, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fclose(f);
        }
    }
    unlink(tmpfile);
    return size;
}

static size_t parse_size_suffix(const char* s) {
    char* end = NULL;
    size_t val = (size_t)strtoull(s, &end, 10);
    if (end && (*end == 'k' || *end == 'K')) val *= 1024;
    else if (end && (*end == 'm' || *end == 'M')) val *= 1024 * 1024;
    else if (end && (*end == 'g' || *end == 'G')) val *= 1024 * 1024 * 1024;
    return val;
}

/* Sort modes for --sort flag */
#define BENCH_SORT_LEVEL 0
#define BENCH_SORT_RATIO 1
#define BENCH_SORT_SPEED 2

typedef struct {
    int level;
    size_t comp_size;
    double ratio;
    double saving;
    double comp_speed;
    double dec_speed;
    double comp_p5, comp_p50, comp_p95;  /* percentile speeds (MB/s) */
    double dec_p5, dec_p50, dec_p95;     /* percentile speeds (MB/s) */
    long peak_rss;
    int ok;
} BenchResult;

static int bench_cmp_ratio(const void* a, const void* b) {
    double ra = ((const BenchResult*)a)->ratio;
    double rb = ((const BenchResult*)b)->ratio;
    return (rb > ra) - (rb < ra); /* descending */
}
static int bench_cmp_speed(const void* a, const void* b) {
    double sa = ((const BenchResult*)a)->comp_speed;
    double sb = ((const BenchResult*)b)->comp_speed;
    return (sb > sa) - (sb < sa); /* descending */
}
static int bench_cmp_level(const void* a, const void* b) {
    return ((const BenchResult*)a)->level - ((const BenchResult*)b)->level;
}

static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

/* Helper: get percentile from sorted array (0-100) */
static double percentile_val(const double* sorted, int n, double p) {
    if (n <= 0) return 0;
    if (n == 1) return sorted[0];
    double idx = p / 100.0 * (n - 1);
    int lo = (int)idx;
    int hi = lo + 1;
    if (hi >= n) return sorted[n - 1];
    double frac = idx - lo;
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

static int cmd_bench(const char* input, int specific_level, int compare, int csv, int warmup, int json, int decode_only, int iterations, size_t max_size, int show_memory, int bench_all_levels, int ratio_only, int sort_mode, int top_n, int use_median, int show_percentile, int brief)
{
    size_t src_size;
    uint8_t* src = read_file(input, &src_size);
    if (!src) return 1;

    /* Truncate to --size if specified */
    if (max_size > 0 && src_size > max_size) {
        src_size = max_size;
    }

    if (json) {
        printf("{\n");
        printf("  \"file\": "); json_print_string(input); printf(",\n");
        printf("  \"original_bytes\": %zu,\n", src_size);
        printf("  \"results\": [\n");
    } else if (csv == 2) {
        /* Markdown header */
        printf("Benchmarking `%s` (%zu bytes / %zu KB)\n\n", input, src_size, src_size / 1024);
        if (ratio_only)
            printf("| Level | Compressed | Ratio | Saving |\n|-------|-----------|-------|--------|\n");
        else if (show_memory)
            printf("| Level | Compressed | Ratio | Saving | Comp MB/s | Dec MB/s | Peak RSS |\n|-------|-----------|-------|--------|-----------|----------|----------|\n");
        else
            printf("| Level | Compressed | Ratio | Saving | Comp MB/s | Dec MB/s |\n|-------|-----------|-------|--------|-----------|----------|\n");
    } else if (csv) {
        /* CSV header */
        if (show_memory)
            printf("file,original_bytes,compressor,level,compressed_bytes,ratio,saving_pct,comp_mbs,dec_mbs,peak_rss_kb");
        else
            printf("file,original_bytes,compressor,level,compressed_bytes,ratio,saving_pct,comp_mbs,dec_mbs");
        if (show_percentile)
            printf(",comp_p5,comp_p50,comp_p95,dec_p5,dec_p50,dec_p95");
        printf("\n");
    } else if (!brief) {
        printf("Benchmarking '%s' (%zu bytes / %zu KB)\n\n", input, src_size, src_size / 1024);
    }

    /* Run external compressors first if --compare */
    if (compare) {
        if (!csv) {
            printf("%-12s %10s %10s %8s %10s\n",
                   "Compressor", "Compressed", "Ratio", "Saving", "Time (s)");
            printf("──────────────────────────────────────────────────────\n");
        }

        struct { const char* name; const char* cmd; } externals[] = {
            {"gzip -6",   "gzip  -6 -c '%s' > '%s'"},
            {"gzip -9",   "gzip  -9 -c '%s' > '%s'"},
            {"bzip2 -9",  "bzip2 -9 -c '%s' > '%s'"},
            {"xz -6",     "xz    -6 -c '%s' > '%s'"},
            {"xz -9",     "xz    -9 -c '%s' > '%s'"},
        };
        int n_ext = sizeof(externals) / sizeof(externals[0]);

        int first_ext = 1;
        for (int i = 0; i < n_ext; i++) {
            double elapsed = 0;
            size_t csize = run_external_compressor(externals[i].cmd, input, &elapsed);
            if (csize > 0) {
                double ratio = (double)src_size / csize;
                double saving = 100.0 * (1.0 - (double)csize / src_size);
                if (json) {
                    if (!first_ext) printf(",\n");
                    printf("    {\"compressor\": "); json_print_string(externals[i].name);
                    printf(", \"compressed_bytes\": %zu, \"ratio\": %.4f, \"saving_pct\": %.2f, \"time_s\": %.2f}",
                           csize, ratio, saving, elapsed);
                    first_ext = 0;
                } else if (csv == 2) {
                    printf("| %s | %zu | %.2fx | %.1f%% | - | - |\n",
                           externals[i].name, csize, ratio, saving);
                } else if (csv) {
                    printf("%s,%zu,%s,0,%zu,%.4f,%.2f,0,0\n",
                           input, src_size, externals[i].name, csize, ratio, saving);
                } else {
                    printf("%-12s %10zu %9.2fx %7.1f%% %9.2f\n",
                           externals[i].name, csize, ratio, saving, elapsed);
                }
            } else if (!csv && !json) {
                printf("%-12s %10s\n", externals[i].name, "(not found)");
            }
        }
        if (json && !first_ext) printf(",\n");
        if (!csv && !json) printf("\n");
    }

    if (!csv && !json && !brief) {
        if (ratio_only) {
            printf("%-6s %10s %10s %8s\n",
                   "Level", "Compressed", "Ratio", "Saving");
            printf("─────────────────────────────────────────\n");
        } else if (show_memory) {
            printf("%-6s %10s %10s %8s %10s %10s %10s\n",
                   "Level", "Compressed", "Ratio", "Saving", "Comp MB/s", "Dec MB/s", "Peak RSS");
            printf("──────────────────────────────────────────────────────────────────────\n");
        } else {
            printf("%-6s %10s %10s %8s %10s %10s\n",
                   "Level", "Compressed", "Ratio", "Saving", "Comp MB/s", "Dec MB/s");
            printf("─────────────────────────────────────────────────────────────────\n");
        }
    }

    int default_levels[] = {1, 3, 6, 9, 12, 20, 24, 26};
    int n_default = sizeof(default_levels) / sizeof(default_levels[0]);

    /* Build full level list 1-26 for --all-levels */
    int full_levels[26];
    for (int li = 0; li < 26; li++) full_levels[li] = li + 1;

    /* If a specific level is requested, bench only that level */
    int* levels;
    int n_levels;
    if (specific_level > 0) {
        levels = &specific_level;
        n_levels = 1;
    } else if (bench_all_levels) {
        levels = full_levels;
        n_levels = 26;
    } else {
        levels = default_levels;
        n_levels = n_default;
    }

    size_t comp_cap = mcx_compress_bound(src_size);
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* dec = (uint8_t*)malloc(src_size + 64);
    BenchResult* results = (BenchResult*)calloc(n_levels, sizeof(BenchResult));
    if (!comp || !dec || !results) {
        fprintf(stderr, "Error: out of memory\n");
        free(src); free(comp); free(dec); free(results);
        return 1;
    }
    int n_results = 0;

    for (int i = 0; i < n_levels; i++) {
        int level = levels[i];

        /* Warmup iteration (reduces cold-cache variance) */
        if ((warmup || decode_only) && !ratio_only) {
            size_t ws = mcx_compress(comp, comp_cap, src, src_size, level);
            if (!mcx_is_error(ws))
                mcx_decompress(dec, src_size + 64, comp, ws);
        }

        /* Determine iteration counts */
        int comp_iters = (decode_only || ratio_only) ? 0 : (iterations > 0 ? iterations : 1);
        int dec_iter_count = ratio_only ? 0 : (iterations > 0 ? iterations : (decode_only ? 3 : 1));

        /* Compress (or use pre-compressed data in decode-only mode) */
        double comp_time = 0;
        size_t comp_size;
        double comp_p5 = 0, comp_p50 = 0, comp_p95 = 0;
        if (decode_only || ratio_only) {
            /* Pre-compress once (not timed) */
            comp_size = mcx_compress(comp, comp_cap, src, src_size, level);
        } else if ((use_median || show_percentile) && comp_iters > 1) {
            /* Per-iteration timing for median/percentile */
            double* ctimes = (double*)malloc(comp_iters * sizeof(double));
            for (int ci = 0; ci < comp_iters; ci++) {
                double t0 = bench_time();
                comp_size = mcx_compress(comp, comp_cap, src, src_size, level);
                double t1 = bench_time();
                ctimes[ci] = t1 - t0;
            }
            qsort(ctimes, comp_iters, sizeof(double), cmp_double);
            comp_time = ctimes[comp_iters / 2]; /* median */
            if (show_percentile && comp_iters >= 5) {
                double mb = src_size / 1048576.0;
                double t5 = percentile_val(ctimes, comp_iters, 5);
                double t50 = percentile_val(ctimes, comp_iters, 50);
                double t95 = percentile_val(ctimes, comp_iters, 95);
                comp_p5  = (t5  > 0) ? mb / t5  : 0;
                comp_p50 = (t50 > 0) ? mb / t50 : 0;
                comp_p95 = (t95 > 0) ? mb / t95 : 0;
            }
            free(ctimes);
        } else {
            double t0 = bench_time();
            for (int ci = 0; ci < comp_iters; ci++)
                comp_size = mcx_compress(comp, comp_cap, src, src_size, level);
            double t1 = bench_time();
            comp_time = (t1 - t0) / comp_iters;
        }

        if (mcx_is_error(comp_size)) {
            printf("L%-5d  ERROR: %s\n", level, mcx_get_error_name(comp_size));
            continue;
        }

        double comp_speed = (comp_time > 0) ? src_size / comp_time / 1048576.0 : 0;

        /* Decompress — run multiple iterations */
        double dec_time = 0;
        double dec_speed = 0;
        double dec_p5 = 0, dec_p50 = 0, dec_p95 = 0;
        size_t dec_size = 0;
        int ok = 1;
        if (!ratio_only) {
            if ((use_median || show_percentile) && dec_iter_count > 1) {
                /* Per-iteration timing for median/percentile */
                double* dtimes = (double*)malloc(dec_iter_count * sizeof(double));
                for (int di = 0; di < dec_iter_count; di++) {
                    double t0 = bench_time();
                    dec_size = mcx_decompress(dec, src_size + 64, comp, comp_size);
                    double t1 = bench_time();
                    dtimes[di] = t1 - t0;
                }
                qsort(dtimes, dec_iter_count, sizeof(double), cmp_double);
                dec_time = dtimes[dec_iter_count / 2]; /* median */
                if (show_percentile && dec_iter_count >= 5) {
                    double mb = src_size / 1048576.0;
                    double t5 = percentile_val(dtimes, dec_iter_count, 5);
                    double t50 = percentile_val(dtimes, dec_iter_count, 50);
                    double t95 = percentile_val(dtimes, dec_iter_count, 95);
                    dec_p5  = (t5  > 0) ? mb / t5  : 0;
                    dec_p50 = (t50 > 0) ? mb / t50 : 0;
                    dec_p95 = (t95 > 0) ? mb / t95 : 0;
                }
                free(dtimes);
            } else {
                double t0 = bench_time();
                for (int di = 0; di < dec_iter_count; di++) {
                    dec_size = mcx_decompress(dec, src_size + 64, comp, comp_size);
                }
                double t1 = bench_time();
                dec_time = (t1 - t0) / dec_iter_count;
            }
            dec_speed = src_size / dec_time / 1048576.0;

            /* Verify */
            ok = (!mcx_is_error(dec_size) && dec_size == src_size &&
                      memcmp(src, dec, src_size) == 0);
        }

        double ratio = (double)src_size / comp_size;
        double saving = 100.0 * (1.0 - (double)comp_size / src_size);

        long peak_rss = show_memory ? get_peak_rss_kb() : 0;

        /* Collect result */
        results[n_results].level = level;
        results[n_results].comp_size = comp_size;
        results[n_results].ratio = ratio;
        results[n_results].saving = saving;
        results[n_results].comp_speed = comp_speed;
        results[n_results].dec_speed = dec_speed;
        results[n_results].comp_p5 = comp_p5;
        results[n_results].comp_p50 = comp_p50;
        results[n_results].comp_p95 = comp_p95;
        results[n_results].dec_p5 = dec_p5;
        results[n_results].dec_p50 = dec_p50;
        results[n_results].dec_p95 = dec_p95;
        results[n_results].peak_rss = peak_rss;
        results[n_results].ok = ok;
        n_results++;
    }

    /* Sort results if requested */
    if (sort_mode == BENCH_SORT_RATIO)
        qsort(results, n_results, sizeof(BenchResult), bench_cmp_ratio);
    else if (sort_mode == BENCH_SORT_SPEED)
        qsort(results, n_results, sizeof(BenchResult), bench_cmp_speed);
    /* BENCH_SORT_LEVEL is already in order */

    /* Print results (limit to --top N if specified) */
    int print_count = n_results;
    if (top_n > 0 && top_n < print_count) print_count = top_n;
    for (int i = 0; i < print_count; i++) {
        BenchResult* r = &results[i];
        if (json) {
            if (i > 0) printf(",\n");
            printf("    {\"compressor\": \"mcx\", \"level\": %d, \"compressed_bytes\": %zu, "
                   "\"ratio\": %.4f, \"saving_pct\": %.2f, \"comp_mbs\": %.1f, \"dec_mbs\": %.1f, "
                   "\"verified\": %s",
                   r->level, r->comp_size, r->ratio, r->saving, r->comp_speed, r->dec_speed,
                   r->ok ? "true" : "false");
            if (show_memory) printf(", \"peak_rss_kb\": %ld", r->peak_rss);
            if (show_percentile && r->comp_p5 > 0) {
                printf(", \"comp_p5\": %.1f, \"comp_p50\": %.1f, \"comp_p95\": %.1f",
                       r->comp_p5, r->comp_p50, r->comp_p95);
                printf(", \"dec_p5\": %.1f, \"dec_p50\": %.1f, \"dec_p95\": %.1f",
                       r->dec_p5, r->dec_p50, r->dec_p95);
            }
            printf("}");
        } else if (csv == 2) {
            /* Markdown table row */
            char membuf[32];
            if (ratio_only) {
                printf("| L%d | %zu | %.2fx | %.1f%% |\n",
                       r->level, r->comp_size, r->ratio, r->saving);
            } else if (show_memory) {
                printf("| L%d | %zu | %.2fx | %.1f%% | %.1f | %.1f | %s |\n",
                       r->level, r->comp_size, r->ratio, r->saving,
                       r->comp_speed, r->dec_speed,
                       fmt_mem(r->peak_rss, membuf, sizeof(membuf)));
            } else {
                printf("| L%d | %zu | %.2fx | %.1f%% | %.1f | %.1f |\n",
                       r->level, r->comp_size, r->ratio, r->saving,
                       r->comp_speed, r->dec_speed);
            }
        } else if (csv) {
            if (show_memory) {
                printf("%s,%zu,mcx,%d,%zu,%.4f,%.2f,%.1f,%.1f,%ld",
                       input, src_size, r->level, r->comp_size, r->ratio, r->saving,
                       r->comp_speed, r->dec_speed, r->peak_rss);
            } else {
                printf("%s,%zu,mcx,%d,%zu,%.4f,%.2f,%.1f,%.1f",
                       input, src_size, r->level, r->comp_size, r->ratio, r->saving,
                       r->comp_speed, r->dec_speed);
            }
            if (show_percentile && r->comp_p5 > 0)
                printf(",%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
                       r->comp_p5, r->comp_p50, r->comp_p95,
                       r->dec_p5, r->dec_p50, r->dec_p95);
            printf("\n");
        } else if (brief) {
            /* One-line compact output per level */
            if (ratio_only) {
                printf("L%d: %.2fx %.1f%%\n", r->level, r->ratio, r->saving);
            } else {
                printf("L%d: %.2fx %.1f/%.1f MB/s\n",
                       r->level, r->ratio, r->comp_speed, r->dec_speed);
            }
        } else {
            char membuf[32];
            if (ratio_only) {
                printf("L%-5d %10zu %9.2fx %7.1f%%\n",
                       r->level, r->comp_size, r->ratio, r->saving);
            } else if (show_percentile && r->comp_p5 > 0) {
                printf("L%-5d %10zu %9.2fx %7.1f%% C[p5=%.1f p50=%.1f p95=%.1f] D[p5=%.1f p50=%.1f p95=%.1f] %s\n",
                       r->level, r->comp_size, r->ratio, r->saving,
                       r->comp_p5, r->comp_p50, r->comp_p95,
                       r->dec_p5, r->dec_p50, r->dec_p95,
                       r->ok ? "" : " FAIL!");
            } else if (show_memory) {
                printf("L%-5d %10zu %9.2fx %7.1f%% %9.1f %9.1f %10s %s\n",
                       r->level, r->comp_size, r->ratio, r->saving, r->comp_speed, r->dec_speed,
                       fmt_mem(r->peak_rss, membuf, sizeof(membuf)),
                       r->ok ? "" : " FAIL!");
            } else {
                printf("L%-5d %10zu %9.2fx %7.1f%% %9.1f %9.1f %s\n",
                       r->level, r->comp_size, r->ratio, r->saving, r->comp_speed, r->dec_speed,
                       r->ok ? "" : " FAIL!");
            }
        }
    }

    if (json) {
        printf("\n  ]\n}\n");
    } else if (!csv && !brief) {
        printf("\n");
    }
    free(src); free(comp); free(dec); free(results);
    return 0;
}

/* ─── Bench --histogram: compressed size vs block size ────────────────── */

static int cmd_bench_histogram(const char* input, int level) {
    size_t src_size;
    uint8_t* src = read_file(input, &src_size);
    if (!src) return 1;

    /* Default to L12 (BWT) since block size mainly affects BWT levels */
    if (level == 0) level = 12;

    /* Block sizes to try: 64K to 64M (powers of 4) */
    static const size_t block_sizes[] = {
        64*1024, 256*1024, 1024*1024, 4*1024*1024, 16*1024*1024, 64*1024*1024
    };
    static const char* block_labels[] = {
        "64K", "256K", "1M", "4M", "16M", "64M"
    };
    int n_sizes = sizeof(block_sizes) / sizeof(block_sizes[0]);

    printf("Block size histogram: '%s' (%zu bytes) at L%d\n\n", input, src_size, level);
    printf("%-10s %12s %9s %7s  %s\n", "BlockSize", "Compressed", "Ratio", "Save%", "Bar");
    printf("%-10s %12s %9s %7s  %s\n", "---------", "----------", "-----", "-----", "---");

    extern size_t mcx_block_size_override;
    size_t saved_override = mcx_block_size_override;
    size_t max_comp = mcx_compress_bound(src_size);
    uint8_t* comp = (uint8_t*)malloc(max_comp);
    if (!comp) { free(src); return 1; }

    size_t best_size = (size_t)-1;
    const char* best_label = "";

    for (int i = 0; i < n_sizes; i++) {
        /* Skip block sizes larger than input (would be identical to previous) */
        if (i > 0 && block_sizes[i] > src_size && block_sizes[i-1] >= src_size)
            continue;

        mcx_block_size_override = block_sizes[i];
        size_t comp_size = mcx_compress(comp, max_comp, src, src_size, level);
        if (mcx_is_error(comp_size)) {
            printf("%-10s %12s %9s %7s  (error)\n", block_labels[i], "-", "-", "-");
            continue;
        }

        double ratio = (double)src_size / comp_size;
        double saving = (1.0 - (double)comp_size / src_size) * 100.0;

        /* Bar: scale to 40 chars, based on ratio 1.0-6.0 range */
        int bar_len = (int)((ratio - 1.0) / 5.0 * 40.0);
        if (bar_len < 0) bar_len = 0;
        if (bar_len > 40) bar_len = 40;
        char bar[41];
        for (int b = 0; b < bar_len; b++) bar[b] = '#';
        bar[bar_len] = '\0';

        printf("%-10s %12zu %8.2fx %6.1f%%  %s\n",
               block_labels[i], comp_size, ratio, saving, bar);

        if (comp_size < best_size) {
            best_size = comp_size;
            best_label = block_labels[i];
        }
    }

    printf("\nBest block size: %s (%zu bytes, %.2fx)\n",
           best_label, best_size, (double)src_size / best_size);

    mcx_block_size_override = saved_override;
    free(comp);
    free(src);
    return 0;
}

/* ─── CRC32 (IEEE 802.3) ─────────────────────────────────────────────── */

static uint32_t g_crc32_table[256];
static int g_crc32_ready = 0;

static void init_crc32(void) {
    if (g_crc32_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (0xEDB88320 & (-(c & 1)));
        g_crc32_table[i] = c;
    }
    g_crc32_ready = 1;
}

static uint32_t compute_crc32(const uint8_t* data, size_t len) {
    init_crc32();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = g_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* FNV-1a 64-bit hash */
static uint64_t compute_fnv1a64(const uint8_t* data, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* ─── Hash command ───────────────────────────────────────────────────── */

static int cmd_hash(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: mcx hash <file.mcx> [file2.mcx ...]\n"
                "  Show CRC32 and FNV-1a hash of decompressed content.\n");
        return 1;
    }

    int errors = 0;
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') continue; /* skip flags */

        size_t src_size;
        uint8_t* src = read_file(argv[i], &src_size);
        if (!src) { errors++; continue; }

        /* Check if it's a valid MCX file */
        mcx_frame_info info;
        size_t r = mcx_get_frame_info(&info, src, src_size);
        if (mcx_is_error(r)) {
            /* Not an MCX file — hash the raw file */
            uint32_t crc = compute_crc32(src, src_size);
            uint64_t fnv = compute_fnv1a64(src, src_size);
            printf("%08x  %016llx  %s (raw, %zu bytes)\n",
                   crc, (unsigned long long)fnv, argv[i], src_size);
            free(src);
            continue;
        }

        /* Decompress in memory */
        uint8_t* dec = (uint8_t*)malloc((size_t)info.original_size + 1024);
        if (!dec) {
            fprintf(stderr, "Error: out of memory for '%s'\n", argv[i]);
            free(src); errors++; continue;
        }

        size_t dsz = mcx_decompress(dec, (size_t)info.original_size + 1024, src, src_size);
        free(src);

        if (mcx_is_error(dsz)) {
            fprintf(stderr, "Error: decompression failed for '%s': %s\n", argv[i], mcx_get_error_name(dsz));
            free(dec); errors++; continue;
        }

        uint32_t crc = compute_crc32(dec, dsz);
        uint64_t fnv = compute_fnv1a64(dec, dsz);
        printf("%08x  %016llx  %s (%zu bytes decompressed)\n",
               crc, (unsigned long long)fnv, argv[i], dsz);
        free(dec);
    }
    return errors > 0 ? 1 : 0;
}

/* ─── Checksum command ───────────────────────────────────────────────── */

static int cmd_checksum(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: mcx checksum <file.mcx> [file2.mcx ...]\n"
                "  Show CRC32 of compressed file and verify header integrity.\n");
        return 1;
    }

    int errors = 0;
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') continue;

        size_t src_size;
        uint8_t* src = read_file(argv[i], &src_size);
        if (!src) { errors++; continue; }

        if (src_size < MCX_FRAME_HEADER_SIZE) {
            fprintf(stderr, "%s: FAIL (too small, %zu bytes)\n", argv[i], src_size);
            free(src); errors++; continue;
        }

        /* Check magic number */
        uint32_t magic;
        memcpy(&magic, src, 4);
        if (magic != 0x0158434D) { /* "MCX\x01" LE */
            fprintf(stderr, "%s: FAIL (bad magic: 0x%08X, not an MCX file)\n", argv[i], magic);
            free(src); errors++; continue;
        }

        /* Parse header */
        uint8_t version  = src[4];
        uint8_t level    = src[6];
        uint8_t strategy = src[7];
        uint64_t orig_size;
        memcpy(&orig_size, src + 8, 8);

        /* CRC32 of entire compressed file (for transfer verification) */
        uint32_t file_crc = compute_crc32(src, src_size);

        printf("%s: %08x  v%u L%u %s  %zu → %llu bytes (%.2fx)\n",
               argv[i], file_crc, version, level, strategy_name(strategy),
               src_size, (unsigned long long)orig_size,
               orig_size > 0 ? (double)orig_size / src_size : 0.0);

        free(src);
    }
    return errors > 0 ? 1 : 0;
}

/* ─── Stat command ───────────────────────────────────────────────────── */

static int cmd_stat(const char* input, int json)
{
    size_t size;
    uint8_t* data = read_file(input, &size);
    if (!data) return 1;

    /* Byte frequency distribution */
    uint32_t freq[256];
    memset(freq, 0, sizeof(freq));
    for (size_t i = 0; i < size; i++) freq[data[i]]++;

    /* Count unique bytes */
    int unique = 0;
    for (int i = 0; i < 256; i++) if (freq[i] > 0) unique++;

    /* Shannon entropy */
    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / size;
        entropy -= p * log2(p);
    }

    /* Count runs (consecutive identical bytes) */
    size_t runs = 0;
    size_t max_run = 0;
    size_t cur_run = 1;
    for (size_t i = 1; i < size; i++) {
        if (data[i] == data[i - 1]) {
            cur_run++;
        } else {
            if (cur_run >= 4) runs++;
            if (cur_run > max_run) max_run = cur_run;
            cur_run = 1;
        }
    }
    if (cur_run >= 4) runs++;
    if (cur_run > max_run) max_run = cur_run;

    /* Detect if text or binary */
    int text_bytes = 0;
    for (size_t i = 0; i < size; i++) {
        uint8_t b = data[i];
        if ((b >= 32 && b <= 126) || b == '\n' || b == '\r' || b == '\t')
            text_bytes++;
    }
    double text_pct = 100.0 * text_bytes / size;

    /* Top 10 most frequent bytes */
    int top[10];
    int top_count = 0;
    uint8_t used[256];
    memset(used, 0, sizeof(used));
    for (int t = 0; t < 10 && t < unique; t++) {
        uint32_t best_freq = 0;
        int best_sym = 0;
        for (int i = 0; i < 256; i++) {
            if (!used[i] && freq[i] > best_freq) {
                best_freq = freq[i];
                best_sym = i;
            }
        }
        top[top_count++] = best_sym;
        used[best_sym] = 1;
    }

    /* Theoretical minimum size */
    double min_bits = entropy * size;
    size_t min_bytes = (size_t)(min_bits / 8.0 + 0.5);

    if (json) {
        printf("{\n");
        printf("  \"file\": "); json_print_string(input); printf(",\n");
        printf("  \"size\": %zu,\n", size);
        printf("  \"type\": \"%s\",\n",
               text_pct > 90 ? "text" : text_pct > 50 ? "mixed" : "binary");
        printf("  \"printable_pct\": %.1f,\n", text_pct);
        printf("  \"unique_bytes\": %d,\n", unique);
        printf("  \"entropy\": %.4f,\n", entropy);
        printf("  \"shannon_min_bytes\": %zu,\n", min_bytes);
        printf("  \"runs_ge4\": %zu,\n", runs);
        printf("  \"longest_run\": %zu,\n", max_run);
        printf("  \"top_bytes\": [\n");
        for (int t = 0; t < top_count; t++) {
            int s = top[t];
            double pct = 100.0 * freq[s] / size;
            printf("    {\"byte\": %d, \"count\": %u, \"pct\": %.2f}%s\n",
                   s, freq[s], pct, (t + 1 < top_count) ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
        free(data);
        return 0;
    }

    printf("File:           %s\n", input);
    printf("Size:           %zu bytes (%.1f KB)\n", size, size / 1024.0);
    printf("Type:           %s (%.0f%% printable)\n",
           text_pct > 90 ? "text" : text_pct > 50 ? "mixed" : "binary", text_pct);
    printf("Unique bytes:   %d / 256\n", unique);
    printf("Entropy:        %.3f bits/byte (max 8.000)\n", entropy);
    printf("Min size:       %zu bytes (%.1f%% of original) — Shannon limit\n",
           min_bytes, 100.0 * min_bytes / size);
    printf("Runs (≥4):      %zu (longest: %zu)\n", runs, max_run);

    printf("\nTop bytes:\n");
    for (int t = 0; t < top_count; t++) {
        int s = top[t];
        double pct = 100.0 * freq[s] / size;
        char display[8];
        if (s >= 32 && s <= 126)
            snprintf(display, sizeof(display), "'%c'", (char)s);
        else
            snprintf(display, sizeof(display), "0x%02X", s);
        printf("  %-6s  %8u  %5.1f%%\n", display, freq[s], pct);
    }

    free(data);
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
        int build_flag = (argc > 2 && (strcmp(argv[2], "--build") == 0 || strcmp(argv[2], "-b") == 0));
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
        printf("  OpenMP:     yes (v%d)\n", _OPENMP);
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
        if (build_flag) {
            /* Extended build info with --build flag */
            printf("\n  Build details:\n");
#ifdef __OPTIMIZE__
            printf("    Optimized:  yes\n");
#else
            printf("    Optimized:  no\n");
#endif
#ifdef NDEBUG
            printf("    Debug:      no (NDEBUG defined)\n");
#else
            printf("    Debug:      yes\n");
#endif
            /* SIMD support detected at compile time */
            printf("    SIMD:      ");
            int has_simd = 0;
#ifdef __SSE2__
            printf(" SSE2"); has_simd = 1;
#endif
#ifdef __SSE4_1__
            printf(" SSE4.1"); has_simd = 1;
#endif
#ifdef __SSE4_2__
            printf(" SSE4.2"); has_simd = 1;
#endif
#ifdef __AVX__
            printf(" AVX"); has_simd = 1;
#endif
#ifdef __AVX2__
            printf(" AVX2"); has_simd = 1;
#endif
#ifdef __AVX512F__
            printf(" AVX-512"); has_simd = 1;
#endif
#ifdef __ARM_NEON
            printf(" NEON"); has_simd = 1;
#endif
            if (!has_simd) printf(" none");
            printf("\n");
            printf("    C std:      C%ld\n", __STDC_VERSION__);
            printf("    Word size:  %zu-bit\n", sizeof(void*) * 8);
#ifdef MCX_USE_DIVSUFSORT
            printf("    BWT sort:   libdivsufsort (fast)\n");
#else
            printf("    BWT sort:   built-in\n");
#endif
        }
        return 0;
    }

    /* Commands */
    if (strcmp(argv[1], "compress") == 0) {
        int level = MCX_LEVEL_DEFAULT;
        const char* inputs[256];
        int n_inputs = 0;
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
                g_delete = 0;
            } else if (strcmp(argv[i], "--delete") == 0) {
                g_delete = 1;
            } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursive") == 0) {
                g_recursive = 1;
            } else if (strcmp(argv[i], "--verify") == 0) {
                g_verify = 1;
            } else if (strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "-n") == 0) {
                g_dryrun = 1;
            } else if (strcmp(argv[i], "--estimate") == 0) {
                g_estimate = 1;
            } else if (strcmp(argv[i], "--no-trials") == 0) {
                extern int mcx_no_trials;
                mcx_no_trials = 1;
            } else if (strcmp(argv[i], "--level-scan") == 0) {
                g_level_scan = 1;
            } else if (strcmp(argv[i], "--level-range") == 0 && i + 1 < argc) {
                /* Parse range like "1-6", "L1-L6", "3-12" */
                const char* range = argv[++i];
                int lo = 0, hi = 0;
                if (range[0] == 'L' || range[0] == 'l') range++;
                lo = atoi(range);
                const char* dash = strchr(range, '-');
                if (dash) {
                    dash++;
                    if (*dash == 'L' || *dash == 'l') dash++;
                    hi = atoi(dash);
                }
                if (lo < MCX_LEVEL_MIN || hi < lo || hi > MCX_LEVEL_MAX) {
                    fprintf(stderr, "Error: --level-range must be LO-HI (e.g. 1-6, L3-L12)\n");
                    return 1;
                }
                g_level_range_lo = lo;
                g_level_range_hi = hi;
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
                g_verbose = 1;
            } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--threads") == 0) {
                if (i + 1 < argc) g_threads = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--block-size") == 0 && i + 1 < argc) {
                /* Parse block size with optional K/M suffix */
                char* endp;
                long val = strtol(argv[++i], &endp, 10);
                if (*endp == 'K' || *endp == 'k') val *= 1024;
                else if (*endp == 'M' || *endp == 'm') val *= 1024 * 1024;
                if (val < 64 * 1024 || val > 256 * 1024 * 1024) {
                    fprintf(stderr, "Error: block size must be 64K-256M (got %ld)\n", val);
                    return 1;
                }
                extern size_t mcx_block_size_override;
                mcx_block_size_override = (size_t)val;
            } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--strategy") == 0) && i + 1 < argc) {
                const char* sname = argv[++i];
                if (strcmp(sname, "lz") == 0 || strcmp(sname, "lz77") == 0) {
                    if (level < 1 || level > 9) level = 6;  /* default LZ level */
                } else if (strcmp(sname, "bwt") == 0) {
                    if (level < 10 || level > 14) level = 12;
                } else if (strcmp(sname, "cm") == 0 || strcmp(sname, "best") == 0) {
                    if (level < 15 || level > 19) level = 18;
                } else if (strcmp(sname, "smart") == 0 || strcmp(sname, "auto") == 0) {
                    level = 20;
                } else if (strcmp(sname, "lzrc") == 0) {
                    level = 26;
                } else {
                    fprintf(stderr, "Error: unknown strategy '%s'\n"
                            "  Available: lz, bwt, cm, smart, lzrc\n", sname);
                    return 1;
                }
            } else if (strcmp(argv[i], "--preserve-mtime") == 0) {
                g_preserve_mtime = 1;
            } else if (strcmp(argv[i], "--atomic") == 0) {
                g_atomic = 1;
            } else if (strcmp(argv[i], "--keep-broken") == 0) {
                g_keep_broken = 1;
            } else if (strcmp(argv[i], "--exclude") == 0 && i + 1 < argc) {
                g_exclude_pattern = argv[++i];
            } else if (strcmp(argv[i], "--min-ratio") == 0 && i + 1 < argc) {
                g_min_ratio = atof(argv[++i]);
                if (g_min_ratio <= 0.0) {
                    fprintf(stderr, "Error: --min-ratio must be > 0 (e.g. 1.5)\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--split") == 0 && i + 1 < argc) {
                g_split_size = parse_size_suffix(argv[++i]);
                if (g_split_size == 0) {
                    fprintf(stderr, "Error: --split size must be > 0 (e.g. 1M, 10M)\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
                const char* fname = argv[++i];
                extern int mcx_force_filter;
                if (strcmp(fname, "delta") == 0) {
                    mcx_force_filter = 1;
                } else if (strcmp(fname, "nibble") == 0) {
                    mcx_force_filter = 2;
                } else if (strcmp(fname, "none") == 0) {
                    mcx_force_filter = 3;
                } else if (strcmp(fname, "auto") == 0) {
                    mcx_force_filter = 0;
                } else {
                    fprintf(stderr, "Error: unknown filter '%s'\n"
                            "  Available: auto, delta, nibble, none\n", fname);
                    return 1;
                }
            } else if (argv[i][0] != '-') {
                if (n_inputs < 256) inputs[n_inputs++] = argv[i];
            }
        }

        if (n_inputs == 0) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s <file> [file2 ...]\n", argv[1]);
            return 1;
        }
#ifdef _OPENMP
        if (g_threads > 0) omp_set_num_threads(g_threads);
#endif
        /* Expand directories if --recursive */
        file_list_t expanded;
        file_list_init(&expanded);
        for (int f = 0; f < n_inputs; f++) {
            if (is_directory(inputs[f])) {
                if (!g_recursive) {
                    fprintf(stderr, "Error: '%s' is a directory (use -r to recurse)\n", inputs[f]);
                    file_list_free(&expanded);
                    return 1;
                }
                collect_files_recursive(inputs[f], &expanded, ".mcx", g_exclude_pattern);
            } else {
                file_list_add(&expanded, inputs[f]);
            }
        }
        if (expanded.count == 0) {
            fprintf(stderr, "Error: no files found to compress\n");
            file_list_free(&expanded);
            return 1;
        }
        if (!g_quiet && expanded.count > 1)
            printf("Compressing %d files...\n\n", expanded.count);

        /* Multi-file: compress each file separately (output name = input.mcx) */
        int errors = 0;
        for (int f = 0; f < expanded.count; f++) {
            const char* out = (expanded.count == 1) ? output : NULL;
            int ret = cmd_compress(expanded.paths[f], out, level);
            if (ret != 0) errors++;
        }
        file_list_free(&expanded);
        return errors > 0 ? 1 : 0;

    } else if (strcmp(argv[1], "decompress") == 0 || strcmp(argv[1], "extract") == 0 ||
               strcmp(argv[1], "x") == 0 || strcmp(argv[1], "d") == 0) {
        const char* inputs[256];
        int n_inputs = 0;
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
                g_delete = 0;
            } else if (strcmp(argv[i], "--delete") == 0) {
                g_delete = 1;
            } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursive") == 0) {
                g_recursive = 1;
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
                g_verbose = 1;
            } else if (strcmp(argv[i], "--verify") == 0) {
                g_verify = 1;
            } else if (strcmp(argv[i], "--preserve-mtime") == 0) {
                g_preserve_mtime = 1;
            } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--threads") == 0) {
                if (i + 1 < argc) g_threads = atoi(argv[++i]);
            } else if (argv[i][0] != '-') {
                if (n_inputs < 256) inputs[n_inputs++] = argv[i];
            }
        }

        if (n_inputs == 0) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s <file> [file2 ...]\n", argv[1]);
            return 1;
        }
#ifdef _OPENMP
        if (g_threads > 0) omp_set_num_threads(g_threads);
#endif
        /* Expand directories if --recursive */
        file_list_t dec_expanded;
        file_list_init(&dec_expanded);
        for (int f = 0; f < n_inputs; f++) {
            if (is_directory(inputs[f])) {
                if (!g_recursive) {
                    fprintf(stderr, "Error: '%s' is a directory (use -r to recurse)\n", inputs[f]);
                    file_list_free(&dec_expanded);
                    return 1;
                }
                /* For decompress, only collect .mcx files */
                file_list_t all_files;
                file_list_init(&all_files);
                collect_files_recursive(inputs[f], &all_files, NULL, NULL);
                for (int j = 0; j < all_files.count; j++) {
                    size_t plen = strlen(all_files.paths[j]);
                    if (plen > 4 && strcmp(all_files.paths[j] + plen - 4, ".mcx") == 0)
                        file_list_add(&dec_expanded, all_files.paths[j]);
                }
                file_list_free(&all_files);
            } else {
                file_list_add(&dec_expanded, inputs[f]);
            }
        }
        if (dec_expanded.count == 0) {
            fprintf(stderr, "Error: no .mcx files found to decompress\n");
            file_list_free(&dec_expanded);
            return 1;
        }
        if (!g_quiet && dec_expanded.count > 1)
            printf("Decompressing %d files...\n\n", dec_expanded.count);

        int errors = 0;
        for (int f = 0; f < dec_expanded.count; f++) {
            const char* out = (dec_expanded.count == 1) ? output : NULL;
            int ret = cmd_decompress(dec_expanded.paths[f], out);
            if (ret != 0) errors++;
        }
        file_list_free(&dec_expanded);
        return errors > 0 ? 1 : 0;

    } else if (strcmp(argv[1], "info") == 0) {
        int json_flag = 0;
        int blocks_flag = 0;
        const char* info_input = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json_flag = 1;
            else if (strcmp(argv[i], "--blocks") == 0) blocks_flag = 1;
            else info_input = argv[i];
        }
        if (!info_input) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx %s [--json] [--blocks] <file>\n", argv[1]);
            return 1;
        }
        return cmd_info(info_input, json_flag, blocks_flag);

    } else if (strcmp(argv[1], "ls") == 0 || strcmp(argv[1], "list") == 0) {
        int ls_json = 0;
        for (int i = 2; i < argc; i++)
            if (strcmp(argv[i], "--json") == 0) ls_json = 1;
        return cmd_ls(argc, argv, ls_json);

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
        if (mcx_is_error(dsz)) { fprintf(stderr, "Error: decompression failed for '%s': %s\n", argv[2], mcx_get_error_name(dsz)); free(dec); return 1; }
        fwrite(dec, 1, dsz, stdout);
        free(dec);
        return 0;

    } else if (strcmp(argv[1], "verify") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: mcx verify <file.mcx> [original]\n");
            return 1;
        }
        size_t src_size;
        uint8_t* src = read_file(argv[2], &src_size);
        if (!src) return 1;
        
        mcx_frame_info info;
        size_t r = mcx_get_frame_info(&info, src, src_size);
        if (mcx_is_error(r)) {
            fprintf(stderr, "Error: Not a valid MCX file\n");
            free(src); return 1;
        }
        
        uint8_t* dec = (uint8_t*)malloc((size_t)info.original_size + 1024);
        if (!dec) { fprintf(stderr, "Error: out of memory\n"); free(src); return 1; }
        
        double t0 = get_time_sec();
        size_t dsz = mcx_decompress(dec, (size_t)info.original_size + 1024, src, src_size);
        double dt = get_time_sec() - t0;
        free(src);
        
        if (mcx_is_error(dsz)) {
            fprintf(stderr, "FAIL: decompression error for '%s': %s\n", argv[2], mcx_get_error_name(dsz));
            free(dec); return 1;
        }
        
        printf("%s: OK (%.2fx, %zu → %zu bytes, %.1f MB/s)\n",
               argv[2], (double)dsz / src_size, src_size, dsz,
               dsz / dt / 1048576.0);
        
        /* If original file provided, compare */
        if (argc >= 4) {
            size_t orig_size;
            uint8_t* orig = read_file(argv[3], &orig_size);
            if (orig) {
                if (orig_size != dsz) {
                    fprintf(stderr, "FAIL: size mismatch (original %zu, decompressed %zu)\n",
                            orig_size, dsz);
                    free(orig); free(dec); return 1;
                }
                if (memcmp(orig, dec, dsz) != 0) {
                    fprintf(stderr, "FAIL: content mismatch\n");
                    free(orig); free(dec); return 1;
                }
                printf("  Verified against %s: MATCH\n", argv[3]);
                free(orig);
            }
        }
        
        free(dec);
        return 0;

    } else if (strcmp(argv[1], "diff") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: mcx diff <file1.mcx> <file2.mcx>\n");
            return 1;
        }
        size_t s1, s2;
        uint8_t* f1 = read_file(argv[2], &s1);
        uint8_t* f2 = read_file(argv[3], &s2);
        if (!f1 || !f2) { free(f1); free(f2); return 1; }
        mcx_frame_info i1, i2;
        size_t r1 = mcx_get_frame_info(&i1, f1, s1);
        size_t r2 = mcx_get_frame_info(&i2, f2, s2);
        if (mcx_is_error(r1) || mcx_is_error(r2)) {
            fprintf(stderr, "Error: one or both files are not valid MCX files\n");
            free(f1); free(f2); return 1;
        }
        printf("%-25s %s\n%-25s %s\n\n", "File A:", argv[2], "File B:", argv[3]);
        printf("%-20s %12s %12s %10s\n", "", "File A", "File B", "Diff");
        printf("────────────────────────────────────────────────────────\n");
        printf("%-20s %12zu %12zu %+9.1f%%\n", "Compressed", s1, s2,
               100.0 * ((double)s2 / s1 - 1.0));
        if (i1.original_size > 0 && i2.original_size > 0) {
            printf("%-20s %12llu %12llu\n", "Original",
                   i1.original_size, i2.original_size);
            printf("%-20s %11.2fx %11.2fx\n", "Ratio",
                   (double)i1.original_size / s1, (double)i2.original_size / s2);
        }
        printf("%-20s %12u %12u\n", "Level", i1.level, i2.level);
        printf("%-20s %12s %12s\n", "Strategy",
               strategy_name(i1.strategy), strategy_name(i2.strategy));
        long long diff = (long long)s2 - (long long)s1;
        printf("\nDelta: %+lld bytes (%s)\n", diff,
               diff < 0 ? "B is smaller ✓" : diff > 0 ? "A is smaller ✓" : "identical size");

        /* Decompress both and compare content byte-by-byte */
        if (i1.original_size > 0 && i2.original_size > 0) {
            uint8_t* d1 = (uint8_t*)malloc((size_t)i1.original_size);
            uint8_t* d2 = (uint8_t*)malloc((size_t)i2.original_size);
            if (d1 && d2) {
                size_t ds1 = mcx_decompress(d1, (size_t)i1.original_size, f1, s1);
                size_t ds2 = mcx_decompress(d2, (size_t)i2.original_size, f2, s2);
                if (!mcx_is_error(ds1) && !mcx_is_error(ds2)) {
                    if (ds1 == ds2 && memcmp(d1, d2, ds1) == 0) {
                        printf("\nContent: identical ✓\n");
                    } else {
                        /* Find and display first differences */
                        size_t min_sz = ds1 < ds2 ? ds1 : ds2;
                        size_t ndiffs = 0;
                        size_t first_diff = (size_t)-1;
                        for (size_t i = 0; i < min_sz; i++) {
                            if (d1[i] != d2[i]) {
                                ndiffs++;
                                if (first_diff == (size_t)-1) first_diff = i;
                            }
                        }
                        ndiffs += (ds1 > ds2) ? ds1 - ds2 : ds2 - ds1;
                        printf("\nContent: %zu byte(s) differ", ndiffs);
                        if (ds1 != ds2)
                            printf(" (sizes: %zu vs %zu)", ds1, ds2);
                        printf("\n");

                        /* Hex dump around first difference */
                        if (first_diff != (size_t)-1) {
                            size_t ctx_start = first_diff >= 16 ? first_diff - 16 : 0;
                            /* Align to 16-byte boundary */
                            ctx_start &= ~(size_t)0xF;
                            size_t ctx_end = first_diff + 48;
                            if (ctx_end > min_sz) ctx_end = min_sz;

                            printf("\nFirst difference at offset 0x%zx (%zu):\n", first_diff, first_diff);
                            printf("\n  File A:\n");
                            for (size_t row = ctx_start; row < ctx_end; row += 16) {
                                printf("  %08zx: ", row);
                                for (size_t col = 0; col < 16 && row + col < ctx_end && row + col < ds1; col++) {
                                    size_t off = row + col;
                                    if (off < min_sz && d1[off] != d2[off])
                                        printf(">%02x", d1[off]);
                                    else
                                        printf(" %02x", d1[off]);
                                }
                                printf("  |");
                                for (size_t col = 0; col < 16 && row + col < ctx_end && row + col < ds1; col++) {
                                    uint8_t c = d1[row + col];
                                    printf("%c", (c >= 32 && c < 127) ? c : '.');
                                }
                                printf("|\n");
                            }
                            printf("\n  File B:\n");
                            for (size_t row = ctx_start; row < ctx_end; row += 16) {
                                printf("  %08zx: ", row);
                                for (size_t col = 0; col < 16 && row + col < ctx_end && row + col < ds2; col++) {
                                    size_t off = row + col;
                                    if (off < min_sz && d1[off] != d2[off])
                                        printf(">%02x", d2[off]);
                                    else
                                        printf(" %02x", d2[off]);
                                }
                                printf("  |");
                                for (size_t col = 0; col < 16 && row + col < ctx_end && row + col < ds2; col++) {
                                    uint8_t c = d2[row + col];
                                    printf("%c", (c >= 32 && c < 127) ? c : '.');
                                }
                                printf("|\n");
                            }
                        }
                    }
                }
            }
            free(d1); free(d2);
        }
        free(f1); free(f2);
        return 0;

    } else if (strcmp(argv[1], "stat") == 0) {
        int json_flag = 0;
        const char* stat_input = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json_flag = 1;
            else stat_input = argv[i];
        }
        if (!stat_input) {
            fprintf(stderr, "Usage: mcx stat [--json] <file>\n");
            return 1;
        }
        return cmd_stat(stat_input, json_flag);

    } else if (strcmp(argv[1], "hash") == 0) {
        return cmd_hash(argc, argv);

    } else if (strcmp(argv[1], "checksum") == 0) {
        return cmd_checksum(argc, argv);

    } else if (strcmp(argv[1], "bench") == 0 || strcmp(argv[1], "compare") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx bench [-l LEVEL] <file>\n");
            return 1;
        }
        /* Parse optional -l/--level, --compare, and --csv flags */
        int bench_level = 0; /* 0 = all levels */
        int bench_compare = 0;
        int bench_csv = 0;
        int bench_warmup = 0;
        int bench_json = 0;
        int bench_decode_only = 0;
        int bench_iterations = 0; /* 0 = use default */
        int bench_memory = 0;
        int bench_all_levels = 0;
        int bench_ratio_only = 0;
        int bench_sort_mode = BENCH_SORT_LEVEL; /* default: sort by level */
        int bench_top_n = 0; /* 0 = show all results */
        int bench_median = 0;
        int bench_percentile = 0;
        int bench_histogram = 0;
        int bench_brief = 0;
        size_t bench_max_size = 0; /* 0 = no limit */
        const char* bench_file = NULL;
        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0) && i + 1 < argc) {
                bench_level = atoi(argv[++i]);
                if (bench_level < MCX_LEVEL_MIN || bench_level > MCX_LEVEL_MAX) {
                    fprintf(stderr, "Error: level must be %d-%d\n", MCX_LEVEL_MIN, MCX_LEVEL_MAX);
                    return 1;
                }
            } else if (strcmp(argv[i], "--compare") == 0) {
                bench_compare = 1;
            } else if (strcmp(argv[i], "--csv") == 0) {
                bench_csv = 1;
            } else if (strcmp(argv[i], "--json") == 0) {
                bench_json = 1;
            } else if (strcmp(argv[i], "--warmup") == 0) {
                bench_warmup = 1;
            } else if (strcmp(argv[i], "--decode-only") == 0) {
                bench_decode_only = 1;
            } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
                bench_iterations = atoi(argv[++i]);
                if (bench_iterations < 1) bench_iterations = 1;
            } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
                bench_max_size = parse_size_suffix(argv[++i]);
                if (bench_max_size == 0) {
                    fprintf(stderr, "Error: --size must be > 0 (e.g. 64K, 1M)\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--memory") == 0) {
                bench_memory = 1;
            } else if (strcmp(argv[i], "--all-levels") == 0) {
                bench_all_levels = 1;
            } else if (strcmp(argv[i], "--ratio-only") == 0) {
                bench_ratio_only = 1;
            } else if (strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
                const char* sval = argv[++i];
                if (strcmp(sval, "ratio") == 0) bench_sort_mode = BENCH_SORT_RATIO;
                else if (strcmp(sval, "speed") == 0) bench_sort_mode = BENCH_SORT_SPEED;
                else if (strcmp(sval, "level") == 0) bench_sort_mode = BENCH_SORT_LEVEL;
                else {
                    fprintf(stderr, "Error: --sort must be ratio, speed, or level\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
                bench_top_n = atoi(argv[++i]);
                if (bench_top_n < 1) {
                    fprintf(stderr, "Error: --top must be >= 1\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--median") == 0) {
                bench_median = 1;
            } else if (strcmp(argv[i], "--percentile") == 0) {
                bench_percentile = 1;
            } else if (strcmp(argv[i], "--histogram") == 0) {
                bench_histogram = 1;
            } else if (strcmp(argv[i], "--brief") == 0) {
                bench_brief = 1;
            } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
                const char* fmt = argv[++i];
                if (strcmp(fmt, "csv") == 0) { bench_csv = 1; bench_json = 0; }
                else if (strcmp(fmt, "json") == 0) { bench_json = 1; bench_csv = 0; }
                else if (strcmp(fmt, "table") == 0) { bench_csv = 0; bench_json = 0; }
                else if (strcmp(fmt, "markdown") == 0 || strcmp(fmt, "md") == 0) { bench_csv = 2; /* 2 = markdown */ bench_json = 0; }
                else {
                    fprintf(stderr, "Error: --format must be table, csv, json, or markdown\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--threads") == 0) {
                if (i + 1 < argc) g_threads = atoi(argv[++i]);
            } else if (!bench_file) {
                bench_file = argv[i];
            }
        }
        if (!bench_file) {
            fprintf(stderr, "Error: no input file specified\n  Usage: mcx bench [-l LEVEL] [--compare] [--csv] [--json] [--warmup] [--decode-only] [--iterations N] [--size SIZE] [--memory] [--all-levels] <file>\n");
            return 1;
        }
#ifdef _OPENMP
        if (g_threads > 0) omp_set_num_threads(g_threads);
#endif
        if (bench_histogram)
            return cmd_bench_histogram(bench_file, bench_level);
        return cmd_bench(bench_file, bench_level, bench_compare, bench_csv, bench_warmup, bench_json, bench_decode_only, bench_iterations, bench_max_size, bench_memory, bench_all_levels, bench_ratio_only, bench_sort_mode, bench_top_n, bench_median, bench_percentile, bench_brief);

    } else if (strcmp(argv[1], "test") == 0) {
        printf("MaxCompression v%s — Self-test\n\n", mcx_version_string());
        int pass = 0, fail = 0;
        int levels[] = {1, 3, 6, 9, 12};
        int n_levels = sizeof(levels) / sizeof(levels[0]);
        setvbuf(stdout, NULL, _IONBF, 0); /* Unbuffered for progress */
        
        /* ── Test patterns ── */
        #define TEST_SIZE 2048
        #define NUM_PATTERNS 7
        uint8_t* patterns[NUM_PATTERNS];
        size_t   pattern_sizes[NUM_PATTERNS];
        const char* pattern_names[NUM_PATTERNS];
        
        for (int p = 0; p < NUM_PATTERNS; p++)
            patterns[p] = (uint8_t*)malloc(TEST_SIZE);
        
        /* Pattern 0: Repeated text */
        {
            const char* text = "The quick brown fox jumps over the lazy dog. ";
            size_t tlen = strlen(text);
            for (size_t i = 0; i < TEST_SIZE; i++)
                patterns[0][i] = text[i % tlen];
            pattern_sizes[0] = TEST_SIZE;
            pattern_names[0] = "repeated text";
        }
        
        /* Pattern 1: All zeros (extreme RLE case) */
        {
            memset(patterns[1], 0, TEST_SIZE);
            pattern_sizes[1] = TEST_SIZE;
            pattern_names[1] = "all zeros";
        }
        
        /* Pattern 2: Sorted bytes (ascending 0..255 repeated) */
        {
            for (size_t i = 0; i < TEST_SIZE; i++)
                patterns[2][i] = (uint8_t)(i & 0xFF);
            pattern_sizes[2] = TEST_SIZE;
            pattern_names[2] = "sorted bytes";
        }
        
        /* Pattern 3: Pseudo-random (hard to compress) */
        {
            uint32_t seed = 0xDEADBEEF;
            for (size_t i = 0; i < TEST_SIZE; i++) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                patterns[3][i] = (uint8_t)(seed & 0xFF);
            }
            pattern_sizes[3] = TEST_SIZE;
            pattern_names[3] = "pseudo-random";
        }
        
        /* Pattern 4: Binary with structure (simulated executable) */
        {
            uint32_t s = 42;
            for (size_t i = 0; i < TEST_SIZE; i++) {
                if (i % 16 < 4) patterns[4][i] = 0xE8; /* x86 CALL opcode */
                else if (i % 16 < 8) patterns[4][i] = (uint8_t)(i * 7 + 3);
                else {
                    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                    patterns[4][i] = (uint8_t)(s & 0xFF);
                }
            }
            pattern_sizes[4] = TEST_SIZE;
            pattern_names[4] = "structured binary";
        }
        
        /* Pattern 5: Small input (32 bytes) */
        {
            const char* tiny = "Hello, MaxCompression test!!\n\x00\x01\xFF";
            memcpy(patterns[5], tiny, 32);
            pattern_sizes[5] = 32;
            pattern_names[5] = "tiny (32B)";
        }
        
        /* Pattern 6: Mixed text + repeated blocks (LZP candidate) */
        {
            const char* blocks[] = {
                "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ",
                "Sed do eiusmod tempor incididunt ut labore et dolore magna. ",
            };
            size_t pos = 0;
            for (int r = 0; r < 40 && pos < TEST_SIZE; r++) {
                const char* b = blocks[r % 2];
                size_t len = strlen(b);
                if (pos + len > TEST_SIZE) len = TEST_SIZE - pos;
                memcpy(patterns[6] + pos, b, len);
                pos += len;
            }
            /* Zero-fill remainder */
            if (pos < TEST_SIZE) memset(patterns[6] + pos, 0, TEST_SIZE - pos);
            pattern_sizes[6] = TEST_SIZE;
            pattern_names[6] = "mixed text + repeats";
        }
        
        size_t comp_cap = TEST_SIZE * 2 + 4096;
        uint8_t* comp = (uint8_t*)malloc(comp_cap);
        uint8_t* dec  = (uint8_t*)malloc(TEST_SIZE + 1024);
        
        for (int p = 0; p < NUM_PATTERNS; p++) {
            printf("  Pattern %d: %s (%zu bytes)\n", p, pattern_names[p], pattern_sizes[p]);
            for (int i = 0; i < n_levels; i++) {
                int lv = levels[i];
                size_t csz = mcx_compress(comp, comp_cap, patterns[p], pattern_sizes[p], lv);
                if (mcx_is_error(csz)) {
                    printf("    L%-2d  FAIL (compress error: %s)\n", lv, mcx_get_error_name(csz));
                    fail++;
                    continue;
                }
                /* Verify decompressed size API */
                unsigned long long reported = mcx_get_decompressed_size(comp, csz);
                if (reported != pattern_sizes[p]) {
                    printf("    L%-2d  FAIL (size mismatch: reported %llu, expected %zu)\n",
                           lv, reported, pattern_sizes[p]);
                    fail++;
                    continue;
                }
                size_t dsz = mcx_decompress(dec, TEST_SIZE + 1024, comp, csz);
                if (mcx_is_error(dsz) || dsz != pattern_sizes[p] ||
                    memcmp(patterns[p], dec, pattern_sizes[p]) != 0) {
                    printf("    L%-2d  FAIL (roundtrip mismatch)\n", lv);
                    fail++;
                    continue;
                }
                printf("    L%-2d  OK  %zu → %zu (%.2fx)\n", lv, pattern_sizes[p], csz,
                       (double)pattern_sizes[p] / csz);
                pass++;
            }
            printf("\n");
        }
        
        /* Cleanup */
        for (int p = 0; p < NUM_PATTERNS; p++)
            free(patterns[p]);
        free(comp);
        free(dec);
        
        printf("%d/%d passed%s\n", pass, pass + fail,
               fail ? " — FAILURES DETECTED" : " — all OK ✓");
        return fail > 0 ? 1 : 0;

    } else if (strcmp(argv[1], "upgrade") == 0 || strcmp(argv[1], "recompress") == 0) {
        /* Re-compress an .mcx file at a different level (decompress+recompress in one step) */
        int new_level = MCX_LEVEL_DEFAULT;
        int quiet = 0, force = 0, in_place = 0;
        const char* input_file = NULL;
        const char* output_path = NULL;
        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0) && i + 1 < argc) {
                new_level = atoi(argv[++i]);
                if (new_level < MCX_LEVEL_MIN || new_level > MCX_LEVEL_MAX) {
                    fprintf(stderr, "Error: level must be %d-%d\n", MCX_LEVEL_MIN, MCX_LEVEL_MAX);
                    return 1;
                }
            } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
                quiet = 1;
            } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
                force = 1;
            } else if (strcmp(argv[i], "--in-place") == 0 || strcmp(argv[i], "-i") == 0) {
                in_place = 1;
            } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
                output_path = argv[++i];
            } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--threads") == 0) {
                if (i + 1 < argc) g_threads = atoi(argv[++i]);
            } else if (!input_file) {
                input_file = argv[i];
            }
        }
        if (!input_file) {
            fprintf(stderr, "Usage: mcx upgrade [-l LEVEL] [-o output.mcx] [--in-place] <input.mcx>\n");
            return 1;
        }
        if (in_place && output_path) {
            fprintf(stderr, "Error: --in-place and -o cannot be used together\n");
            return 1;
        }
#ifdef _OPENMP
        if (g_threads > 0) omp_set_num_threads(g_threads);
#endif
        /* Read compressed input */
        size_t src_size;
        uint8_t* src = read_file(input_file, &src_size);
        if (!src) return 1;

        /* Get frame info for the old level */
        mcx_frame_info old_info;
        size_t r = mcx_get_frame_info(&old_info, src, src_size);
        if (mcx_is_error(r)) {
            fprintf(stderr, "Error: '%s' is not a valid MCX file\n", input_file);
            free(src); return 1;
        }

        if ((int)old_info.level == new_level && !force) {
            if (!quiet) printf("'%s' is already at level %d (use -f to force recompress)\n",
                               input_file, new_level);
            free(src); return 0;
        }

        /* Decompress */
        size_t orig_size = (size_t)old_info.original_size;
        uint8_t* decompressed = (uint8_t*)malloc(orig_size + 1024);
        if (!decompressed) { fprintf(stderr, "Error: out of memory\n"); free(src); return 1; }

        size_t dsz = mcx_decompress(decompressed, orig_size + 1024, src, src_size);
        if (mcx_is_error(dsz)) {
            fprintf(stderr, "Error: decompression failed: %s\n", mcx_get_error_name(dsz));
            free(src); free(decompressed); return 1;
        }

        /* Re-compress at new level */
        size_t comp_cap = mcx_compress_bound(dsz);
        uint8_t* compressed = (uint8_t*)malloc(comp_cap);
        if (!compressed) { fprintf(stderr, "Error: out of memory\n"); free(src); free(decompressed); return 1; }

        double t0 = get_time_sec();
        size_t csz = mcx_compress(compressed, comp_cap, decompressed, dsz, new_level);
        double dt = get_time_sec() - t0;

        if (mcx_is_error(csz)) {
            fprintf(stderr, "Error: recompression failed: %s\n", mcx_get_error_name(csz));
            free(src); free(decompressed); free(compressed); return 1;
        }

        /* Verify roundtrip */
        uint8_t* verify_buf = (uint8_t*)malloc(dsz + 1024);
        if (verify_buf) {
            size_t vsz = mcx_decompress(verify_buf, dsz + 1024, compressed, csz);
            if (mcx_is_error(vsz) || vsz != dsz || memcmp(decompressed, verify_buf, dsz) != 0) {
                fprintf(stderr, "Error: roundtrip verification failed after recompression!\n");
                free(src); free(decompressed); free(compressed); free(verify_buf);
                return 1;
            }
            free(verify_buf);
        }

        /* Determine output path */
        const char* out = output_path ? output_path : input_file;

        /* Write output */
        FILE* fp = fopen(out, "wb");
        if (!fp) {
            fprintf(stderr, "Error: cannot write '%s': %s\n", out, strerror(errno));
            free(src); free(decompressed); free(compressed); return 1;
        }
        fwrite(compressed, 1, csz, fp);
        fclose(fp);

        if (!quiet) {
            long long saved = (long long)src_size - (long long)csz;
            printf("Upgraded '%s': L%u → L%d, %zu → %zu bytes",
                   input_file, old_info.level, new_level, src_size, csz);
            if (saved > 0)
                printf(" (saved %lld bytes, %.1f%%)", saved, 100.0 * saved / src_size);
            else if (saved < 0)
                printf(" (grew %lld bytes)", -saved);
            printf(", %.1f MB/s\n", dsz / dt / 1048576.0);
        }

        free(src); free(decompressed); free(compressed);
        return 0;

    } else if (strcmp(argv[1], "pipe") == 0 ||
               (argc == 1) ||
               (argc >= 2 && strcmp(argv[1], "-") == 0)) {
        /* Pipe mode: read from stdin, write to stdout
         * mcx pipe [-l LEVEL]     — compress stdin → stdout
         * mcx pipe -d             — decompress stdin → stdout
         */
        int pipe_level = MCX_LEVEL_DEFAULT;
        int pipe_decompress = 0;
        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0) && i + 1 < argc) {
                pipe_level = atoi(argv[++i]);
            } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decompress") == 0) {
                pipe_decompress = 1;
            }
        }

        /* Read all of stdin into memory */
        size_t stdin_cap = 1024 * 1024; /* 1 MB initial */
        size_t stdin_len = 0;
        uint8_t* stdin_buf = (uint8_t*)malloc(stdin_cap);
        if (!stdin_buf) { fprintf(stderr, "Error: out of memory\n"); return 1; }

        while (!feof(stdin)) {
            if (stdin_len >= stdin_cap) {
                stdin_cap *= 2;
                uint8_t* tmp = (uint8_t*)realloc(stdin_buf, stdin_cap);
                if (!tmp) { fprintf(stderr, "Error: out of memory\n"); free(stdin_buf); return 1; }
                stdin_buf = tmp;
            }
            size_t n = fread(stdin_buf + stdin_len, 1, stdin_cap - stdin_len, stdin);
            if (n == 0) break;
            stdin_len += n;
        }

        if (stdin_len == 0) {
            fprintf(stderr, "Error: no data on stdin\n");
            free(stdin_buf); return 1;
        }

        if (pipe_decompress) {
            /* Decompress */
            unsigned long long orig = mcx_get_decompressed_size(stdin_buf, stdin_len);
            if (orig == 0) {
                fprintf(stderr, "Error: cannot determine decompressed size from stdin data\n");
                free(stdin_buf); return 1;
            }
            uint8_t* out = (uint8_t*)malloc((size_t)orig + 1024);
            if (!out) { fprintf(stderr, "Error: out of memory\n"); free(stdin_buf); return 1; }

            size_t dsz = mcx_decompress(out, (size_t)orig + 1024, stdin_buf, stdin_len);
            if (mcx_is_error(dsz)) {
                fprintf(stderr, "Error: decompression failed: %s\n", mcx_get_error_name(dsz));
                free(stdin_buf); free(out); return 1;
            }
            fwrite(out, 1, dsz, stdout);
            free(out);
        } else {
            /* Compress */
            size_t bound = mcx_compress_bound(stdin_len);
            uint8_t* out = (uint8_t*)malloc(bound);
            if (!out) { fprintf(stderr, "Error: out of memory\n"); free(stdin_buf); return 1; }

            size_t csz = mcx_compress(out, bound, stdin_buf, stdin_len, pipe_level);
            if (mcx_is_error(csz)) {
                fprintf(stderr, "Error: compression failed: %s\n", mcx_get_error_name(csz));
                free(stdin_buf); free(out); return 1;
            }
            fwrite(out, 1, csz, stdout);
            free(out);
        }

        free(stdin_buf);
        return 0;

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
            fprintf(stderr, "  Available commands: compress, decompress, info, cat, bench, test, upgrade, pipe\n");
        return 1;
    }
}
