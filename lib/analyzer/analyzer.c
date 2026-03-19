/**
 * @file analyzer.c
 * @brief Data analysis — entropy measurement, type detection.
 */

#include "analyzer.h"
#include <math.h>

/* ─── Shannon Entropy ────────────────────────────────────────────────── */

double mcx_entropy(const uint8_t* data, size_t size)
{
    if (data == NULL || size == 0) return 0.0;

    size_t freq[256] = {0};
    for (size_t i = 0; i < size; i++) {
        freq[data[i]]++;
    }

    double entropy = 0.0;
    double inv_size = 1.0 / (double)size;

    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] * inv_size;
        entropy -= p * log2(p);
    }

    return entropy;
}

/* ─── Data Type Detection ────────────────────────────────────────────── */

mcx_data_type_t mcx_detect_type(const uint8_t* data, size_t size)
{
    if (data == NULL || size == 0) return MCX_DTYPE_UNKNOWN;

    /* Count ASCII printable, control, and high bytes */
    size_t printable = 0;
    size_t control   = 0;
    size_t high      = 0;
    size_t nulls     = 0;

    size_t check_size = MCX_MIN(size, 4096); /* Sample first 4 KB */

    for (size_t i = 0; i < check_size; i++) {
        uint8_t b = data[i];
        if (b == 0) {
            nulls++;
        } else if (b >= 32 && b <= 126) {
            printable++;
        } else if (b == '\n' || b == '\r' || b == '\t') {
            printable++; /* whitespace counts as printable */
        } else if (b < 32) {
            control++;
        } else {
            high++;
        }
    }

    double printable_ratio = (double)printable / (double)check_size;

    /* Check for executable signatures */
    if (size >= 2 && data[0] == 'M' && data[1] == 'Z') {
        return MCX_DTYPE_EXECUTABLE; /* PE (Windows) */
    }
    if (size >= 4 && data[0] == 0x7F && data[1] == 'E'
                  && data[2] == 'L' && data[3] == 'F') {
        return MCX_DTYPE_EXECUTABLE; /* ELF (Linux) */
    }

    /* High entropy = likely already compressed or encrypted */
    double ent = mcx_entropy(data, check_size);
    if (ent > 7.5) {
        return MCX_DTYPE_HIGH_ENTROPY;
    }

    /* Mostly printable = text */
    if (printable_ratio > 0.95) {
        /* Check for structured text */
        if (size >= 1 && (data[0] == '{' || data[0] == '[' || data[0] == '<')) {
            return MCX_DTYPE_STRUCTURED;
        }
        if (high > 0) {
            return MCX_DTYPE_TEXT_UTF8;
        }
        return MCX_DTYPE_TEXT_ASCII;
    }

    /* Check for numeric data (lots of repeated byte patterns) */
    if (nulls > check_size / 4) {
        return MCX_DTYPE_NUMERIC;
    }

    return MCX_DTYPE_BINARY;
}

/* ─── Full Analysis ──────────────────────────────────────────────────── */

mcx_analysis_t mcx_analyze(const uint8_t* data, size_t size)
{
    mcx_analysis_t result;
    memset(&result, 0, sizeof(result));

    result.type            = mcx_detect_type(data, size);
    result.entropy         = mcx_entropy(data, MCX_MIN(size, 4096));
    result.self_similarity = 0.0; /* TODO: implement fractal self-similarity detection */
    result.block_size      = MCX_DEFAULT_BLOCK_SIZE;

    /* Adjust block size based on data type */
    switch (result.type) {
        case MCX_DTYPE_TEXT_ASCII:
        case MCX_DTYPE_TEXT_UTF8:
            result.block_size = 256 * 1024; /* Larger blocks for text (BWT benefits) */
            break;
        case MCX_DTYPE_HIGH_ENTROPY:
            result.block_size = MCX_MAX_BLOCK_SIZE; /* Big blocks, store as-is */
            break;
        default:
            result.block_size = MCX_DEFAULT_BLOCK_SIZE;
            break;
    }

    return result;
}

/* ─── Adaptive Block Sizing ──────────────────────────────────────────── */

