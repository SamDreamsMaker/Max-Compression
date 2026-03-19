/**
 * @file core.c
 * @brief Core orchestrator — the compression/decompression pipeline.
 *
 * This is the heart of MaxCompression. It:
 * 1. Analyzes the input data (Stage 0)
 * 2. Selects the best preprocessing strategy (Stage 1)
 * 3. Applies predictive modeling (Stage 2) — TODO
 * 4. Encodes with entropy coding (Stage 3)
 * 5. Writes the .mcx frame format
 *
 * Pipeline: BWT + MTF + RLE + rANS (default) or CM-rANS (best).
 */

#include "internal.h"
#include "analyzer/analyzer.h"
#include "model/model.h"
#include "entropy/entropy.h"
#include "entropy/mcx_fse.h"
#include "optimizer/genetic.h"
#include "lz/mcx_lz.h"
#include "babel/babel_transform.h"
#include "babel/babel_stride.h"
#include "entropy/adaptive_ac.h"
#include "preprocess/preprocess.h"
#include "lz/lzrc.h"
#include <time.h>

/* Decompress profiling: set MCX_PROFILE=1 to enable stage timing */
static int mcx_profile_enabled = -1; /* -1 = not checked yet */
static inline int mcx_profile(void) {
    if (mcx_profile_enabled < 0) {
        const char* env = getenv("MCX_PROFILE");
        mcx_profile_enabled = (env && env[0] == '1') ? 1 : 0;
    }
    return mcx_profile_enabled;
}
static inline double mcx_time_ms(struct timespec* start, struct timespec* end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 + (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

/* RLE2 (RUNA/RUNB) */
extern size_t mcx_rle2_encode(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_size);
extern size_t mcx_rle2_decode(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_size);
extern size_t mcx_multi_rans_compress(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_size);
extern size_t mcx_multi_rans_decompress(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_size);

/* E8/E9 x86 filter */
extern size_t mcx_e8e9_encode(uint8_t* data, size_t size);
extern size_t mcx_e8e9_decode(uint8_t* data, size_t size);

#include <stdio.h>
#ifdef _OPENMP
#ifdef _OPENMP
#include <omp.h>
#endif
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  Version & Error API
 * ═══════════════════════════════════════════════════════════════════════ */

unsigned mcx_version_number(void)
{
    return MCX_VERSION_MAJOR * 10000 + MCX_VERSION_MINOR * 100 + MCX_VERSION_PATCH;
}

const char* mcx_version_string(void)
{
    /* Use preprocessor to build version string from header constants */
    #define MCX_STR(x) MCX_STR2(x)
    #define MCX_STR2(x) #x
    return MCX_STR(MCX_VERSION_MAJOR) "." MCX_STR(MCX_VERSION_MINOR) "." MCX_STR(MCX_VERSION_PATCH);
    #undef MCX_STR2
    #undef MCX_STR
}

int mcx_is_error(size_t result)
{
    return MCX_IS_ERROR(result);
}

static const char* error_names[] = {
    "No error",
    "Generic error",
    "Destination buffer too small",
    "Source data corrupted",
    "Invalid magic number",
    "Invalid compression level",
    "Memory allocation failed",
    "Unknown block type",
};

const char* mcx_get_error_name(size_t result)
{
    size_t code;
    if (!MCX_IS_ERROR(result)) return error_names[0];
    code = MCX_ERROR_CODE(result);
    if (code >= MCX_ERR_MAX) return "Unknown error";
    return error_names[code];
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Frame header read/write
 * ═══════════════════════════════════════════════════════════════════════ */

static void write_frame_header(uint8_t* dst, const mcx_frame_header_t* hdr)
{
    memcpy(dst + 0,  &hdr->magic,           4);
    dst[4]  = hdr->version;
    dst[5]  = hdr->flags;
    dst[6]  = hdr->level;
    dst[7]  = hdr->strategy;
    memcpy(dst + 8,  &hdr->original_size,   8);
    memcpy(dst + 16, &hdr->header_checksum,  4);
}

static int read_frame_header(const uint8_t* src, size_t src_size,
                             mcx_frame_header_t* hdr)
{
    if (src_size < MCX_FRAME_HEADER_SIZE) return -1;

    memcpy(&hdr->magic,           src + 0,  4);
    hdr->version  = src[4];
    hdr->flags    = src[5];
    hdr->level    = src[6];
    hdr->strategy = src[7];
    memcpy(&hdr->original_size,   src + 8,  8);
    memcpy(&hdr->header_checksum, src + 16, 4);

    if (hdr->magic != MCX_MAGIC) return -1;
    if (hdr->version != MCX_FRAME_VERSION) return -1;

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Compress Bound
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_compress_bound(size_t src_size)
{
    /* Worst case: frame header + CM-rANS context tables (256×256×2) +
     * data expanded by preprocessing + entropy coder overhead */
    return MCX_FRAME_HEADER_SIZE
         + src_size + (src_size / 8)
         + 256 * 256 * 2    /* CM-rANS context tables */
         + 4096 + 256 * 4;  /* rANS state + Huffman header */
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Compress
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_compress(void* dst, size_t dst_cap,
                    const void* src, size_t src_size,
                    int level)
{
    uint8_t* out;
    const uint8_t* in;
    mcx_analysis_t analysis;
    mcx_strategy_t strategy;
    mcx_frame_header_t header;
    size_t offset;

    if (dst == NULL || src == NULL || src_size == 0) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }
    if (level < MCX_LEVEL_MIN || level > MCX_LEVEL_MAX) {
        return MCX_ERROR(MCX_ERR_INVALID_LEVEL);
    }
    if (dst_cap < MCX_FRAME_HEADER_SIZE + 8) {
        fprintf(stderr, "MCX_COMPRESS UPFRONT FAIL: dst_cap %zu < %zu\n", dst_cap, (size_t)(MCX_FRAME_HEADER_SIZE + 8));
        return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    }

    out = (uint8_t*)dst;
    in = (const uint8_t*)src;

    /* ── Stage 0: Analyze ── */
    analysis = mcx_analyze(in, src_size);

    /* ── Choose strategy based on level (and analysis for BWT levels) ── */
    if (level == 26) {
        /* Level 26: Force LZRC strategy — BT match finder (best ratio, slowest) */
        strategy = MCX_STRATEGY_LZRC;
    } else if (level == 24) {
        /* Level 24: Force LZRC fast — HC match finder (~3x faster, ~2-5% larger) */
        strategy = MCX_STRATEGY_LZRC;
    } else if (level == 25) {
        /* Level 25: Force LZ24 strategy (16MB window) — used by multi-trial */
        strategy = MCX_STRATEGY_LZ24;
    } else if (level <= 3) {
        strategy = MCX_STRATEGY_LZ_FAST; /* v1.0: Fast LZ77 — always try, fall back in block loop */
    } else if (level <= 9) {
        strategy = MCX_STRATEGY_LZ_HC;   /* v1.0: Lazy LZ77 — always try, fall back in block loop */
    } else if (analysis.type == MCX_DTYPE_HIGH_ENTROPY) {
        /* BWT is useless on already-compressed/encrypted data: skip directly to STORE.
         * For LZ levels we still try (LZ77 can exploit long-range patterns even in
         * max-entropy byte histograms, e.g. a cyclic counter i&0xFF). */
        strategy = MCX_STRATEGY_STORE;
    } else if (level <= 14) {
        strategy = MCX_STRATEGY_DEFAULT; /* v0.2: BWT + MTF + RLE + ANS */
    } else if (level <= 19) {
        strategy = MCX_STRATEGY_BEST;    /* v0.2: BWT + MTF + RLE + CM-rANS */
    } else {
        /* v1.3 Smart Mode (L20+): auto-detect best strategy.
         * Try stride-delta first (wins big on structured binary like .xls).
         * Fall back to BWT+RLE2 for text, LZ24 for generic binary. */
        int stride = mcx_babel_stride_detect(in, MCX_MIN(src_size, 65536));
        if (stride > 0) {
            strategy = MCX_STRATEGY_STRIDE;
        } else if (analysis.type == MCX_DTYPE_TEXT_ASCII ||
                   analysis.type == MCX_DTYPE_TEXT_UTF8 ||
                   analysis.type == MCX_DTYPE_STRUCTURED) {
            /* Text strategy by size:
             * < 1KB: LZ-HC (BWT overhead too high for tiny files)
             * >= 1KB: BWT + RLE2 + rANS (suffix sort, per-block with multi-table)
             * Note: even files > 4MB use BWT — they're split into 1MB blocks
             * and BWT + multi-table rANS beats LZ24 on text. */
            if (src_size < 1024) {
                strategy = MCX_STRATEGY_LZ_HC; /* BWT overhead too high for < 1KB */
            } else {
                strategy = MCX_STRATEGY_DEFAULT;
            }
        } else if (analysis.type == MCX_DTYPE_HIGH_ENTROPY) {
            strategy = MCX_STRATEGY_STORE;
        } else {
            /* Generic binary / unknown:
             * L20+: Use LZRC for binary data (3.22x vs 2.93x on mozilla).
             * Lower levels: BWT (LZRC only available at L26+).
             * Multi-trial at L20 will also try BWT and keep best.
             * For files > 16MB: use HC match finder (faster, only ~2% worse). */
            if (level >= 20 && src_size >= 1024) {
                strategy = MCX_STRATEGY_LZRC;
            } else {
                strategy = MCX_STRATEGY_DEFAULT;
            }
        }
    }

    /* ── Write frame header ── */
    memset(&header, 0, sizeof(header));
    header.magic         = MCX_MAGIC;
    header.version       = MCX_FRAME_VERSION;
    header.flags         = MCX_FLAG_HAS_ORIG_SIZE;
    header.level         = (uint8_t)level;
    header.strategy      = (uint8_t)strategy;
    header.original_size = (uint64_t)src_size;
    header.header_checksum = 0;

    write_frame_header(out, &header);
    offset = MCX_FRAME_HEADER_SIZE;

    /* ── Define Block layout ── */
    uint32_t num_blocks = 1;
    size_t* orig_block_sizes = NULL;  /* Per-block original sizes (adaptive) */
    int adaptive_blocks = 0;

    if (src_size > 0 && strategy != MCX_STRATEGY_STORE && strategy != MCX_STRATEGY_FAST) {
        /* Try adaptive block sizing for BWT strategies on multi-block data */
        if (src_size > MCX_MAX_BLOCK_SIZE &&
            (strategy == MCX_STRATEGY_DEFAULT || strategy == MCX_STRATEGY_BEST ||
             strategy == MCX_STRATEGY_STRIDE)) {
            uint32_t adaptive_count = 0;
            size_t* adaptive_sizes = NULL;
            if (mcx_adaptive_blocks(in, src_size, &adaptive_sizes, &adaptive_count) == 0 &&
                adaptive_count > 0) {
                /* Check if adaptive gave different sizing than uniform */
                uint32_t uniform_count = (uint32_t)((src_size + MCX_MAX_BLOCK_SIZE - 1) / MCX_MAX_BLOCK_SIZE);
                if (adaptive_count != uniform_count) {
                    num_blocks = adaptive_count;
                    orig_block_sizes = adaptive_sizes;
                    adaptive_blocks = 1;
                    header.flags |= MCX_FLAG_ADAPTIVE_BLOCKS;
                    /* Re-write header with updated flags */
                    write_frame_header(out, &header);
                } else {
                    free(adaptive_sizes);
                    num_blocks = uniform_count;
                }
            } else {
                if (adaptive_sizes) free(adaptive_sizes);
                num_blocks = (uint32_t)((src_size + MCX_MAX_BLOCK_SIZE - 1) / MCX_MAX_BLOCK_SIZE);
            }
        } else {
            num_blocks = (uint32_t)((src_size + MCX_MAX_BLOCK_SIZE - 1) / MCX_MAX_BLOCK_SIZE);
        }
    }
    size_t block_sizes_offset = 0;

    if (strategy != MCX_STRATEGY_STORE && strategy != MCX_STRATEGY_FAST) {
        size_t header_need = 4 + num_blocks * 4;
        if (adaptive_blocks) header_need += num_blocks * 4; /* original sizes too */
        if (offset + header_need > dst_cap) {
            if (orig_block_sizes) free(orig_block_sizes);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }
        
        /* Write number of blocks */
        memcpy(out + offset, &num_blocks, 4);
        offset += 4;
        
        /* Reserve space for the array of N compressed block sizes */
        block_sizes_offset = offset;
        offset += num_blocks * 4;

        /* For adaptive blocks: write original block sizes */
        if (adaptive_blocks) {
            for (uint32_t b = 0; b < num_blocks; b++) {
                uint32_t obs32 = (uint32_t)orig_block_sizes[b];
                memcpy(out + offset, &obs32, 4);
                offset += 4;
            }
        }
    }

    /* ── Stage 1 & 3: Compress based on strategy ── */
    if (strategy == MCX_STRATEGY_STORE) {
        /* Just copy data (incompressible) */
        if (offset + src_size > dst_cap) {
            fprintf(stderr, "STORE DST_TOO_SMALL: offset %zu + src_size %zu > dst_cap %zu\n", offset, src_size, dst_cap);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }
        memcpy(out + offset, in, src_size);
        offset += src_size;

    } else if (strategy == MCX_STRATEGY_FAST) {
        /* Fast path: Huffman only */
        size_t compressed = mcx_huffman_compress(
            out + offset, dst_cap - offset, in, src_size, NULL);
        if (MCX_IS_ERROR(compressed)) return compressed;
        offset += compressed;

    } else {
        /* Unified block processing: LZ77 or BWT+Entropy */
        uint8_t** block_buffers = (uint8_t**)malloc(num_blocks * sizeof(uint8_t*));
        size_t*   block_sizes   = (size_t*)malloc(num_blocks * sizeof(size_t));
        int       omp_err       = 0;
        
        if (!block_buffers || !block_sizes) {
            if (block_buffers) free(block_buffers);
            if (block_sizes) free(block_sizes);
            return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
        }

        memset(block_buffers, 0, num_blocks * sizeof(uint8_t*));
        
        int32_t b;
#ifdef _OPENMP
        if (!omp_in_parallel()) {
            omp_set_nested(0); /* Prevent inner CM-rANS OMP from corrupting heap */
        }
#endif
        /* Pre-compute cumulative source offsets for adaptive blocks */
        size_t* block_src_offsets = NULL;
        size_t* block_src_sizes_arr = NULL;
        if (adaptive_blocks && orig_block_sizes) {
            block_src_offsets = (size_t*)malloc(num_blocks * sizeof(size_t));
            block_src_sizes_arr = (size_t*)malloc(num_blocks * sizeof(size_t));
            if (block_src_offsets && block_src_sizes_arr) {
                size_t cum = 0;
                for (uint32_t i = 0; i < num_blocks; i++) {
                    block_src_offsets[i] = cum;
                    block_src_sizes_arr[i] = orig_block_sizes[i];
                    cum += orig_block_sizes[i];
                }
            } else {
                free(block_src_offsets); free(block_src_sizes_arr);
                free(block_buffers); free(block_sizes);
                if (orig_block_sizes) free(orig_block_sizes);
                return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
            }
        }

        #pragma omp parallel for schedule(dynamic)
        for (b = 0; b < (int32_t)num_blocks; b++) {
            if (omp_err) continue;
            
            size_t src_offset, block_src_size;
            if (adaptive_blocks && block_src_offsets) {
                src_offset = block_src_offsets[b];
                block_src_size = block_src_sizes_arr[b];
            } else {
                src_offset = (size_t)b * MCX_MAX_BLOCK_SIZE;
                block_src_size = src_size - src_offset;
                if (block_src_size > MCX_MAX_BLOCK_SIZE) {
                    block_src_size = MCX_MAX_BLOCK_SIZE;
                }
            }

            if (strategy == MCX_STRATEGY_LZ_FAST || strategy == MCX_STRATEGY_LZ_HC) {
                size_t lz_cap = mcx_lz_compress_bound(block_src_size);
                uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                size_t fse_cap = mcx_compress_bound(lz_cap);
                uint8_t* fse_buf = (uint8_t*)malloc(fse_cap + 1); /* +1 for block type flag */
                
                if (!lz_buf || !fse_buf) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(lz_buf); free(fse_buf);
                    continue;
                }

                /* LZ77 Pass */
                size_t lz_size;
                if (strategy == MCX_STRATEGY_LZ_HC) {
                    lz_size = mcx_lz_compress_hc(lz_buf, lz_cap, in + src_offset, block_src_size, level);
                } else {
                    lz_size = mcx_lz_compress(lz_buf, lz_cap, in + src_offset, block_src_size, 1);
                }

                if (lz_size == 0) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(lz_buf); free(fse_buf);
                    continue;
                }

                /* Entropy Pass: try FSE on LZ output, then fall back gracefully */
                size_t fse_size = mcx_fse_compress(fse_buf + 1, fse_cap, lz_buf, lz_size);

                /* Also try rANS — 0.5-1% better than FSE on LZ output */
                size_t rans_cap = lz_size + lz_size / 4 + 1024;
                uint8_t* rans_buf = (uint8_t*)malloc(rans_cap + 1);
                size_t rans_size = 0;
                if (rans_buf) {
                    rans_size = mcx_rans_compress(rans_buf + 1, rans_cap, lz_buf, lz_size);
                    if (rans_size > 0) rans_buf[0] = 0xA8; /* LZ16+rANS */
                }

                /* Also try adaptive AC on LZ output — 7% better than FSE on binary */
                size_t aac_cap = lz_size + lz_size / 4 + 1024;
                uint8_t* aac_buf = (uint8_t*)malloc(aac_cap + 1);
                size_t aac_size = 0;
                if (aac_buf && level >= 7) { /* AAC for L7+: ~5% better ratio, slower decompress */
                    aac_size = mcx_adaptive_ac_compress(aac_buf + 1, aac_cap, lz_buf, lz_size);
                    if (aac_size > 0) aac_buf[0] = 0xAE; /* LZ16+AAC */
                }

                /* Pick best: FSE vs rANS vs AAC vs raw LZ vs store */
                size_t best_size = block_src_size; /* Store threshold */
                uint8_t best_type = 0x00;

                if (fse_size > 0 && fse_size < best_size) {
                    best_size = fse_size;
                    best_type = 0xAA; /* LZ+FSE */
                }
                if (rans_size > 0 && rans_size < best_size) {
                    best_size = rans_size;
                    best_type = 0xA8; /* LZ+rANS */
                }
                if (aac_size > 0 && aac_size < best_size) {
                    best_size = aac_size;
                    best_type = 0xAE; /* LZ+AAC */
                }
                if (lz_size < best_size) {
                    best_size = lz_size;
                    best_type = 0xAB; /* LZ raw */
                }

                if (best_type == 0xAA) {
                    fse_buf[0] = 0xAA;
                    block_sizes[b] = fse_size + 1;
                    block_buffers[b] = fse_buf;
                    free(lz_buf);
                    if (aac_buf) free(aac_buf);
                    if (rans_buf) free(rans_buf);
                } else if (best_type == 0xA8) {
                    block_sizes[b] = rans_size + 1;
                    block_buffers[b] = rans_buf;
                    free(lz_buf);
                    free(fse_buf);
                    if (aac_buf) free(aac_buf);
                    rans_buf = NULL;
                } else if (best_type == 0xAE) {
                    block_sizes[b] = aac_size + 1;
                    block_buffers[b] = aac_buf;
                    free(lz_buf);
                    free(fse_buf);
                    if (rans_buf) free(rans_buf);
                    aac_buf = NULL;
                } else if (best_type == 0xAB) {
                    fse_buf[0] = 0xAB;
                    memcpy(fse_buf + 1, lz_buf, lz_size);
                    block_sizes[b] = lz_size + 1;
                    block_buffers[b] = fse_buf;
                    free(lz_buf);
                    if (aac_buf) free(aac_buf);
                    if (rans_buf) free(rans_buf);
                } else {
                    /* Store original */
                    fse_buf[0] = 0x00;
                    memcpy(fse_buf + 1, in + src_offset, block_src_size);
                    block_sizes[b] = block_src_size + 1;
                    block_buffers[b] = fse_buf;
                    free(lz_buf);
                    if (aac_buf) free(aac_buf);
                    if (rans_buf) free(rans_buf);
                }

            } else if (strategy == MCX_STRATEGY_LZ24) {
                /* LZ24 Path: 24-bit offsets (16MB window) + FSE */
                size_t lz_cap = mcx_lz24_compress_bound(block_src_size);
                uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                size_t fse_cap = mcx_compress_bound(lz_cap);
                uint8_t* fse_buf = (uint8_t*)malloc(fse_cap + 1);
                
                if (!lz_buf || !fse_buf) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(lz_buf); free(fse_buf);
                    continue;
                }

                size_t lz_size = mcx_lz24_compress(lz_buf, lz_cap,
                                                    in + src_offset, block_src_size);
                if (lz_size == 0) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(lz_buf); free(fse_buf);
                    continue;
                }

                size_t fse_size = mcx_fse_compress(fse_buf + 1, fse_cap, lz_buf, lz_size);

                /* Also try rANS on LZ24 output */
                size_t rans24_cap = lz_size + lz_size / 4 + 1024;
                uint8_t* rans24_buf = (uint8_t*)malloc(rans24_cap + 1);
                size_t rans24_size = 0;
                if (rans24_buf) {
                    rans24_size = mcx_rans_compress(rans24_buf + 1, rans24_cap, lz_buf, lz_size);
                    if (rans24_size > 0) rans24_buf[0] = 0xA9; /* LZ24+rANS */
                }

                /* Also try adaptive AC on LZ24 output */
                size_t aac_cap = lz_size + lz_size / 4 + 1024;
                uint8_t* aac_buf = (uint8_t*)malloc(aac_cap + 1);
                size_t aac_size = 0;
                if (aac_buf) {
                    aac_size = mcx_adaptive_ac_compress(aac_buf + 1, aac_cap, lz_buf, lz_size);
                    if (aac_size > 0) aac_buf[0] = 0xAF; /* LZ24+AAC */
                }

                /* Pick best: FSE vs rANS vs AAC vs raw LZ24 vs store */
                size_t best_size = block_src_size;
                uint8_t best_type = 0x00;

                if (fse_size > 0 && fse_size < best_size) {
                    best_size = fse_size; best_type = 0xAC;
                }
                if (rans24_size > 0 && rans24_size < best_size) {
                    best_size = rans24_size; best_type = 0xA9;
                }
                if (aac_size > 0 && aac_size < best_size) {
                    best_size = aac_size; best_type = 0xAF;
                }
                if (lz_size < best_size) {
                    best_size = lz_size; best_type = 0xAD;
                }

                if (best_type == 0xAC) {
                    fse_buf[0] = 0xAC;
                    block_sizes[b] = fse_size + 1;
                    block_buffers[b] = fse_buf;
                    free(lz_buf);
                    if (aac_buf) free(aac_buf);
                    if (rans24_buf) free(rans24_buf);
                } else if (best_type == 0xA9) {
                    block_sizes[b] = rans24_size + 1;
                    block_buffers[b] = rans24_buf;
                    free(lz_buf); free(fse_buf);
                    if (aac_buf) free(aac_buf);
                    rans24_buf = NULL;
                } else if (best_type == 0xAF) {
                    block_sizes[b] = aac_size + 1;
                    block_buffers[b] = aac_buf;
                    free(lz_buf); free(fse_buf);
                    if (rans24_buf) free(rans24_buf);
                    aac_buf = NULL;
                } else if (best_type == 0xAD) {
                    fse_buf[0] = 0xAD;
                    memcpy(fse_buf + 1, lz_buf, lz_size);
                    block_sizes[b] = lz_size + 1;
                    block_buffers[b] = fse_buf;
                    free(lz_buf);
                    if (aac_buf) free(aac_buf);
                    if (rans24_buf) free(rans24_buf);
                } else {
                    fse_buf[0] = 0x00;
                    memcpy(fse_buf + 1, in + src_offset, block_src_size);
                    block_sizes[b] = block_src_size + 1;
                    block_buffers[b] = fse_buf;
                    free(lz_buf);
                    if (aac_buf) free(aac_buf);
                    if (rans24_buf) free(rans24_buf);
                }
            } else if (strategy == MCX_STRATEGY_LZRC) {
                /* LZRC Path: v2.0 LZ + Range Coder */
                size_t lzrc_cap = block_src_size * 2 + 4096;
                uint8_t* lzrc_buf = (uint8_t*)malloc(lzrc_cap + 1);
                if (!lzrc_buf) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
                
                /* Scale window to file size */
                int wlog = 20; /* 1MB minimum */
                int wlog_max = (level >= 26) ? 26 : 24; /* L26: up to 64MB, L24: 16MB */
                while (wlog < wlog_max && (1u << wlog) < block_src_size)
                    wlog++;
                
                size_t lzrc_size;
                if (level == 24) {
                    /* Level 24: HC match finder (~3x faster, ~2-5% larger) */
                    lzrc_size = mcx_lzrc_compress_fast(lzrc_buf + 1, lzrc_cap,
                                                        in + src_offset, block_src_size,
                                                        wlog, 8);
                } else {
                    /* Level 26 or L20 small files: BT match finder (best ratio) */
                    lzrc_size = mcx_lzrc_compress(lzrc_buf + 1, lzrc_cap,
                                                    in + src_offset, block_src_size,
                                                    wlog, 32);
                }
                
                if (lzrc_size > 0 && lzrc_size < block_src_size) {
                    lzrc_buf[0] = 0xB0; /* LZRC block type */
                    block_sizes[b] = lzrc_size + 1;
                    block_buffers[b] = lzrc_buf;
                } else {
                    /* LZRC didn't help — store raw */
                    lzrc_buf[0] = 0x00;
                    memcpy(lzrc_buf + 1, in + src_offset, block_src_size);
                    block_sizes[b] = block_src_size + 1;
                    block_buffers[b] = lzrc_buf;
                }
            } else {
                /* BWT Path (DEFAULT, BEST, BABEL, or STRIDE) */
                
                /* ── Preprocessing (strategy BABEL or STRIDE) ── */
                uint8_t* babel_buf = NULL;
                const uint8_t* block_in = in + src_offset;
                size_t block_in_size = block_src_size;
                uint32_t babel_size32 = 0;
                
                if (strategy == MCX_STRATEGY_STRIDE) {
                    size_t stride_cap = mcx_babel_stride_bound(block_src_size);
                    babel_buf = (uint8_t*)malloc(stride_cap);
                    if (!babel_buf) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }
                    size_t stride_out = mcx_babel_stride_forward(babel_buf, stride_cap,
                                                                  block_in, block_src_size);
                    if (stride_out == 0) {
                        /* No stride detected — fall back to raw data */
                        free(babel_buf);
                        babel_buf = NULL;
                    } else {
                        block_in = babel_buf;
                        block_in_size = stride_out;
                        babel_size32 = (uint32_t)stride_out;
                    }
                } else if (strategy == MCX_STRATEGY_BABEL) {
                    size_t babel_cap = mcx_babel_bound(block_src_size);
                    babel_buf = (uint8_t*)malloc(babel_cap);
                    if (!babel_buf) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }
                    size_t babel_out = mcx_babel_forward(babel_buf, babel_cap,
                                                         block_in, block_src_size);
                    if (babel_out == 0) {
                        /* Babel failed — fall back to raw data */
                        free(babel_buf);
                        babel_buf = NULL;
                    } else {
                        block_in = babel_buf;
                        block_in_size = babel_out;
                        babel_size32 = (uint32_t)babel_out;
                    }
                }
                
                uint8_t* buf0 = (uint8_t*)malloc(block_in_size);
                uint8_t* buf1 = (uint8_t*)malloc(block_in_size);
                uint8_t* buf2 = (uint8_t*)malloc(block_in_size + (block_in_size / 4) + 1024);
                size_t max_out = mcx_compress_bound(block_in_size) + 8; /* +8 for babel header */
                uint8_t* out1 = (uint8_t*)malloc(max_out);

                if (!buf0 || !buf1 || !buf2 || !out1) {
                    #pragma omp critical
                    { omp_err = 1; }
                    if (buf0) free(buf0);
                    if (buf1) free(buf1);
                    if (buf2) free(buf2);
                    if (out1) free(out1);
                    if (babel_buf) free(babel_buf);
                    continue;
                }

                /* For Babel strategy: Babel handles prediction, so use a simple
                 * order-0 entropy coder (rANS). No BWT/MTF/Delta — those would
                 * fight the Babel residual structure.
                 * For Stride strategy: use BWT+CM-rANS (best pipeline) since
                 * stride-delta output is ideal for BWT (near-zero runs). */
                mcx_genome genome;
                if (strategy == MCX_STRATEGY_BABEL) {
                    genome.use_bwt = 0;
                    genome.use_mtf_rle = 0;
                    genome.use_delta = 0;
                    genome.entropy_coder = 1; /* rANS (order-0) */
                    genome.cm_learning = 0;
                } else if (strategy == MCX_STRATEGY_STRIDE) {
                    if (babel_buf != NULL) {
                        /* Stride delta output: choose pipeline based on zero density.
                         * High diversity (< 85% zeros): BWT+MTF+RLE2 is much better
                         *   because BWT reorganizes the non-zero bytes efficiently.
                         * Very sparse (>= 85% zeros): rANS order-0 is sufficient
                         *   and avoids BWT overhead. */
                        size_t zero_count = 0;
                        for (size_t zi = 0; zi < block_in_size; zi++) {
                            if (block_in[zi] == 0) zero_count++;
                        }
                        int zero_pct = (int)(100 * zero_count / block_in_size);
                        
                        if (zero_pct < 90) {
                            /* Diverse stride output → BWT + MTF + RLE2 pipeline */
                            genome.use_bwt = 1;
                            genome.use_mtf_rle = 1;
                            genome.use_delta = 0;
                            genome.entropy_coder = 1; /* rANS */
                            genome.cm_learning = 0;
                        } else {
                            /* Very sparse → skip BWT and MTF (overhead on near-sorted data).
                             * RLE2 applied separately below for STRIDE strategy. */
                            genome.use_bwt = 0;
                            genome.use_mtf_rle = 0;
                            genome.use_delta = 0;
                            genome.entropy_coder = 1; /* rANS */
                            genome.cm_learning = 0;
                        }
                    } else {
                        genome = mcx_evolve(block_in, block_in_size, 12);
                    }
                } else {
                    /* For smart mode (L20+), use the effective level matching
                     * the selected strategy, not the user-requested level. */
                    int eff_level = level;
                    if (level >= 20) {
                        if (strategy == MCX_STRATEGY_DEFAULT) eff_level = 12;
                        else if (strategy == MCX_STRATEGY_BEST) eff_level = 17;
                        else if (strategy == MCX_STRATEGY_LZ_HC) eff_level = 9;
                    }
                    if (level >= 20 && strategy == MCX_STRATEGY_DEFAULT) {
                        /* Smart mode: force optimal genome.
                         * BWT + MTF + RLE2 + rANS, no delta. */
                        genome.use_bwt = 1;
                        genome.use_mtf_rle = 1;
                        genome.use_delta = 0;
                        genome.entropy_coder = 1; /* rANS */
                        genome.cm_learning = 0;
                    } else {
                        genome = mcx_evolve(block_in, block_in_size, eff_level);
                    }
                }
                
            /* Fix genome optimizer: force BWT + MTF at L10-L14.
             * The optimizer sometimes picks bwt=0 (raw rANS) which defeats
             * the purpose of L10+ levels. ooffice went from 1.20x to 2.18x. */
            if (level >= 10 && level <= 14 && strategy == MCX_STRATEGY_DEFAULT) {
                if (!genome.use_bwt) {
                    genome.use_bwt = 1;
                    genome.use_mtf_rle = 1;
                }
            }

            /* Fix genome optimizer delta bug: if the optimizer picked delta=1
             * but data is mostly text-like (< 5% bytes >= 128), disable delta.
             * Delta on text/low-entropy data destroys BWT-friendly patterns.
             * But some binary data (like kennedy.xls) genuinely benefits from delta. */
            if (genome.use_delta && genome.use_bwt) {
                size_t high128 = 0;
                size_t check_len = block_in_size > 4096 ? 4096 : block_in_size;
                for (size_t ci = 0; ci < check_len; ci++) {
                    if (block_in[ci] >= 128) high128++;
                }
                if (high128 * 20 < check_len) { /* < 5% high bytes → text */
                    genome.use_delta = 0;
                }
            }

            /* Genome byte written later (after RLE2 may modify cm_learning) */
            size_t payload_offset = 1;
            
            /* For Babel/Stride, always store preproc size (0 = no preproc applied) */
            if (strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE) {
                memcpy(out1 + payload_offset, &babel_size32, 4);
                payload_offset += 4;
            }
            
            const uint8_t* stage_in = block_in;
            size_t stage_size = block_in_size;
            
            if (genome.use_delta) {
                if (stage_in != buf0) memcpy(buf0, stage_in, stage_size);
                mcx_delta_encode(buf0, stage_size);
                stage_in = buf0;
            }

            uint64_t pidx64 = 0;
            if (genome.use_bwt) {
                size_t primary_idx;
                size_t bwt_result = mcx_bwt_forward(buf1, &primary_idx, stage_in, stage_size);
                if (MCX_IS_ERROR(bwt_result)) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf0); free(buf1); free(buf2); free(out1); if (babel_buf) free(babel_buf);
                    continue;
                }
                pidx64 = (uint64_t)primary_idx;
                memcpy(out1 + payload_offset, &pidx64, 8);
                payload_offset += 8;
                stage_in = buf1;
            }
            
            uint32_t rle32 = (uint32_t)stage_size;
            if (genome.use_mtf_rle) {
                if (stage_in != buf1) {
                    memcpy(buf1, stage_in, stage_size);
                    stage_in = buf1;
                }
                mcx_mtf_encode((uint8_t*)stage_in, stage_size);
                size_t rle_cap = stage_size + (stage_size / 4) + 1024;
                size_t rle_size;
                /* Use RLE2 (RUNA/RUNB) for text in smart mode — 5-8% better.
                 * Signal via cm_learning=7 when entropy_coder is rANS (not CM-rANS). */
                /* RLE2 (RUNA/RUNB) is better than RLE1 on BWT+MTF output when
                 * the data has few high-value bytes (254/255 need 2-byte escapes).
                 * After BWT+MTF, text has very few 254/255 bytes; binary has more.
                 * Check the MTF output before RLE: if < 1% is 254+, use RLE2. */
                size_t high_count = 0;
                for (size_t hi = 0; hi < stage_size; hi++) {
                    if (((uint8_t*)stage_in)[hi] >= 254) high_count++;
                }
                int use_rle2 = (genome.entropy_coder != 2 &&
                                high_count * 100 < stage_size); /* < 1% high bytes */
                if (use_rle2) {
                    rle_size = mcx_rle2_encode(buf2, rle_cap, stage_in, stage_size);
                    genome.cm_learning = 7; /* Flag for decoder */
                } else {
                    rle_size = mcx_rle_encode(buf2, rle_cap, stage_in, stage_size);
                }
                if (MCX_IS_ERROR(rle_size) || rle_size == 0) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf0); free(buf1); free(buf2); free(out1); if (babel_buf) free(babel_buf);
                    continue;
                }
                stage_in = buf2;
                stage_size = rle_size;
                rle32 = (uint32_t)rle_size;
                memcpy(out1 + payload_offset, &rle32, 4);
                payload_offset += 4;
            }

            /* For STRIDE without BWT: apply RLE2 directly on stride-delta output.
             * RLE2 crushes zero-runs (91% zeros in ptt5) much better than raw rANS.
             * Test: ptt5 10.20x with RLE2+rANS vs 8.84x rANS-only. */
            if (strategy == MCX_STRATEGY_STRIDE && !genome.use_bwt && !genome.use_mtf_rle
                && genome.entropy_coder != 2) {
                size_t rle_cap2 = stage_size + (stage_size / 4) + 1024;
                size_t rle_sz2 = mcx_rle2_encode(buf2, rle_cap2, stage_in, stage_size);
                if (!MCX_IS_ERROR(rle_sz2) && rle_sz2 > 0 && rle_sz2 < stage_size) {
                    uint32_t rle32_stride = (uint32_t)rle_sz2;
                    memcpy(out1 + payload_offset, &rle32_stride, 4);
                    payload_offset += 4;
                    stage_in = buf2;
                    stage_size = rle_sz2;
                    genome.cm_learning = 7; /* Signal RLE2 to decoder */
                }
            }

            /* Write genome byte now (after all RLE2 flags have been set) */
            out1[0] = mcx_encode_genome(&genome);

            size_t entropy_size;
            if (genome.entropy_coder == 2) {
                entropy_size = mcx_cmrans_compress(out1 + payload_offset, max_out - payload_offset, stage_in, stage_size);
            } else if (genome.entropy_coder == 1) {
                /* For large text blocks in L20+, try multi-table rANS (like bzip2's
                 * multi-table Huffman). Falls back to single rANS if multi is larger. */
                if (level >= 12 && stage_size > 32768 && genome.use_bwt && !genome.use_delta) {
                    /* Try both single and multi-table rANS, keep smaller */
                    size_t avail = max_out - payload_offset;
                    uint8_t* alt_buf = (uint8_t*)malloc(avail);
                    if (alt_buf) {
                        size_t single_sz = mcx_rans_compress(out1 + payload_offset, avail, stage_in, stage_size);
                        size_t multi_sz = mcx_multi_rans_compress(alt_buf, avail, stage_in, stage_size);
                        if (!mcx_is_error(multi_sz) && (mcx_is_error(single_sz) || multi_sz < single_sz)) {
                            memcpy(out1 + payload_offset, alt_buf, multi_sz);
                            entropy_size = multi_sz;
                            genome.cm_learning = 6; /* Signal multi-table to decoder */
                            out1[0] = mcx_encode_genome(&genome);
                        } else {
                            entropy_size = single_sz;
                        }
                        free(alt_buf);
                    } else {
                        entropy_size = mcx_rans_compress(out1 + payload_offset, avail, stage_in, stage_size);
                    }
                } else {
                    entropy_size = mcx_rans_compress(out1 + payload_offset, max_out - payload_offset, stage_in, stage_size);
                }
            } else {
                entropy_size = mcx_huffman_compress(out1 + payload_offset, max_out - payload_offset, stage_in, stage_size, NULL);
            }
            
            if (MCX_IS_ERROR(entropy_size)) {
                #pragma omp critical
                { omp_err = 1; }
                free(buf0); free(buf1); free(buf2); free(out1); if (babel_buf) free(babel_buf);
                continue;
            }

            block_buffers[b] = out1;
            block_sizes[b] = payload_offset + entropy_size;

            free(buf0); free(buf1); free(buf2);
            if (babel_buf) free(babel_buf);
        }
        } /* end BWT path block */

        if (omp_err) {
            for (uint32_t b = 0; b < num_blocks; b++) {
                if (block_buffers[b]) free(block_buffers[b]);
            }
            free(block_buffers); free(block_sizes);
            if (block_src_offsets) free(block_src_offsets);
            if (block_src_sizes_arr) free(block_src_sizes_arr);
            if (orig_block_sizes) free(orig_block_sizes);
            return MCX_ERROR(MCX_ERR_GENERIC);
        }

        for (uint32_t b = 0; b < num_blocks; b++) {
            uint32_t bsize32 = (uint32_t)block_sizes[b];
            memcpy(out + block_sizes_offset + (b * 4), &bsize32, 4);
            
            if (offset + block_sizes[b] > dst_cap) {
                for (uint32_t k = 0; k < num_blocks; k++) {
                    if (block_buffers[k]) free(block_buffers[k]);
                }
                free(block_buffers); free(block_sizes);
                return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
            }
            
            memcpy(out + offset, block_buffers[b], block_sizes[b]);
            offset += block_sizes[b];
            free(block_buffers[b]);
        }
        free(block_buffers);
        free(block_sizes);
        if (block_src_offsets) free(block_src_offsets);
        if (block_src_sizes_arr) free(block_src_sizes_arr);
        if (orig_block_sizes) free(orig_block_sizes);
    }

    /* ── Multi-trial for L20+: try alternative strategies, keep smallest ── */
    if (level >= 20 && level <= 22 && strategy != MCX_STRATEGY_STORE 
        && strategy != MCX_STRATEGY_LZ_HC && strategy != MCX_STRATEGY_LZ_FAST
        && strategy != MCX_STRATEGY_LZ24) {
        uint8_t* alt_buf = (uint8_t*)malloc(dst_cap);
        if (alt_buf) {
            /* Try LZ-HC (L9) — BWT usually wins on text, but LZ+AAC can beat BWT
             * on highly repetitive data where LZ's sliding window excels.
             * Skip for large text (>1MB) where BWT dominates and L9 is slow. */
            if (!(analysis.type == MCX_DTYPE_TEXT_ASCII ||
                  analysis.type == MCX_DTYPE_TEXT_UTF8 ||
                  analysis.type == MCX_DTYPE_STRUCTURED) ||
                src_size <= 1024 * 1024) {
                size_t alt_size = mcx_compress(alt_buf, dst_cap, src, src_size, 9);
                if (!mcx_is_error(alt_size) && alt_size < offset) {
                    memcpy(dst, alt_buf, alt_size);
                    offset = alt_size;
                }
            }
            /* Try BWT with genome optimizer (L12) — might find a better genome
             * than the forced genome at L20 (especially for binary data).
             * Skip for text (forced genome already optimal) and large files (>16MB). */
            if (src_size <= MCX_MAX_BLOCK_SIZE &&
                analysis.type != MCX_DTYPE_TEXT_ASCII &&
                analysis.type != MCX_DTYPE_TEXT_UTF8) {
                size_t alt12 = mcx_compress(alt_buf, dst_cap, src, src_size, 12);
                if (!mcx_is_error(alt12) && alt12 < offset) {
                    memcpy(dst, alt_buf, alt12);
                    offset = alt12;
                }
            }
            /* Try LZRC (L26) — v2.0 LZ+RC with 16MB window.
             * Best on binary archives, skip on pure text (BWT always wins).
             * Skip if LZRC is already the primary strategy.
             * Limit to ≤16MB — BT match finder uses ~8 bytes/position. */
            if (strategy != MCX_STRATEGY_LZRC &&
                src_size <= (16 << 20) &&
                analysis.type != MCX_DTYPE_TEXT_ASCII &&
                analysis.type != MCX_DTYPE_TEXT_UTF8) {
                size_t alt_lzrc = mcx_compress(alt_buf, dst_cap, src, src_size, 26);
                if (!mcx_is_error(alt_lzrc) && alt_lzrc < offset) {
                    memcpy(dst, alt_buf, alt_lzrc);
                    offset = alt_lzrc;
                }
            }
            free(alt_buf);
        }
    }
    
    /* ── E8/E9 x86 filter trial ──
     * For non-text data at L20+, try preprocessing with E8/E9 filter.
     * This converts relative x86 CALL/JMP addresses to absolute,
     * dramatically improving BWT compression of executable binaries.
     * Only try if data might contain x86 code (BINARY or EXECUTABLE). */
    if (level == 20 &&
        analysis.type != MCX_DTYPE_TEXT_ASCII &&
        analysis.type != MCX_DTYPE_TEXT_UTF8 &&
        analysis.type != MCX_DTYPE_STRUCTURED &&
        src_size >= 64) {
        /* Quick check: count E8/E9 opcodes. Only try if >= 0.5% of bytes
         * are E8 or E9 (typical for x86 executables: 2-5%, non-exe: <0.1%) */
        size_t e8e9_count = 0;
        for (size_t i = 0; i < src_size - 4; i++) {
            if (in[i] == 0xE8 || in[i] == 0xE9) { e8e9_count++; i += 4; }
        }
        if (e8e9_count * 200 < src_size) goto skip_e8e9; /* < 0.5% */
        
        /* Make a copy, apply E8/E9, compress at same level (but skip this
         * trial recursively by using level 21 as a "no E8/E9 retry" flag) */
        uint8_t* e8_buf = (uint8_t*)malloc(src_size);
        uint8_t* e8_dst = (uint8_t*)malloc(dst_cap);
        if (e8_buf && e8_dst) {
            memcpy(e8_buf, src, src_size);
            mcx_e8e9_encode(e8_buf, src_size);
            /* Compress the E8/E9-filtered data at level 21 (skips E8/E9 retry) */
            size_t e8_size = mcx_compress(e8_dst, dst_cap, e8_buf, src_size, 21);
            if (!mcx_is_error(e8_size) && e8_size < offset) {
                /* E8/E9 version is smaller — use it and set the flag */
                memcpy(dst, e8_dst, e8_size);
                /* Set E8/E9 flag in the frame header */
                ((uint8_t*)dst)[5] |= MCX_FLAG_E8E9;
                offset = e8_size;
            }
        }
        if (e8_buf) free(e8_buf);
        if (e8_dst) free(e8_dst);
    }
