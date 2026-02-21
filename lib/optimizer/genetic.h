/**
 * @file genetic.h
 * @brief Meta-optimizer using evolutionary algorithms for block pipeline routing.
 */

#ifndef MCX_OPTIMIZER_GENETIC_H
#define MCX_OPTIMIZER_GENETIC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Genetic Genome configuring the compression pipeline for a single block.
 */
typedef struct {
    uint8_t use_bwt;       /* 1 if BWT enabled, 0 otherwise */
    uint8_t use_mtf_rle;   /* 1 if MTF + RLE enabled, 0 otherwise */
    uint8_t use_delta;     /* 1 if Delta encoding enabled, 0 otherwise */
    uint8_t entropy_coder; /* 0 = Default (fast), 1 = rANS, 2 = CM-rANS */
    uint8_t cm_learning;   /* Context Mixing learning rate scaler (0-7) */
} mcx_genome;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a 1-byte header into a genome structure.
 */
mcx_genome mcx_decode_genome(uint8_t header);

/**
 * @brief Encode a genome structure into a 1-byte header.
 */
uint8_t mcx_encode_genome(const mcx_genome* g);

/**
 * @brief Evolve and find the best genome for a given data block.
 * Uses a heuristic search to evaluate multiple genomes and pick the one with the best (lowest) compressed size.
 *
 * @param block_data Pointer to the source block data.
 * @param block_size Size of the source block.
 * @param level Base compression level requested by the user.
 * @return The optimal mcx_genome found.
 */
mcx_genome mcx_evolve(const uint8_t* block_data, size_t block_size, int level);

#ifdef __cplusplus
}
#endif

#endif /* MCX_OPTIMIZER_GENETIC_H */
