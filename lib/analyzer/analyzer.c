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