skip_e8e9:

    return offset;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Decompress
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_decompress(void* dst, size_t dst_cap,
                      const void* src, size_t src_size)
{
    const uint8_t* in;
    uint8_t* out;
    mcx_frame_header_t header;
    size_t orig_size;
    size_t offset;
    mcx_strategy_t strategy;

    if (dst == NULL || src == NULL || src_size < MCX_FRAME_HEADER_SIZE) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }

    in = (const uint8_t*)src;
    out = (uint8_t*)dst;

    /* ── Read frame header ── */
    if (read_frame_header(in, src_size, &header) != 0) {
        return MCX_ERROR(MCX_ERR_INVALID_MAGIC);
    }

    orig_size = (size_t)header.original_size;
    if (orig_size > dst_cap) {
        return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    }

    offset = MCX_FRAME_HEADER_SIZE;
    strategy = (mcx_strategy_t)header.strategy;

    if (strategy == MCX_STRATEGY_STORE) {
        /* Raw copy */
        if (offset + orig_size > src_size) {
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }
        memcpy(out, in + offset, orig_size);
        /* Apply E8/E9 inverse if flag is set */
        if (header.flags & MCX_FLAG_E8E9) mcx_e8e9_decode(out, orig_size);
        return orig_size;

    } else if (strategy == MCX_STRATEGY_FAST) {
        /* Huffman only */
        return mcx_huffman_decompress(out, dst_cap, in + offset, src_size - offset);

    } else if (strategy != MCX_STRATEGY_STORE && strategy != MCX_STRATEGY_FAST) {
        uint32_t num_blocks;
        if (offset + 4 > src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        memcpy(&num_blocks, in + offset, 4);
        offset += 4;
        
        size_t* block_sizes = (size_t*)malloc(num_blocks * sizeof(size_t));
        if (!block_sizes) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);

        if (offset + num_blocks * 4 > src_size) {
            free(block_sizes);
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }

        /* Read the array of compressed chunk sizes */
        for (uint32_t b = 0; b < num_blocks; b++) {
            uint32_t bsize32;
            memcpy(&bsize32, in + offset + (b * 4), 4);
            block_sizes[b] = (size_t)bsize32;
        }
        offset += num_blocks * 4;

        /* Read original block sizes if adaptive blocks flag is set */
        size_t* orig_block_sizes_dec = NULL;
        size_t* dst_block_offsets = NULL;
        if (header.flags & MCX_FLAG_ADAPTIVE_BLOCKS) {
            if (offset + num_blocks * 4 > src_size) {
                free(block_sizes);
                return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            }
            orig_block_sizes_dec = (size_t*)malloc(num_blocks * sizeof(size_t));
            dst_block_offsets = (size_t*)malloc(num_blocks * sizeof(size_t));
            if (!orig_block_sizes_dec || !dst_block_offsets) {
                free(block_sizes);
                if (orig_block_sizes_dec) free(orig_block_sizes_dec);
                if (dst_block_offsets) free(dst_block_offsets);
                return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
            }
            size_t cum_dst = 0;
            for (uint32_t b = 0; b < num_blocks; b++) {
                uint32_t obs32;
                memcpy(&obs32, in + offset + (b * 4), 4);
                orig_block_sizes_dec[b] = (size_t)obs32;
                dst_block_offsets[b] = cum_dst;
                cum_dst += orig_block_sizes_dec[b];
            }
            offset += num_blocks * 4;
        }

        /* Calculate absolute offset boundaries to decouple thread reads */
        size_t* block_offsets = (size_t*)malloc(num_blocks * sizeof(size_t));
        if (!block_offsets) {
            free(block_sizes);
            if (orig_block_sizes_dec) free(orig_block_sizes_dec);
            if (dst_block_offsets) free(dst_block_offsets);
            return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
        }
        
        size_t current_offset = offset;
        for (uint32_t b = 0; b < num_blocks; b++) {
            block_offsets[b] = current_offset;
            current_offset += block_sizes[b];
        }
        
        if (current_offset > src_size) {
            free(block_sizes); free(block_offsets);
            if (orig_block_sizes_dec) free(orig_block_sizes_dec);
            if (dst_block_offsets) free(dst_block_offsets);
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }

        int omp_err = 0;
        
        int32_t b;
        #pragma omp parallel for schedule(dynamic)
        for (b = 0; b < (int32_t)num_blocks; b++) {
            if (omp_err) continue;

            size_t dst_offset, block_dst_size;
            if (orig_block_sizes_dec) {
                dst_offset = dst_block_offsets[b];
                block_dst_size = orig_block_sizes_dec[b];
            } else {
                dst_offset = (size_t)b * MCX_MAX_BLOCK_SIZE;
                block_dst_size = orig_size - dst_offset;
                if (block_dst_size > MCX_MAX_BLOCK_SIZE) {
                    block_dst_size = MCX_MAX_BLOCK_SIZE;
                }
            }

            size_t chunk_src_offset = block_offsets[b];
            size_t chunk_comp_size  = block_sizes[b];
            
            if (chunk_comp_size < 1) { /* genome byte / magic byte */
                #pragma omp critical
                { omp_err = 1; }
                continue;
            }

            /* Dispatch based on block's first byte, not file-level strategy.
             * This supports per-block strategy selection (L20 may mix BWT and LZ blocks). */
            uint8_t block_type = in[chunk_src_offset];
            
            if (block_type == 0x00) {
                /* STORE fallback */
                if (chunk_comp_size - 1 != block_dst_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
                memcpy(out + dst_offset, in + chunk_src_offset + 1, block_dst_size);
            } else if (block_type == 0xB0) {
                /* 0xB0 = LZRC (v2.0 LZ + Range Coder) */
                size_t dec_size = mcx_lzrc_decompress(
                    out + dst_offset, block_dst_size,
                    in + chunk_src_offset + 1, chunk_comp_size - 1);
                if (dec_size != block_dst_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
            } else if (block_type == 0xAE) {
                /* 0xAE = LZ77 + Adaptive AC */
                size_t lz_cap = mcx_lz_compress_bound(block_dst_size);
                uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                if (!lz_buf) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
                size_t lz_size = mcx_adaptive_ac_decompress(lz_buf, lz_cap,
                                                             in + chunk_src_offset + 1,
                                                             chunk_comp_size - 1);
                if (lz_size == 0) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(lz_buf);
                    continue;
                }
                size_t decomp = mcx_lz_decompress(out + dst_offset, block_dst_size,
                                                  lz_buf, lz_size, block_dst_size);
                free(lz_buf);
                if (decomp != block_dst_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
            } else if (block_type == 0xAA || block_type == 0xAB || block_type == 0xA8) {
                    /* 0xAA = LZ77 + FSE, 0xA8 = LZ77 + rANS, 0xAB = LZ77 raw */
                    size_t lz_cap = mcx_lz_compress_bound(block_dst_size);
                    uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                    if (!lz_buf) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }

                    size_t lz_size;
                    if (block_type == 0xAA) {
                        lz_size = mcx_fse_decompress(lz_buf, lz_cap,
                                                     in + chunk_src_offset + 1,
                                                     chunk_comp_size - 1);
                    } else if (block_type == 0xA8) {
                        lz_size = mcx_rans_decompress(lz_buf, lz_cap,
                                                      in + chunk_src_offset + 1,
                                                      chunk_comp_size - 1);
                    } else {
                        /* 0xAB: LZ77 output stored raw */
                        lz_size = chunk_comp_size - 1;
                        if (lz_size > lz_cap) lz_size = 0;
                        else memcpy(lz_buf, in + chunk_src_offset + 1, lz_size);
                    }

                    if (lz_size == 0) {
                        #pragma omp critical
                        { omp_err = 1; }
                        free(lz_buf);
                        continue;
                    }

                    size_t decomp = mcx_lz_decompress(out + dst_offset, block_dst_size,
                                                      lz_buf, lz_size, block_dst_size);
                    free(lz_buf);

                    if (decomp != block_dst_size) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }
                } else if (block_type == 0xAF) {
                    /* 0xAF = LZ24 + Adaptive AC */
                    size_t lz_cap = mcx_lz24_compress_bound(block_dst_size);
                    uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                    if (!lz_buf) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }
                    size_t lz_size = mcx_adaptive_ac_decompress(lz_buf, lz_cap,
                                                                 in + chunk_src_offset + 1,
                                                                 chunk_comp_size - 1);
                    if (lz_size == 0) {
                        #pragma omp critical
                        { omp_err = 1; }
                        free(lz_buf);
                        continue;
                    }
                    size_t decomp = mcx_lz24_decompress(out + dst_offset, block_dst_size,
                                                         lz_buf, lz_size, block_dst_size);
                    free(lz_buf);
                    if (decomp != block_dst_size) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }
                } else if (block_type == 0xAC || block_type == 0xAD || block_type == 0xA9) {
                    size_t lz_cap = mcx_lz24_compress_bound(block_dst_size);
                    uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                    if (!lz_buf) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }

                    size_t lz_size;
                    if (block_type == 0xAC) {
                        lz_size = mcx_fse_decompress(lz_buf, lz_cap,
                                                     in + chunk_src_offset + 1,
                                                     chunk_comp_size - 1);
                    } else if (block_type == 0xA9) {
                        lz_size = mcx_rans_decompress(lz_buf, lz_cap,
                                                      in + chunk_src_offset + 1,
                                                      chunk_comp_size - 1);
                    } else {
                        lz_size = chunk_comp_size - 1;
                        if (lz_size > lz_cap) lz_size = 0;
                        else memcpy(lz_buf, in + chunk_src_offset + 1, lz_size);
                    }

                    if (lz_size == 0) {
                        #pragma omp critical
                        { omp_err = 1; }
                        free(lz_buf);
                        continue;
                    }

                    size_t decomp = mcx_lz24_decompress(out + dst_offset, block_dst_size,
                                                         lz_buf, lz_size, block_dst_size);
                    free(lz_buf);

                    if (decomp != block_dst_size) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }
                } else {
                /* BWT Path (+ Babel inverse for BABEL strategy) */
                mcx_genome genome = mcx_decode_genome(in[chunk_src_offset]);
            size_t payload_offset = 1;
            
            /* For Babel strategy, read the babel output size */
            uint32_t babel_size32 = 0;
            size_t babel_intermediate_size = 0;
            if (strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE) {
                if (payload_offset + 4 > chunk_comp_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
                memcpy(&babel_size32, in + chunk_src_offset + payload_offset, 4);
                babel_intermediate_size = (size_t)babel_size32;
                payload_offset += 4;
            }
            
            /* For Babel, the BWT pipeline decompresses to babel_intermediate_size,
             * not block_dst_size. We then run babel_inverse to get block_dst_size. */
            size_t bwt_target_size = ((strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE) && babel_intermediate_size > 0)
                                   ? babel_intermediate_size : block_dst_size;

            uint64_t pidx64 = 0;
            size_t primary_idx = 0;
            if (genome.use_bwt) {
                if (payload_offset + 8 > chunk_comp_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
                memcpy(&pidx64, in + chunk_src_offset + payload_offset, 8);
                primary_idx = (size_t)pidx64;
                payload_offset += 8;
            }

            uint32_t rle32 = 0;
            size_t rle_size = bwt_target_size; /* Default if no BWT/MTF/RLE */
            /* Read RLE size if MTF+RLE is used, OR if STRIDE RLE2 flag is set */
            int has_rle_header = genome.use_mtf_rle || 
                                 (genome.cm_learning >= 6 && genome.entropy_coder != 2 && !genome.use_mtf_rle);
            if (has_rle_header) {
                if (payload_offset + 4 > chunk_comp_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
                memcpy(&rle32, in + chunk_src_offset + payload_offset, 4);
                rle_size = (size_t)rle32;
                payload_offset += 4;
            }

            uint8_t* buf1 = (uint8_t*)malloc(rle_size + 1024);
            uint8_t* buf2 = (uint8_t*)malloc(bwt_target_size + 1024);
            /* For Babel, we need an extra buffer for the intermediate decoded data */
            uint8_t* babel_dec_buf = NULL;
            if ((strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE) && babel_intermediate_size > 0) {
                babel_dec_buf = (uint8_t*)malloc(babel_intermediate_size + 1024);
                if (!babel_dec_buf) {
                    #pragma omp critical
                    { omp_err = 1; }
                    if (buf1) free(buf1);
                    if (buf2) free(buf2);
                    continue;
                }
            }
            
            if (!buf1 || !buf2) {
                #pragma omp critical
                { omp_err = 1; }
                if (buf1) free(buf1); 
                if (buf2) free(buf2);
                continue;
            }

            struct timespec _prof_t0, _prof_t1, _prof_t2, _prof_t3, _prof_t4;
            int _do_prof = mcx_profile();
            if (_do_prof) clock_gettime(CLOCK_MONOTONIC, &_prof_t0);

            size_t dec_res;
            if (genome.entropy_coder == 2) {
                dec_res = mcx_cmrans_decompress(
                    buf1, rle_size + 1024, in + chunk_src_offset + payload_offset, chunk_comp_size - payload_offset);
            } else if (genome.entropy_coder == 1) {
                if (genome.cm_learning == 6) {
                    /* Multi-table rANS */
                    dec_res = mcx_multi_rans_decompress(
                        buf1, rle_size + 1024, in + chunk_src_offset + payload_offset, chunk_comp_size - payload_offset);
                } else {
                    dec_res = mcx_rans_decompress(
                        buf1, rle_size + 1024, in + chunk_src_offset + payload_offset, chunk_comp_size - payload_offset);
                }
            } else {
                dec_res = mcx_huffman_decompress(
                    buf1, rle_size + 1024, in + chunk_src_offset + payload_offset, chunk_comp_size - payload_offset);
            }
            if (_do_prof) clock_gettime(CLOCK_MONOTONIC, &_prof_t1);

            if (MCX_IS_ERROR(dec_res)) {
                #pragma omp critical
                { omp_err = 1; }
                free(buf1); free(buf2);
                continue;
            }

            uint8_t* stage_out = buf1;
            size_t stage_size = dec_res;

            if (genome.use_mtf_rle) {
                size_t rle_dec;
                /* Check cm_learning>=6 flag for RLE2 (RUNA/RUNB) decoding.
                 * cm_learning=6: multi-rANS + RLE2
                 * cm_learning=7: single rANS + RLE2 */
                if (genome.cm_learning >= 6 && genome.entropy_coder != 2) {
                    rle_dec = mcx_rle2_decode(buf2, bwt_target_size + 1024, stage_out, stage_size);
                } else {
                    rle_dec = mcx_rle_decode(buf2, bwt_target_size + 1024, stage_out, stage_size);
                }
                if (_do_prof) clock_gettime(CLOCK_MONOTONIC, &_prof_t2);
                if (MCX_IS_ERROR(rle_dec) || rle_dec == 0) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2);
                    continue;
                }
                mcx_mtf_decode(buf2, rle_dec);
                if (_do_prof) clock_gettime(CLOCK_MONOTONIC, &_prof_t3);
                stage_out = buf2;
                stage_size = rle_dec;
            } else if (genome.cm_learning >= 6 && genome.entropy_coder != 2) {
                /* STRIDE without BWT/MTF but with RLE2 (sparse zero-run case).
                 * Read rle32 size prefix, then decode RLE2. */
                uint32_t rle32_val;
                memcpy(&rle32_val, stage_out, 4);  /* Wrong — rle32 is in the header, not in entropy data */
                /* Actually, the rle32 was written before entropy coding,
                 * so it's already consumed by the payload parsing above.
                 * The entropy decoder output IS the RLE2-encoded data.
                 * We just need to RLE2-decode it. */
                size_t rle_dec = mcx_rle2_decode(buf2, bwt_target_size + 1024, stage_out, stage_size);
                if (MCX_IS_ERROR(rle_dec) || rle_dec == 0) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2);
                    continue;
                }
                stage_out = buf2;
                stage_size = rle_dec;
            }

            if (genome.use_bwt) {
                /* For Babel, decode BWT into intermediate buffer, not final output */
                uint8_t* bwt_dst = ((strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE) && babel_dec_buf)
                                 ? babel_dec_buf : (out + dst_offset);
                size_t bwt_dec = mcx_bwt_inverse(bwt_dst, primary_idx, stage_out, stage_size);
                if (_do_prof) clock_gettime(CLOCK_MONOTONIC, &_prof_t4);
                if (MCX_IS_ERROR(bwt_dec) || bwt_dec != bwt_target_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2); if (babel_dec_buf) free(babel_dec_buf);
                    continue;
                }
                if (_do_prof) {
                    fprintf(stderr, "[PROFILE] Block %d (%zu bytes):\n", b,
                            bwt_target_size);
                    fprintf(stderr, "  rANS decode:   %7.2f ms\n",
                            mcx_time_ms(&_prof_t0, &_prof_t1));
                    if (genome.use_mtf_rle) {
                        fprintf(stderr, "  RLE2 decode:   %7.2f ms\n",
                                mcx_time_ms(&_prof_t1, &_prof_t2));
                        fprintf(stderr, "  MTF decode:    %7.2f ms\n",
                                mcx_time_ms(&_prof_t2, &_prof_t3));
                        fprintf(stderr, "  BWT inverse:   %7.2f ms\n",
                                mcx_time_ms(&_prof_t3, &_prof_t4));
                    } else {
                        fprintf(stderr, "  BWT inverse:   %7.2f ms\n",
                                mcx_time_ms(&_prof_t1, &_prof_t4));
                    }
                    fprintf(stderr, "  TOTAL:         %7.2f ms\n",
                            mcx_time_ms(&_prof_t0, &_prof_t4));
                }
                stage_out = bwt_dst;
            } else {
                if (stage_size != bwt_target_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2); if (babel_dec_buf) free(babel_dec_buf);
                    continue;
                }
                if ((strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE) && babel_dec_buf) {
                    memcpy(babel_dec_buf, stage_out, stage_size);
                    stage_out = babel_dec_buf;
                } else {
                    memcpy(out + dst_offset, stage_out, stage_size);
                    stage_out = out + dst_offset;
                }
            }

            if (genome.use_delta) {
                mcx_delta_decode(stage_out, bwt_target_size);
            }
            
            /* ── Babel/Stride inverse ── */
            if (strategy == MCX_STRATEGY_BABEL && babel_dec_buf && babel_intermediate_size > 0) {
                size_t babel_dec = mcx_babel_inverse(out + dst_offset, block_dst_size,
                                                     stage_out, babel_intermediate_size);
                if (babel_dec != block_dst_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2); free(babel_dec_buf);
                    continue;
                }
            } else if (strategy == MCX_STRATEGY_STRIDE && babel_dec_buf && babel_intermediate_size > 0) {
                size_t stride_dec = mcx_babel_stride_inverse(out + dst_offset, block_dst_size,
                                                              stage_out, babel_intermediate_size);
                if (stride_dec != block_dst_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2); free(babel_dec_buf);
                    continue;
                }
            }

            free(buf1); free(buf2); if (babel_dec_buf) free(babel_dec_buf);
            } /* end BWT path block */
        } /* end parallel loops */

        free(block_sizes);
        free(block_offsets);
        if (orig_block_sizes_dec) free(orig_block_sizes_dec);
        if (dst_block_offsets) free(dst_block_offsets);

        if (omp_err) {
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }

        /* Apply E8/E9 inverse if flag is set */
        if (header.flags & MCX_FLAG_E8E9) mcx_e8e9_decode(out, orig_size);
        return orig_size;
    }

    return MCX_ERROR(MCX_ERR_UNKNOWN_BLOCK);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Decompressed Size
 * ═══════════════════════════════════════════════════════════════════════ */

