/**
 * @file analyzer.h
 * @brief Data analysis module — Detects data type, entropy, and structure.
 *
 * This is Stage 0 of the MaxCompression pipeline.
 * It examines input data and produces an analysis report that guides
 * the selection of preprocessing and compression strategies.
 */

#ifndef MCX_ANALYZER_H
#define MCX_ANALYZER_H

#include "../internal.h"

/**
 * Analyze a block of data to determine its type, entropy,
 * and structural properties.
 *
 * @param data  Pointer to the data block
 * @param size  Size of the data block in bytes
 * @return      Analysis report
 */
mcx_analysis_t mcx_analyze(const uint8_t* data, size_t size);

/**
 * Compute Shannon entropy of a data block (in bits per byte).
 * Range: 0.0 (perfectly uniform) to 8.0 (maximum randomness).
 *
 * @param data  Pointer to the data block
 * @param size  Size of the data block in bytes
 * @return      Entropy in bits per byte
 */
double mcx_entropy(const uint8_t* data, size_t size);

/**
 * Detect the data type of a block (text, binary, structured, etc.)
 *
 * @param data  Pointer to the data block
 * @param size  Size of the data block in bytes
 * @return      Detected data type
 */
mcx_data_type_t mcx_detect_type(const uint8_t* data, size_t size);

#endif /* MCX_ANALYZER_H */
