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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>
#include "../lib/internal.h"

/* ─── Helpers ────────────────────────────────────────────────────────── */

static void print_usage(void)
{
    printf(
        "MaxCompression v%s\n"
        "\n"
        "Usage:\n"
        "  mcx compress  [-l LEVEL] <input> [-o output.mcx]\n"
        "  mcx decompress <input.mcx> [-o output]\n"
        "  mcx info       <input.mcx>\n"
        "  mcx --version\n"
        "  mcx --help\n"
        "\n"
        "Compression Levels:\n"
        "  %d (fastest) to %d (best compression), default: %d\n"
        "\n"
        "Examples:\n"
        "  mcx compress -l 6 myfile.txt\n"
        "  mcx decompress myfile.txt.mcx\n"
        "  mcx compress -l 19 bigfile.bin -o compressed.mcx\n"
        "\n",
        mcx_version_string(),
        MCX_LEVEL_MIN, MCX_LEVEL_MAX, MCX_LEVEL_DEFAULT
    );
}

static uint8_t* read_file(const char* path, size_t* size_out)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fprintf(stderr, "Error: empty or unreadable file '%s'\n", path);
        fclose(f);
        return NULL;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)file_size);
    if (data == NULL) {
        fprintf(stderr, "Error: out of memory (need %ld bytes)\n", file_size);
        fclose(f);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)file_size, f);
    fclose(f);

    if (read != (size_t)file_size) {
        fprintf(stderr, "Error: could not read entire file '%s'\n", path);
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
        fprintf(stderr, "Error: cannot create '%s'\n", path);
        return -1;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        fprintf(stderr, "Error: could not write entire file '%s'\n", path);
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

static int cmd_compress(const char* input, const char* output, int level)
{
    FILE* fin = fopen(input, "rb");
    if (!fin) {
        fprintf(stderr, "Error: cannot open '%s'\n", input);
        return 1;
    }
    
    char auto_output[1024];
    if (output == NULL) {
        make_compress_output(auto_output, sizeof(auto_output), input);
        output = auto_output;
    }
    
    FILE* fout = fopen(output, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot create '%s'\n", output);
        fclose(fin);
        return 1;
    }

    /* Fixed buffers for streaming */
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

    fseek(fin, 0, SEEK_END);
    size_t src_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    printf("Compressing '%s' (%zu bytes) at level %d... (Streaming API)\n", input, src_size, level);

    mcx_in_buffer in_b = { in_buf, 0, 0 };
    mcx_out_buffer out_b = { out_buf, OUT_CHUNK_SIZE, 0 };
    
    int is_eof = 0;
    size_t total_out = 0;
    size_t result = 1;
    
    while (result != 0) {
        /* Read more if we exhausted the input chunk and aren't EOF theoretically */
        if (in_b.pos >= in_b.size && !is_eof) {
            size_t bytes_read = fread(in_buf, 1, IN_CHUNK_SIZE, fin);
            in_b.size = bytes_read;
            in_b.pos = 0;
            
            if (bytes_read == 0) {
                is_eof = 1; /* Trigger EOF flush sequence */
            }
        }
        
        if (is_eof) {
            in_b.src = NULL;
            in_b.size = 0;
            in_b.pos = 0;
        }

        /* Perform a stream boundary pass */
        result = mcx_compress_stream(cctx, &out_b, &in_b, level);
        
        if (mcx_is_error(result)) {
            fprintf(stderr, "Error: Stream compression failed: %s\n", mcx_get_error_name(result));
            free(in_buf); free(out_buf); mcx_free_cctx(cctx);
            fclose(fin); fclose(fout);
            return 1;
        }
        
        /* Flush output if we have encoded parts waiting */
        if (out_b.pos > 0) {
            fwrite(out_buf, 1, out_b.pos, fout);
            total_out += out_b.pos;
            out_b.pos = 0;
        }
    }

    double ratio = (double)src_size / (double)total_out;
    double savings = (1.0 - (double)total_out / (double)src_size) * 100.0;

    printf("Done!\n");
    printf("  Input:  %zu bytes\n", src_size);
    printf("  Output: %zu bytes -> '%s'\n", total_out, output);
    printf("  Ratio:  %.2fx (%.1f%% smaller)\n", ratio, savings);

    free(in_buf);
    free(out_buf);
    mcx_free_cctx(cctx);
    fclose(fin);
    fclose(fout);
    return 0;
}

static int cmd_decompress(const char* input, const char* output)
{
    FILE* fin = fopen(input, "rb");
    if (!fin) {
        fprintf(stderr, "Error: cannot open '%s'\n", input);
        return 1;
    }
    
    char auto_output[1024];
    if (output == NULL) {
        make_decompress_output(auto_output, sizeof(auto_output), input);
        output = auto_output;
    }
    
    FILE* fout = fopen(output, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot create '%s'\n", output);
        fclose(fin);
        return 1;
    }

    size_t IN_CHUNK_SIZE = 256 * 1024;
    size_t OUT_CHUNK_SIZE = 1048576 * 2; /* 2MB streaming boundary */
    
    uint8_t* in_buf = (uint8_t*)malloc(IN_CHUNK_SIZE);
    uint8_t* out_buf = (uint8_t*)malloc(OUT_CHUNK_SIZE);
    
    mcx_dctx* dctx = mcx_create_dctx();
    if (!in_buf || !out_buf || !dctx) {
        fprintf(stderr, "Error: out of memory during CLI decompression init\n");
        if (in_buf) free(in_buf); if (out_buf) free(out_buf);
        if (dctx) mcx_free_dctx(dctx);
        fclose(fin); fclose(fout);
        return 1;
    }

    printf("Decompressing '%s'... (Streaming API)\n", input);

    mcx_in_buffer in_b = { in_buf, 0, 0 };
    mcx_out_buffer out_b = { out_buf, OUT_CHUNK_SIZE, 0 };
    
    size_t total_out = 0;
    size_t result = 1;
    
    while (result != 0) {
        if (in_b.pos >= in_b.size) {
            size_t bytes_read = fread(in_buf, 1, IN_CHUNK_SIZE, fin);
            in_b.size = bytes_read;
            in_b.pos = 0;
            
            if (bytes_read == 0 && result != 0 && dctx->state != MCX_DSTATE_FINISHED) {
                fprintf(stderr, "Error: unexpected EOF in compressed stream!\n");
                free(in_buf); free(out_buf); mcx_free_dctx(dctx);
                fclose(fin); fclose(fout);
                return 1;
            }
        }

        result = mcx_decompress_stream(dctx, &out_b, &in_b);
        
        if (mcx_is_error(result)) {
            fprintf(stderr, "Error: Stream decompression failed: %s\n", mcx_get_error_name(result));
            free(in_buf); free(out_buf); mcx_free_dctx(dctx);
            fclose(fin); fclose(fout);
            return 1;
        }
        
        if (out_b.pos > 0) {
            fwrite(out_buf, 1, out_b.pos, fout);
            total_out += out_b.pos;
            out_b.pos = 0;
        }
    }

    printf("Done!\n");
    printf("  Decompressed: %zu bytes -> '%s'\n", total_out, output);

    free(in_buf);
    free(out_buf);
    mcx_free_dctx(dctx);
    fclose(fin);
    fclose(fout);
    return 0;
}

static int cmd_info(const char* input)
{
    size_t src_size;
    uint8_t* src = read_file(input, &src_size);
    if (src == NULL) return 1;

    unsigned long long orig_size = mcx_get_decompressed_size(src, src_size);

    printf("File:             %s\n", input);
    printf("Compressed size:  %zu bytes\n", src_size);
    if (orig_size > 0) {
        printf("Original size:    %llu bytes\n", orig_size);
        printf("Ratio:            %.2fx\n", (double)orig_size / (double)src_size);
    } else {
        printf("Original size:    unknown\n");
    }

    free(src);
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

    /* Commands */
    if (strcmp(argv[1], "compress") == 0) {
        int level = MCX_LEVEL_DEFAULT;
        const char* input = NULL;
        const char* output = NULL;

        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0) && i + 1 < argc) {
                level = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
                output = argv[++i];
            } else if (argv[i][0] != '-') {
                input = argv[i];
            }
        }

        if (input == NULL) {
            fprintf(stderr, "Error: no input file specified\n");
            return 1;
        }
        return cmd_compress(input, output, level);

    } else if (strcmp(argv[1], "decompress") == 0) {
        const char* input = NULL;
        const char* output = NULL;

        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
                output = argv[++i];
            } else if (argv[i][0] != '-') {
                input = argv[i];
            }
        }

        if (input == NULL) {
            fprintf(stderr, "Error: no input file specified\n");
            return 1;
        }
        return cmd_decompress(input, output);

    } else if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: no input file specified\n");
            return 1;
        }
        return cmd_info(argv[2]);

    } else {
        fprintf(stderr, "Unknown command: '%s'\n\n", argv[1]);
        print_usage();
        return 1;
    }
}