unsigned long long mcx_get_decompressed_size(const void* src, size_t src_size)
{
    mcx_frame_header_t header;
    if (src == NULL || src_size < MCX_FRAME_HEADER_SIZE) return 0;

    if (read_frame_header((const uint8_t*)src, src_size, &header) != 0) {
        return 0;
    }

    if (header.flags & MCX_FLAG_HAS_ORIG_SIZE) {
        return header.original_size;
    }

    return 0;
}

size_t mcx_get_frame_info(mcx_frame_info* info, const void* src, size_t src_size)
{
    if (info == NULL || src == NULL || src_size < MCX_FRAME_HEADER_SIZE)
        return MCX_ERROR(MCX_ERR_GENERIC);

    mcx_frame_header_t header;
    if (read_frame_header((const uint8_t*)src, src_size, &header) != 0)
        return MCX_ERROR(MCX_ERR_INVALID_MAGIC);

    info->original_size = (header.flags & MCX_FLAG_HAS_ORIG_SIZE) ? header.original_size : 0;
    info->version = header.version;
    info->level = header.level;
    info->strategy = header.strategy;
    info->flags = header.flags;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Streaming Compression API
 * ═══════════════════════════════════════════════════════════════════════ */

static size_t stream_compress_block(mcx_cctx* cctx)
{
    mcx_strategy_t strategy = (mcx_strategy_t)cctx->header[7];
    size_t comp_size = 0;

    if (strategy == MCX_STRATEGY_FAST) {
        comp_size = mcx_huffman_compress(
            cctx->out_buf + 4, cctx->out_size - 4,
            cctx->in_buf, cctx->in_pos, NULL);
        return comp_size;
    }

    if (strategy == MCX_STRATEGY_LZ_FAST || strategy == MCX_STRATEGY_LZ_HC) {
        size_t lz_cap = mcx_lz_compress_bound(cctx->in_pos);
        uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
        size_t fse_cap = mcx_compress_bound(lz_cap);
        uint8_t* fse_buf = (uint8_t*)malloc(fse_cap + 1);

        if (!lz_buf || !fse_buf) {
            free(lz_buf); free(fse_buf);
            return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
        }

        size_t lz_size;
        if (strategy == MCX_STRATEGY_LZ_HC) {
            lz_size = mcx_lz_compress_hc(lz_buf, lz_cap, cctx->in_buf, cctx->in_pos, cctx->level);
        } else {
            lz_size = mcx_lz_compress(lz_buf, lz_cap, cctx->in_buf, cctx->in_pos, 1);
        }

        if (lz_size == 0) {
            free(lz_buf); free(fse_buf);
            return MCX_ERROR(MCX_ERR_GENERIC);
        }

        fse_buf[0] = 0xAA; /* LZ77+FSE flag */
        size_t fse_size = mcx_fse_compress(fse_buf + 1, fse_cap, lz_buf, lz_size);
        free(lz_buf);

        if (fse_size == 0 || fse_size >= cctx->in_pos) {
            fse_buf[0] = 0x00; /* STORE fallback */
            memcpy(fse_buf + 1, cctx->in_buf, cctx->in_pos);
            fse_size = cctx->in_pos;
        }

        if (4 + fse_size + 1 > cctx->out_size) {
            free(fse_buf);
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }

        memcpy(cctx->out_buf + 4, fse_buf, fse_size + 1);
        free(fse_buf);
        return fse_size + 1;
    } else if (!(strategy == MCX_STRATEGY_DEFAULT || strategy == MCX_STRATEGY_BEST ||
                 strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE)) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }

    /* ── Stride/Babel preprocessing ── */
    uint8_t* preproc_buf = NULL;
    const uint8_t* preproc_in = cctx->in_buf;
    size_t preproc_size = cctx->in_pos;
    uint32_t preproc_size32 = 0;

    if (strategy == MCX_STRATEGY_STRIDE) {
        size_t stride_cap = mcx_babel_stride_bound(cctx->in_pos);
        preproc_buf = (uint8_t*)malloc(stride_cap);
        if (preproc_buf) {
            size_t stride_out = mcx_babel_stride_forward(preproc_buf, stride_cap,
                                                          cctx->in_buf, cctx->in_pos);
            if (stride_out > 0) {
                preproc_in = preproc_buf;
                preproc_size = stride_out;
                preproc_size32 = (uint32_t)stride_out;
            } else {
                free(preproc_buf);
                preproc_buf = NULL;
            }
        }
    } else if (strategy == MCX_STRATEGY_BABEL) {
        size_t babel_cap = mcx_babel_bound(cctx->in_pos);
        preproc_buf = (uint8_t*)malloc(babel_cap);
        if (preproc_buf) {
            size_t babel_out = mcx_babel_forward(preproc_buf, babel_cap,
                                                  cctx->in_buf, cctx->in_pos);
            if (babel_out > 0) {
                preproc_in = preproc_buf;
                preproc_size = babel_out;
                preproc_size32 = (uint32_t)babel_out;
            } else {
                free(preproc_buf);
                preproc_buf = NULL;
            }
        }
    }

    size_t bsz = preproc_size > 0 ? preproc_size : 1;
    uint8_t* bbuf0 = (uint8_t*)malloc(bsz);
    uint8_t* bbuf1 = (uint8_t*)malloc(bsz);
    uint8_t* bbuf2 = (uint8_t*)malloc(bsz + (bsz / 4) + 1024);
    size_t entropy_cap = mcx_compress_bound(preproc_size);
    uint8_t* entropy_buf = (uint8_t*)malloc(entropy_cap);

    if (!bbuf0 || !bbuf1 || !bbuf2 || !entropy_buf) {
        free(bbuf0); free(bbuf1); free(bbuf2); free(entropy_buf); if (preproc_buf) free(preproc_buf);
        if (preproc_buf) free(preproc_buf);
        return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    }

    mcx_genome genome;
    if (strategy == MCX_STRATEGY_BABEL) {
        genome.use_bwt = 0;
        genome.use_mtf_rle = 0;
        genome.use_delta = 0;
        genome.entropy_coder = 1; /* rANS (order-0) */
        genome.cm_learning = 0;
    } else if (strategy == MCX_STRATEGY_STRIDE) {
        /* When stride detected: use BWT+CM-rANS (level 19) on delta data
         * When stride failed (no preproc): use DEFAULT (level 12) for best text results */
        int effective_level = (preproc_buf != NULL) ? 19 : 12;
        genome = mcx_evolve(preproc_in, preproc_size, effective_level);
    } else {
        genome = mcx_evolve(preproc_in, preproc_size, cctx->level);
    }
    cctx->out_buf[4] = mcx_encode_genome(&genome);
    size_t payload_offset = 1;

    /* Store preproc size for decoder (always write for BABEL/STRIDE, 0 = no preproc) */
    if (strategy == MCX_STRATEGY_BABEL || strategy == MCX_STRATEGY_STRIDE) {
        memcpy(cctx->out_buf + 4 + payload_offset, &preproc_size32, 4);
        payload_offset += 4;
    }

    const uint8_t* stage_in = preproc_in;
    size_t stage_size = preproc_size;

    if (genome.use_delta) {
        if (stage_in != bbuf0) memcpy(bbuf0, stage_in, stage_size);
        mcx_delta_encode(bbuf0, stage_size);
        stage_in = bbuf0;
    }

    if (genome.use_bwt) {
        size_t pidx;
        size_t bwt_res = mcx_bwt_forward(bbuf1, &pidx, stage_in, stage_size);
        if (MCX_IS_ERROR(bwt_res)) {
            free(bbuf0); free(bbuf1); free(bbuf2); free(entropy_buf); if (preproc_buf) free(preproc_buf);
            return bwt_res;
        }
        uint64_t p64 = (uint64_t)pidx;
        memcpy(cctx->out_buf + 4 + payload_offset, &p64, 8);
        payload_offset += 8;
        stage_in = bbuf1;
    }

    if (genome.use_mtf_rle) {
        if (stage_in != bbuf1) {
            memcpy(bbuf1, stage_in, stage_size);
            stage_in = bbuf1;
        }
        mcx_mtf_encode((uint8_t*)stage_in, stage_size);
        size_t rle_sz = mcx_rle_encode(bbuf2, bsz + (bsz / 4) + 1024, stage_in, stage_size);
        if (MCX_IS_ERROR(rle_sz)) {
            free(bbuf0); free(bbuf1); free(bbuf2); free(entropy_buf); if (preproc_buf) free(preproc_buf);
            return rle_sz;
        }
        uint32_t r32 = (uint32_t)rle_sz;
        memcpy(cctx->out_buf + 4 + payload_offset, &r32, 4);
        payload_offset += 4;
        stage_in = bbuf2;
        stage_size = rle_sz;
    }

    size_t csize;
    if (genome.entropy_coder == 2) {
        csize = mcx_cmrans_compress(entropy_buf, entropy_cap, stage_in, stage_size);
    } else if (genome.entropy_coder == 1) {
        csize = mcx_rans_compress(entropy_buf, entropy_cap, stage_in, stage_size);
    } else {
        csize = mcx_huffman_compress(entropy_buf, entropy_cap, stage_in, stage_size, NULL);
    }

    free(bbuf0); free(bbuf1); free(bbuf2);
    if (preproc_buf) free(preproc_buf);

    if (MCX_IS_ERROR(csize)) { free(entropy_buf); return csize; }

    size_t total_payload = payload_offset + csize;
    if (4 + total_payload > cctx->out_size) {
        free(entropy_buf);
        return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
    }
    memcpy(cctx->out_buf + 4 + payload_offset, entropy_buf, csize);
    free(entropy_buf);

    return total_payload;
}

