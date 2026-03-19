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

/**
 * Compute adaptive block boundaries based on entropy analysis.
 * Scans input in MCX_ENTROPY_WINDOW-sized windows, groups consecutive
 * windows with similar entropy into blocks. High-entropy regions get
 * smaller blocks (faster processing), low-entropy regions get larger
 * blocks (better BWT context).
 *
 * @param data       Pointer to the input data
 * @param size       Total input size
 * @param out_sizes  Output array of block sizes (caller must free)
 * @param out_count  Output: number of blocks
 * @return           0 on success, -1 on error
 */
int mcx_adaptive_blocks(const uint8_t* data, size_t size,
                        size_t** out_sizes, uint32_t* out_count);

#endif /* MCX_ANALYZER_H */
