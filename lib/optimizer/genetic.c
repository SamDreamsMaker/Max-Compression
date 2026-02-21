/**
 * @file genetic.c
 * @brief Implementation of the genetic meta-optimizer.
 */

#include "genetic.h"
#include <internal.h>
#include <string.h>
#include <stdlib.h>

/* ─── Genome Serialization ────────────────────────────────────────────────── */

mcx_genome mcx_decode_genome(uint8_t header)
{
    mcx_genome g;
    g.use_bwt       = (header & 0x01) ? 1 : 0;
    g.use_mtf_rle   = (header & 0x02) ? 1 : 0;
    g.use_delta     = (header & 0x04) ? 1 : 0;
    g.entropy_coder = (header >> 3) & 0x03;
    g.cm_learning   = (header >> 5) & 0x07;
    return g;
}

uint8_t mcx_encode_genome(const mcx_genome* g)
{
    uint8_t header = 0;
    if (g->use_bwt)     header |= 0x01;
    if (g->use_mtf_rle) header |= 0x02;
    if (g->use_delta)   header |= 0x04;
    header |= ((g->entropy_coder & 0x03) << 3);
    header |= ((g->cm_learning & 0x07) << 5);
    return header;
}

/* ─── Fitness Function ───────────────────────────────────────────────────── */

/* Forward declarations representing the pipeline blocks we might evaluate.
 * These require a simulation compressor or proxy since the main compressor is not re-entrant for genomes yet. 
 * Actually, to evaluate fitness accurately, we can run a slimmed-down entropy pass.
 * For now, we will create a heuristic function. Real compression simulation is too slow for 4MB.
 * We sample 4KB from the block to simulate.
 */

static size_t evaluate_fitness(const mcx_genome* g, const uint8_t* sample, size_t sample_size)
{
    /* To strictly simulate, we'll estimate entropy sizes.
     * BWT reduces entropy locally. CM-rANS compresses best when patterns exist.
     * This is a placeholder for actual pipeline simulation.
     */
    
    /* Allocations for simulation */
    uint8_t* bwt_buf = NULL;
    uint8_t* in_buf = (uint8_t*)sample;
    size_t in_size = sample_size;

    size_t penalty = 0;

    /* Simulate Delta */
    if (g->use_delta) {
        /* Not actively simulated, but we could check correlation */
        int correlation = 0;
        for (size_t i = 1; i < in_size; i++) {
            if (abs((int)in_buf[i] - (int)in_buf[i-1]) < 16) correlation++;
        }
        if (correlation > (int)(in_size / 2)) {
            penalty -= (in_size / 4); /* Reward delta for highly correlated streams */
        } else {
            penalty += (in_size / 2); /* Penalize delta if unused */
        }
    }

    /* Simulate BWT */
    if (g->use_bwt) {
        /* Running actual BWT on 4KB sample is fast enough! */
        bwt_buf = (uint8_t*)malloc(sample_size);
        if (bwt_buf) {
            size_t pidx;
            mcx_bwt_forward(bwt_buf, &pidx, in_buf, sample_size);
            in_buf = bwt_buf;
        }
        /* BWT always helps if there are repeats, but adds overhead */
    }

    /* Estimate final size via simple zero-order entropy as a proxy */
    size_t counts[256] = {0};
    for (size_t i = 0; i < in_size; i++) {
        counts[in_buf[i]]++;
    }
    
    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / (double)in_size;
            /* Approximate log2(1/p) */
            entropy += p * (1.0 / p); /* Simple proxy for fitness, less distinct values = better */
        }
    }

    size_t estimated_size = (size_t)((double)in_size * (entropy / 256.0)) + penalty;

    /* Adjust for coder strength */
    if (g->entropy_coder == 2) estimated_size = (estimated_size * 8) / 10; /* CM-rANS is ~20% better */
    if (g->entropy_coder == 1) estimated_size = (estimated_size * 9) / 10; /* rANS is ~10% better */

    if (bwt_buf) free(bwt_buf);

    return estimated_size;
}

/* ─── Evolutionary Optimizer ─────────────────────────────────────────────── */

#define SAMPLE_SIZE 4096

mcx_genome mcx_evolve(const uint8_t* block_data, size_t block_size, int level)
{
    mcx_genome best_genome = {0, 0, 0, 0, 0};
    size_t best_size = (size_t)-1;

    size_t sample_len = block_size > SAMPLE_SIZE ? SAMPLE_SIZE : block_size;

    /* Base constraints derived from user level */
    int allow_cm = (level >= 15);
    int allow_bwt = (level >= 4);

    /* Construct candidate configurations (Populations) */
    mcx_genome candidates[8];
    int candidate_count = 0;

    /* 0: Fast fallback */
    candidates[candidate_count++] = (mcx_genome){0, 0, 0, level >= 10 ? 1 : 0, 0};
    
    /* 1: BWT standard */
    if (allow_bwt) {
        candidates[candidate_count++] = (mcx_genome){1, 1, 0, level >= 10 ? 1 : 0, 0};
        candidates[candidate_count++] = (mcx_genome){1, 1, 1, level >= 10 ? 1 : 0, 0}; /* With Delta */
    }

    /* 2: Advanced CM */
    if (allow_cm) {
        candidates[candidate_count++] = (mcx_genome){1, 1, 0, 2, 4}; /* Default learning */
        candidates[candidate_count++] = (mcx_genome){1, 1, 0, 2, 7}; /* High learning */
        candidates[candidate_count++] = (mcx_genome){0, 0, 1, 2, 4}; /* Delta + CM (no BWT) */
    }

    /* Evaluate fitness across populations */
    for (int i = 0; i < candidate_count; i++) {
        size_t est_size = evaluate_fitness(&candidates[i], block_data, sample_len);
        
        if (est_size < best_size) {
            best_size = est_size;
            best_genome = candidates[i];
        }
    }

    return best_genome;
}