mcx_cctx* mcx_create_cctx(void)
{
    mcx_cctx* cctx = (mcx_cctx*)malloc(sizeof(mcx_cctx));
    if (!cctx) return NULL;
    
    memset(cctx, 0, sizeof(mcx_cctx));
    cctx->state = MCX_CSTATE_INIT;
    cctx->in_buf = (uint8_t*)malloc(MCX_MAX_BLOCK_SIZE);
    
    /* Out buffer must be large enough to hold a worst-case compressed block plus headers */
    size_t out_cap = mcx_compress_bound(MCX_MAX_BLOCK_SIZE) + MCX_FRAME_HEADER_SIZE + 32;
    cctx->out_buf = (uint8_t*)malloc(out_cap);
    cctx->out_size = out_cap;
    
    if (!cctx->in_buf || !cctx->out_buf) {
        mcx_free_cctx(cctx);
        return NULL;
    }
    
    return cctx;
}

void mcx_free_cctx(mcx_cctx* cctx)
{
    if (cctx) {
        if (cctx->in_buf) free(cctx->in_buf);
        if (cctx->out_buf) free(cctx->out_buf);
        free(cctx);
    }
}

size_t mcx_compress_stream(mcx_cctx* cctx, mcx_out_buffer* output, mcx_in_buffer* input, int level)
{
    if (!cctx || !output) return MCX_ERROR(MCX_ERR_GENERIC);
    
    if (cctx->state == MCX_CSTATE_INIT) {
        cctx->level = level;
        mcx_strategy_t strategy;
        
        if (level <= 3) strategy = MCX_STRATEGY_LZ_FAST;
        else if (level <= 9) strategy = MCX_STRATEGY_LZ_HC;
        else if (level <= 14) strategy = MCX_STRATEGY_DEFAULT;
        else if (level <= 19) strategy = MCX_STRATEGY_BEST;
        else if (level <= 22) strategy = MCX_STRATEGY_BABEL;
        else strategy = MCX_STRATEGY_STRIDE;
        
        mcx_frame_header_t header;
        memset(&header, 0, sizeof(header));
        header.magic = MCX_MAGIC;
        header.version = MCX_FRAME_VERSION;
        header.flags = MCX_FLAG_STREAMING; /* Missing ORIGINAL_SIZE effectively implies stream */
        header.level = (uint8_t)level;
        header.strategy = (uint8_t)strategy;
        header.original_size = 0; /* Unknown in stream mode initially */
        
        write_frame_header(cctx->header, &header);
        
        memcpy(cctx->out_buf, cctx->header, MCX_FRAME_HEADER_SIZE);
        cctx->out_pos = MCX_FRAME_HEADER_SIZE;
        cctx->state = MCX_CSTATE_FLUSH_HEADER;
    }
    
    /* 1. Flush any pending output */
    if (cctx->out_pos > 0) {
        size_t available_dst = output->size - output->pos;
        size_t to_copy = MCX_MIN(cctx->out_pos, available_dst);
        
        if (to_copy > 0) {
            memcpy((uint8_t*)output->dst + output->pos, cctx->out_buf, to_copy);
            output->pos += to_copy;
            
            cctx->out_pos -= to_copy;
            if (cctx->out_pos > 0) {
                memmove(cctx->out_buf, cctx->out_buf + to_copy, cctx->out_pos);
                return 1; /* Needs more output space */
            }
        }
    }
    
    if (cctx->state == MCX_CSTATE_FLUSH_EOF && input->src == NULL && input->size == 0) {
        return 0; /* Fully finished */
    }
    
    /* 2. Process input / block compression */
    while (input && input->pos < input->size) {
        
        size_t available_in_buf = MCX_MAX_BLOCK_SIZE - cctx->in_pos;
        size_t to_copy_in = MCX_MIN(input->size - input->pos, available_in_buf);
        
        memcpy(cctx->in_buf + cctx->in_pos, (const uint8_t*)input->src + input->pos, to_copy_in);
        cctx->in_pos += to_copy_in;
        input->pos += to_copy_in;
        
        /* If our 1MB buffer is full, trigger a compression block */
        if (cctx->in_pos == MCX_MAX_BLOCK_SIZE || (input->src == NULL && cctx->in_pos > 0)) {
            
            /* The out_buf has enough padding for the 4-byte block size header */
            size_t comp_size = stream_compress_block(cctx);
            
            if (MCX_IS_ERROR(comp_size)) return comp_size;
            
            /* Write block header */
            uint32_t comp32 = (uint32_t)comp_size;
            memcpy(cctx->out_buf, &comp32, 4);
            cctx->out_pos = 4 + comp_size;
            
            /* Reset input */
            cctx->in_pos = 0;
            
            /* Flush this block before continuing */
            size_t available_dst = output->size - output->pos;
            size_t to_copy = MCX_MIN(cctx->out_pos, available_dst);
            if (to_copy > 0) {
                memcpy((uint8_t*)output->dst + output->pos, cctx->out_buf, to_copy);
                output->pos += to_copy;
                cctx->out_pos -= to_copy;
                if (cctx->out_pos > 0) {
                    memmove(cctx->out_buf, cctx->out_buf + to_copy, cctx->out_pos);
                    return 1;
                }
            }
        }
    }
    
    /* 3. Handling stream closure */
    if (input && input->src == NULL && input->size == 0 && cctx->state != MCX_CSTATE_FLUSH_EOF) {
        if (cctx->in_pos > 0) {
            /* The loop above will process the final dangling buffer on the next iteration natively if we just tell it to */
            /* Wait, there's no loop above if input->pos < input->size doesn't hit. Let's force encode here if dangling */
            
            size_t comp_size = stream_compress_block(cctx);
            
            uint32_t comp32 = (uint32_t)comp_size;
            memcpy(cctx->out_buf, &comp32, 4);
            cctx->out_pos = 4 + comp_size;
            cctx->in_pos = 0;
            
            /* Attempt immediate flush */
            size_t available_dst = output->size - output->pos;
            size_t to_copy = MCX_MIN(cctx->out_pos, available_dst);
            if (to_copy > 0) {
                memcpy((uint8_t*)output->dst + output->pos, cctx->out_buf, to_copy);
                output->pos += to_copy;
                cctx->out_pos -= to_copy;
                if (cctx->out_pos > 0) {
                    memmove(cctx->out_buf, cctx->out_buf + to_copy, cctx->out_pos);
                    /* Return needs more space before we write EOF */
                    return 1;
                }
            }
        }
        
        /* Write EOF block (size 0) */
        uint32_t eof = 0;
        memcpy(cctx->out_buf + cctx->out_pos, &eof, 4);
        cctx->out_pos += 4;
        cctx->state = MCX_CSTATE_FLUSH_EOF;
        
        /* Attempt EOF flush */
        size_t available_dst = output->size - output->pos;
        size_t to_copy = MCX_MIN(cctx->out_pos, available_dst);
        if (to_copy > 0) {
            memcpy((uint8_t*)output->dst + output->pos, cctx->out_buf, to_copy);
            output->pos += to_copy;
            cctx->out_pos -= to_copy;
            if (cctx->out_pos > 0) {
                memmove(cctx->out_buf, cctx->out_buf + to_copy, cctx->out_pos);
                return 1;
            }
        }
        
        return 0;
    }
    
    return 1; /* By default stream needs more calls */
}