int mcx_adaptive_blocks(const uint8_t* data, size_t size,
                        size_t** out_sizes, uint32_t* out_count)
{
    if (!data || size == 0 || !out_sizes || !out_count) return -1;

    /* If data fits in one block, no need for adaptive sizing */
    if (size <= MCX_MIN_BLOCK_SIZE) {
        size_t* sizes = (size_t*)malloc(sizeof(size_t));
        if (!sizes) return -1;
        sizes[0] = size;
        *out_sizes = sizes;
        *out_count = 1;
        return 0;
    }

    /* Scan entropy in windows */
    size_t num_windows = (size + MCX_ENTROPY_WINDOW - 1) / MCX_ENTROPY_WINDOW;
    double* window_entropy = (double*)malloc(num_windows * sizeof(double));
    if (!window_entropy) return -1;

    for (size_t w = 0; w < num_windows; w++) {
        size_t woff = w * MCX_ENTROPY_WINDOW;
        size_t wlen = MCX_MIN(MCX_ENTROPY_WINDOW, size - woff);
        window_entropy[w] = mcx_entropy(data + woff, wlen);
    }

    /* Classify windows: high entropy (>7.0) vs low entropy */
    #define ENTROPY_THRESHOLD 7.0

    /* Group windows into blocks.
     * Strategy: merge consecutive windows of the same class.
     * High-entropy blocks: max 4MB (no benefit from large BWT)
     * Low-entropy blocks: max MCX_MAX_BLOCK_SIZE (BWT benefits from context) */
    #define HIGH_ENTROPY_MAX_BLOCK  (4 * 1024 * 1024)

    /* Worst case: each window is its own block */
    size_t* block_sizes = (size_t*)malloc(num_windows * sizeof(size_t));
    if (!block_sizes) { free(window_entropy); return -1; }

    uint32_t nblocks = 0;
    size_t current_block = 0;
    int current_class = (window_entropy[0] > ENTROPY_THRESHOLD) ? 1 : 0;
    size_t max_for_class = current_class ? HIGH_ENTROPY_MAX_BLOCK : MCX_MAX_BLOCK_SIZE;

    for (size_t w = 0; w < num_windows; w++) {
        int wclass = (window_entropy[w] > ENTROPY_THRESHOLD) ? 1 : 0;
        size_t woff = w * MCX_ENTROPY_WINDOW;
        size_t wlen = MCX_MIN(MCX_ENTROPY_WINDOW, size - woff);

        /* Start new block if class changes or current block would exceed max */
        if (wclass != current_class || current_block + wlen > max_for_class) {
            if (current_block > 0) {
                block_sizes[nblocks++] = current_block;
            }
            current_class = wclass;
            max_for_class = current_class ? HIGH_ENTROPY_MAX_BLOCK : MCX_MAX_BLOCK_SIZE;
            current_block = wlen;
        } else {
            current_block += wlen;
        }
    }
    /* Flush last block */
    if (current_block > 0) {
        block_sizes[nblocks++] = current_block;
    }

    /* Merge tiny blocks (< MCX_MIN_BLOCK_SIZE) with neighbors */
    for (uint32_t i = 0; i < nblocks; i++) {
        if (block_sizes[i] < MCX_MIN_BLOCK_SIZE && nblocks > 1) {
            if (i + 1 < nblocks) {
                /* Merge with next */
                block_sizes[i + 1] += block_sizes[i];
                /* Shift left */
                for (uint32_t j = i; j + 1 < nblocks; j++) {
                    block_sizes[j] = block_sizes[j + 1];
                }
                nblocks--;
                i--; /* Re-check this position */
            } else if (i > 0) {
                /* Merge with previous */
                block_sizes[i - 1] += block_sizes[i];
                nblocks--;
            }
        }
    }

    free(window_entropy);

    /* Shrink allocation */
    size_t* final = (size_t*)realloc(block_sizes, nblocks * sizeof(size_t));
    *out_sizes = final ? final : block_sizes;
    *out_count = nblocks;
    return 0;

    #undef ENTROPY_THRESHOLD
    #undef HIGH_ENTROPY_MAX_BLOCK
}