mcx_dctx* mcx_create_dctx(void)
{
    mcx_dctx* dctx = (mcx_dctx*)malloc(sizeof(mcx_dctx));
    if (!dctx) return NULL;
    
    memset(dctx, 0, sizeof(mcx_dctx));
    dctx->state = MCX_DSTATE_INIT_HEADER;
    dctx->in_needed = MCX_FRAME_HEADER_SIZE;
    
    /* Out buffer caches decompressed chunks */
    dctx->out_buf = (uint8_t*)malloc(MCX_MAX_BLOCK_SIZE);
    dctx->out_size = MCX_MAX_BLOCK_SIZE;
    
    /* In buffer caches compressed chunks (can be slightly larger than 1MB) */
    size_t in_cap = mcx_compress_bound(MCX_MAX_BLOCK_SIZE) + MCX_FRAME_HEADER_SIZE + 32;
    dctx->in_buf = (uint8_t*)malloc(in_cap);
    
    if (!dctx->in_buf || !dctx->out_buf) {
        mcx_free_dctx(dctx);
        return NULL;
    }
    
    return dctx;
}

void mcx_free_dctx(mcx_dctx* dctx)
{
    if (dctx) {
        if (dctx->in_buf) free(dctx->in_buf);
        if (dctx->out_buf) free(dctx->out_buf);
        free(dctx);
    }
}

size_t mcx_decompress_stream(mcx_dctx* dctx, mcx_out_buffer* output, mcx_in_buffer* input)
{
    if (!dctx || !output || !input) return MCX_ERROR(MCX_ERR_GENERIC);
    
    /* 1. Flush any pending decompressed output */
    if (dctx->out_pos > 0) {
        size_t available_dst = output->size - output->pos;
        size_t to_copy = MCX_MIN(dctx->out_pos, available_dst);
        
        if (to_copy > 0) {
            memcpy((uint8_t*)output->dst + output->pos, dctx->out_buf, to_copy);
            output->pos += to_copy;
            
            dctx->out_pos -= to_copy;
            if (dctx->out_pos > 0) {
                memmove(dctx->out_buf, dctx->out_buf + to_copy, dctx->out_pos);
                return 1; /* Needs more output space */
            }
        }
    }
    
    if (dctx->state == MCX_DSTATE_FINISHED) {
        return 0;
    }
    
    /* 2. Process input into internal buffer to reach the byte boundary we currently need */
    size_t in_avail = input->size - input->pos;
    size_t missing = dctx->in_needed - dctx->in_pos;
    size_t to_read = MCX_MIN(in_avail, missing);
    
    if (to_read > 0) {
        memcpy(dctx->in_buf + dctx->in_pos, (const uint8_t*)input->src + input->pos, to_read);
        dctx->in_pos += to_read;
        input->pos += to_read;
    }
    
    /* Still missing data for current state transition */
    if (dctx->in_pos < dctx->in_needed) {
        return 1; 
    }
    
    /* 3. State transitions triggered by full internal buffer */
    if (dctx->state == MCX_DSTATE_INIT_HEADER) {
        mcx_frame_header_t header;
        if (read_frame_header(dctx->in_buf, dctx->in_pos, &header) != 0) {
            return MCX_ERROR(MCX_ERR_INVALID_MAGIC);
        }
        
        if (!(header.flags & MCX_FLAG_STREAMING)) {
            return MCX_ERROR(MCX_ERR_UNKNOWN_BLOCK); /* Streams can only read Streaming formatted tracks */
        }
        
        dctx->strategy = (mcx_strategy_t)header.strategy;
        
        dctx->in_pos = 0;
        dctx->in_needed = 4; /* Now we expect the first 4-byte chunk size */
        dctx->state = MCX_DSTATE_READ_BLOCK_HEADER;
        return 1; /* Request reading that chunk size */
    }
    
    if (dctx->state == MCX_DSTATE_READ_BLOCK_HEADER) {
        uint32_t block_size;
        memcpy(&block_size, dctx->in_buf, 4);
        
        if (block_size == 0) {
            dctx->state = MCX_DSTATE_FINISHED;
            return 0;
        }
        
        dctx->current_block_comp_size = block_size;
        dctx->in_pos = 0;
        dctx->in_needed = block_size;
        dctx->state = MCX_DSTATE_READ_BLOCK_PAYLOAD;
        return 1; /* Proceed to grabbing entire payload */
    }
    
    if (dctx->state == MCX_DSTATE_READ_BLOCK_PAYLOAD) {
        size_t orig_block_size = 0;
        
        if (dctx->strategy == MCX_STRATEGY_FAST) {
            /* Decoding huffman directly into internal out_buf max boundary */
            orig_block_size = mcx_huffman_decompress(dctx->out_buf, dctx->out_size, dctx->in_buf, dctx->in_pos);
            if (MCX_IS_ERROR(orig_block_size)) return orig_block_size;
        } else if (dctx->strategy == MCX_STRATEGY_LZ_FAST || dctx->strategy == MCX_STRATEGY_LZ_HC) {
            /* LZ blocks from streaming compress: first byte is block type magic */
            if (dctx->in_pos < 1) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            uint8_t block_type = dctx->in_buf[0];

            if (block_type == 0x00) {
                /* STORE fallback */
                orig_block_size = dctx->in_pos - 1;
                memcpy(dctx->out_buf, dctx->in_buf + 1, orig_block_size);
            } else if (block_type == 0xAA) {
                /* LZ77 + FSE */
                size_t lz_cap = mcx_lz_compress_bound(dctx->out_size);
                uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                if (!lz_buf) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);

                size_t lz_size = mcx_fse_decompress(lz_buf, lz_cap,
                                                      dctx->in_buf + 1, dctx->in_pos - 1);
                if (lz_size == 0) { free(lz_buf); return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); }

                orig_block_size = mcx_lz_decompress(dctx->out_buf, dctx->out_size,
                                                     lz_buf, lz_size, dctx->out_size);
                free(lz_buf);
                if (orig_block_size == 0) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            } else if (block_type == 0xA8) {
                /* LZ77 + rANS */
                size_t lz_cap = mcx_lz_compress_bound(dctx->out_size);
                uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                if (!lz_buf) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);

                size_t lz_size = mcx_rans_decompress(lz_buf, lz_cap,
                                                      dctx->in_buf + 1, dctx->in_pos - 1);
                if (MCX_IS_ERROR(lz_size)) { free(lz_buf); return lz_size; }

                orig_block_size = mcx_lz_decompress(dctx->out_buf, dctx->out_size,
                                                     lz_buf, lz_size, dctx->out_size);
                free(lz_buf);
                if (orig_block_size == 0) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            } else if (block_type == 0xAB) {
                /* LZ77 raw (no entropy coding) */
                orig_block_size = mcx_lz_decompress(dctx->out_buf, dctx->out_size,
                                                     dctx->in_buf + 1, dctx->in_pos - 1,
                                                     dctx->out_size);
                if (orig_block_size == 0) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            } else if (block_type == 0xAE) {
                /* LZ77 + Adaptive AC */
                size_t lz_cap = mcx_lz_compress_bound(dctx->out_size);
                uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                if (!lz_buf) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);

                size_t lz_size = mcx_adaptive_ac_decompress(lz_buf, lz_cap,
                                                             dctx->in_buf + 1, dctx->in_pos - 1);
                if (lz_size == 0) { free(lz_buf); return MCX_ERROR(MCX_ERR_SRC_CORRUPTED); }

                orig_block_size = mcx_lz_decompress(dctx->out_buf, dctx->out_size,
                                                     lz_buf, lz_size, dctx->out_size);
                free(lz_buf);
                if (orig_block_size == 0) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            } else {
                return MCX_ERROR(MCX_ERR_UNKNOWN_BLOCK);
            }
        } else if (dctx->strategy == MCX_STRATEGY_DEFAULT || dctx->strategy == MCX_STRATEGY_BEST ||
                   dctx->strategy == MCX_STRATEGY_BABEL || dctx->strategy == MCX_STRATEGY_STRIDE) {
            if (dctx->in_pos < 1) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            mcx_genome genome = mcx_decode_genome(dctx->in_buf[0]);
            size_t payload_offset = 1;

            /* Read preproc size if Babel/Stride */
            uint32_t preproc_size32 = 0;
            size_t preproc_intermediate_size = 0;
            if (dctx->strategy == MCX_STRATEGY_BABEL || dctx->strategy == MCX_STRATEGY_STRIDE) {
                if (payload_offset + 4 <= dctx->in_pos) {
                    memcpy(&preproc_size32, dctx->in_buf + payload_offset, 4);
                    preproc_intermediate_size = (size_t)preproc_size32;
                    payload_offset += 4;
                }
            }

            uint64_t pidx64 = 0;
            size_t pidx = 0;
            if (genome.use_bwt) {
                if (payload_offset + 8 > dctx->in_pos) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
                memcpy(&pidx64, dctx->in_buf + payload_offset, 8);
                pidx = (size_t)pidx64;
                payload_offset += 8;
            }

            uint32_t rle32 = 0;
            size_t rle_sz = MCX_MAX_BLOCK_SIZE;
            if (genome.use_mtf_rle) {
                if (payload_offset + 4 > dctx->in_pos) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
                memcpy(&rle32, dctx->in_buf + payload_offset, 4);
                rle_sz = (size_t)rle32;
                payload_offset += 4;
            }

            size_t target_size = (preproc_intermediate_size > 0) ? preproc_intermediate_size : MCX_MAX_BLOCK_SIZE;

            uint8_t* bbuf1 = (uint8_t*)malloc(rle_sz + 1024);
            uint8_t* bbuf2 = (uint8_t*)malloc(target_size + 1024);
            uint8_t* preproc_dec = (preproc_intermediate_size > 0) ? 
                                   (uint8_t*)malloc(preproc_intermediate_size + 1024) : NULL;
            if (!bbuf1 || !bbuf2) {
                if (bbuf1) free(bbuf1);
                if (bbuf2) free(bbuf2);
                if (preproc_dec) free(preproc_dec);
                return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
            }

            size_t dec_res;
            if (genome.entropy_coder == 2) {
                dec_res = mcx_cmrans_decompress(bbuf1, rle_sz + 1024, dctx->in_buf + payload_offset, dctx->in_pos - payload_offset);
            } else if (genome.entropy_coder == 1) {
                dec_res = mcx_rans_decompress(bbuf1, rle_sz + 1024, dctx->in_buf + payload_offset, dctx->in_pos - payload_offset);
            } else {
                dec_res = mcx_huffman_decompress(bbuf1, rle_sz + 1024, dctx->in_buf + payload_offset, dctx->in_pos - payload_offset);
            }

            if (MCX_IS_ERROR(dec_res)) { free(bbuf1); free(bbuf2); if (preproc_dec) free(preproc_dec); return dec_res; }

            uint8_t* stage_out = bbuf1;
            size_t stage_size = dec_res;

            if (genome.use_mtf_rle) {
                size_t rle_dec = mcx_rle_decode(bbuf2, target_size + 1024, stage_out, stage_size);
                if (MCX_IS_ERROR(rle_dec)) { free(bbuf1); free(bbuf2); if (preproc_dec) free(preproc_dec); return rle_dec; }
                mcx_mtf_decode(bbuf2, rle_dec);
                stage_out = bbuf2;
                stage_size = rle_dec;
            }

            /* BWT inverse into appropriate buffer */
            uint8_t* bwt_target = (preproc_dec) ? preproc_dec : dctx->out_buf;
            if (genome.use_bwt) {
                orig_block_size = mcx_bwt_inverse(bwt_target, pidx, stage_out, stage_size);
                if (MCX_IS_ERROR(orig_block_size)) { free(bbuf1); free(bbuf2); if (preproc_dec) free(preproc_dec); return orig_block_size; }
                stage_out = bwt_target;
                stage_size = orig_block_size;
            } else {
                memcpy(bwt_target, stage_out, stage_size);
                orig_block_size = stage_size;
                stage_out = bwt_target;
            }

            if (genome.use_delta) {
                mcx_delta_decode(stage_out, orig_block_size);
            }

            /* Preproc inverse (Babel/Stride) */
            if (preproc_dec && preproc_intermediate_size > 0) {
                if (dctx->strategy == MCX_STRATEGY_BABEL) {
                    orig_block_size = mcx_babel_inverse(dctx->out_buf, dctx->out_size,
                                                        stage_out, preproc_intermediate_size);
                } else if (dctx->strategy == MCX_STRATEGY_STRIDE) {
                    orig_block_size = mcx_babel_stride_inverse(dctx->out_buf, dctx->out_size,
                                                                stage_out, preproc_intermediate_size);
                }
            }

            free(bbuf1); free(bbuf2); if (preproc_dec) free(preproc_dec);
            
            if (MCX_IS_ERROR(orig_block_size)) return orig_block_size;
        } else {
            return MCX_ERROR(MCX_ERR_UNKNOWN_BLOCK);
        }
        
        dctx->out_pos = orig_block_size;
        
        /* Reset for next block header */
        dctx->in_pos = 0;
        dctx->in_needed = 4;
        dctx->state = MCX_DSTATE_READ_BLOCK_HEADER;
        
        /* Attempt immediate flush to free space */
        size_t available_dst = output->size - output->pos;
        size_t to_copy = MCX_MIN(dctx->out_pos, available_dst);
        
        if (to_copy > 0) {
            memcpy((uint8_t*)output->dst + output->pos, dctx->out_buf, to_copy);
            output->pos += to_copy;
            
            dctx->out_pos -= to_copy;
            if (dctx->out_pos > 0) {
                memmove(dctx->out_buf, dctx->out_buf + to_copy, dctx->out_pos);
            }
        }
        
        return 1; /* More blocks expected */
    }
    
    return MCX_ERROR(MCX_ERR_GENERIC);
}
