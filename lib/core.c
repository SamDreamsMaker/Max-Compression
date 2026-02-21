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
#include <stdio.h>
#ifdef _OPENMP
#include <omp.h>
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
    return "0.1.0";
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

    /* ── Choose strategy based on analysis and level ── */
    if (analysis.type == MCX_DTYPE_HIGH_ENTROPY) {
        strategy = MCX_STRATEGY_STORE;
    } else if (level <= 3) {
        strategy = MCX_STRATEGY_LZ_FAST; /* v1.0: Fast LZ77 + Huffman */
    } else if (level <= 9) {
        strategy = MCX_STRATEGY_LZ_HC;   /* v1.0: Lazy LZ77 + Huffman */
    } else if (level <= 14) {
        strategy = MCX_STRATEGY_DEFAULT; /* v0.2: BWT + MTF + RLE + ANS */
    } else {
        strategy = MCX_STRATEGY_BEST;    /* v0.2: BWT + MTF + RLE + CM-rANS */
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
    if (src_size > 0 && strategy != MCX_STRATEGY_STORE && strategy != MCX_STRATEGY_FAST) {
        num_blocks = (uint32_t)((src_size + MCX_MAX_BLOCK_SIZE - 1) / MCX_MAX_BLOCK_SIZE);
    }
    size_t block_sizes_offset = 0;
    if (strategy != MCX_STRATEGY_STORE && strategy != MCX_STRATEGY_FAST) {
        if (offset + 4 + num_blocks * 4 > dst_cap) {
            return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
        }
        
        /* Write number of blocks */
        memcpy(out + offset, &num_blocks, 4);
        offset += 4;
        
        /* Reserve space for the array of N compressed block sizes */
        block_sizes_offset = offset;
        offset += num_blocks * 4;
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
        #pragma omp parallel for schedule(dynamic)
        for (b = 0; b < num_blocks; b++) {
            if (omp_err) continue;
            
            size_t src_offset = (size_t)b * MCX_MAX_BLOCK_SIZE;
            size_t block_src_size = src_size - src_offset;
            if (block_src_size > MCX_MAX_BLOCK_SIZE) {
                block_src_size = MCX_MAX_BLOCK_SIZE;
            }

            if (strategy == MCX_STRATEGY_LZ_FAST || strategy == MCX_STRATEGY_LZ_HC) {
                size_t lz_cap = mcx_lz_compress_bound(block_src_size);
                uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                size_t fse_cap = mcx_compress_bound(lz_cap);
                uint8_t* fse_buf = (uint8_t*)malloc(fse_cap + 1); /* +1 for block type flag */
                
                if (!lz_buf || !fse_buf) {
                    #pragma omp critical
                    { omp_err = 1; }
                    if (lz_buf) free(lz_buf); if (fse_buf) free(fse_buf);
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

                /* Entropy Pass */
                fse_buf[0] = 0xAA; /* Magic byte for LZ77+FSE block type */
                size_t fse_size = mcx_fse_compress(fse_buf + 1, fse_cap, lz_buf, lz_size);
                free(lz_buf);

                if (fse_size == 0 || fse_size >= block_src_size) {
                    /* Incompressible block fallback */
                    fse_buf[0] = 0x00; /* Magic byte for STORE block type */
                    memcpy(fse_buf + 1, in + src_offset, block_src_size);
                    fse_size = block_src_size;
                }

                block_sizes[b] = fse_size + 1;
                block_buffers[b] = fse_buf;
            } else {
                /* BWT Path (DEFAULT or BEST) */
                uint8_t* buf0 = (uint8_t*)malloc(block_src_size);
                uint8_t* buf1 = (uint8_t*)malloc(block_src_size);
                uint8_t* buf2 = (uint8_t*)malloc(block_src_size + (block_src_size / 4) + 1024);
                size_t max_out = mcx_compress_bound(block_src_size);
                uint8_t* out1 = (uint8_t*)malloc(max_out);

                if (!buf0 || !buf1 || !buf2 || !out1) {
                    #pragma omp critical
                    { omp_err = 1; }
                    if (buf0) free(buf0); if (buf1) free(buf1); 
                    if (buf2) free(buf2); if (out1) free(out1);
                    continue;
                }

                mcx_genome genome = mcx_evolve(in + src_offset, block_src_size, level);
            out1[0] = mcx_encode_genome(&genome);
            size_t payload_offset = 1;
            
            const uint8_t* stage_in = in + src_offset;
            size_t stage_size = block_src_size;
            
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
                    free(buf0); free(buf1); free(buf2); free(out1);
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
                size_t rle_size = mcx_rle_encode(buf2, rle_cap, stage_in, stage_size);
                if (MCX_IS_ERROR(rle_size)) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf0); free(buf1); free(buf2); free(out1);
                    continue;
                }
                stage_in = buf2;
                stage_size = rle_size;
                rle32 = (uint32_t)rle_size;
                memcpy(out1 + payload_offset, &rle32, 4);
                payload_offset += 4;
            }

            size_t entropy_size;
            if (genome.entropy_coder == 2) {
                entropy_size = mcx_cmrans_compress(out1 + payload_offset, max_out - payload_offset, stage_in, stage_size);
            } else if (genome.entropy_coder == 1) {
                entropy_size = mcx_rans_compress(out1 + payload_offset, max_out - payload_offset, stage_in, stage_size);
            } else {
                entropy_size = mcx_huffman_compress(out1 + payload_offset, max_out - payload_offset, stage_in, stage_size, NULL);
            }
            
            if (MCX_IS_ERROR(entropy_size)) {
                #pragma omp critical
                { omp_err = 1; }
                free(buf0); free(buf1); free(buf2); free(out1);
                continue;
            }

            block_buffers[b] = out1;
            block_sizes[b] = payload_offset + entropy_size;

            free(buf0); free(buf1); free(buf2);
        }
        } /* end BWT path block */

        if (omp_err) {
            for (uint32_t b = 0; b < num_blocks; b++) {
                if (block_buffers[b]) free(block_buffers[b]);
            }
            free(block_buffers); free(block_sizes);
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
    }

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

        /* Calculate absolute offset boundaries to decouple thread reads */
        size_t* block_offsets = (size_t*)malloc(num_blocks * sizeof(size_t));
        if (!block_offsets) {
            free(block_sizes);
            return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
        }
        
        size_t current_offset = offset;
        for (uint32_t b = 0; b < num_blocks; b++) {
            block_offsets[b] = current_offset;
            current_offset += block_sizes[b];
        }
        
        if (current_offset > src_size) {
            free(block_sizes); free(block_offsets);
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }

        int omp_err = 0;
        
        int32_t b;
        #pragma omp parallel for schedule(dynamic)
        for (b = 0; b < num_blocks; b++) {
            if (omp_err) continue;

            size_t dst_offset = (size_t)b * MCX_MAX_BLOCK_SIZE;
            size_t block_dst_size = orig_size - dst_offset;
            if (block_dst_size > MCX_MAX_BLOCK_SIZE) {
                block_dst_size = MCX_MAX_BLOCK_SIZE;
            }

            size_t chunk_src_offset = block_offsets[b];
            size_t chunk_comp_size  = block_sizes[b];
            
            if (chunk_comp_size < 1) { /* genome byte / magic byte */
                #pragma omp critical
                { omp_err = 1; }
                continue;
            }

            if (strategy == MCX_STRATEGY_LZ_FAST || strategy == MCX_STRATEGY_LZ_HC) {
                uint8_t block_type = in[chunk_src_offset];
                
                if (block_type == 0x00) {
                    /* STORE fallback */
                    if (chunk_comp_size - 1 != block_dst_size) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }
                    memcpy(out + dst_offset, in + chunk_src_offset + 1, block_dst_size);
                } else if (block_type == 0xAA) {
                    /* LZ77 + FSE */
                    size_t lz_cap = mcx_lz_compress_bound(block_dst_size);
                    uint8_t* lz_buf = (uint8_t*)malloc(lz_cap);
                    if (!lz_buf) {
                        #pragma omp critical
                        { omp_err = 1; }
                        continue;
                    }

                    size_t lz_size = mcx_fse_decompress(lz_buf, lz_cap, 
                                            in + chunk_src_offset + 1, chunk_comp_size - 1);
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
                } else {
                    #pragma omp critical
                    { omp_err = 1; }
                    continue;
                }
            } else {
                /* BWT Path */
                mcx_genome genome = mcx_decode_genome(in[chunk_src_offset]);
            size_t payload_offset = 1;

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
            size_t rle_size = block_dst_size; /* Default if no BWT/MTF/RLE */
            if (genome.use_mtf_rle) {
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
            uint8_t* buf2 = (uint8_t*)malloc(block_dst_size + 1024);
            
            if (!buf1 || !buf2) {
                #pragma omp critical
                { omp_err = 1; }
                if (buf1) free(buf1); 
                if (buf2) free(buf2);
                continue;
            }

            size_t dec_res;
            if (genome.entropy_coder == 2) {
                dec_res = mcx_cmrans_decompress(
                    buf1, rle_size + 1024, in + chunk_src_offset + payload_offset, chunk_comp_size - payload_offset);
            } else if (genome.entropy_coder == 1) {
                dec_res = mcx_rans_decompress(
                    buf1, rle_size + 1024, in + chunk_src_offset + payload_offset, chunk_comp_size - payload_offset);
            } else {
                dec_res = mcx_huffman_decompress(
                    buf1, rle_size + 1024, in + chunk_src_offset + payload_offset, chunk_comp_size - payload_offset);
            }

            if (MCX_IS_ERROR(dec_res)) {
                #pragma omp critical
                { omp_err = 1; }
                free(buf1); free(buf2);
                continue;
            }

            uint8_t* stage_out = buf1;
            size_t stage_size = dec_res;

            if (genome.use_mtf_rle) {
                size_t rle_dec = mcx_rle_decode(buf2, block_dst_size + 1024, stage_out, stage_size);
                if (MCX_IS_ERROR(rle_dec)) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2);
                    continue;
                }
                mcx_mtf_decode(buf2, rle_dec);
                stage_out = buf2;
                stage_size = rle_dec;
            }

            if (genome.use_bwt) {
                size_t bwt_dec = mcx_bwt_inverse(out + dst_offset, primary_idx, stage_out, stage_size);
                if (MCX_IS_ERROR(bwt_dec) || bwt_dec != block_dst_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2);
                    continue;
                }
                stage_out = out + dst_offset;
            } else {
                if (stage_size != block_dst_size) {
                    #pragma omp critical
                    { omp_err = 1; }
                    free(buf1); free(buf2);
                    continue;
                }
                memcpy(out + dst_offset, stage_out, stage_size);
                stage_out = out + dst_offset;
            }

            if (genome.use_delta) {
                mcx_delta_decode(stage_out, block_dst_size);
            }

            free(buf1); free(buf2);
            } /* end BWT path block */
        } /* end parallel loops */

        free(block_sizes);
        free(block_offsets);

        if (omp_err) {
            return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
        }

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
            if (lz_buf) free(lz_buf); if (fse_buf) free(fse_buf);
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
    } else if (!(strategy == MCX_STRATEGY_DEFAULT || strategy == MCX_STRATEGY_BEST)) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }

    size_t bsz = cctx->in_pos > 0 ? cctx->in_pos : 1;
    uint8_t* bbuf0 = (uint8_t*)malloc(bsz);
    uint8_t* bbuf1 = (uint8_t*)malloc(bsz);
    uint8_t* bbuf2 = (uint8_t*)malloc(bsz + (bsz / 4) + 1024);
    size_t entropy_cap = mcx_compress_bound(cctx->in_pos);
    uint8_t* entropy_buf = (uint8_t*)malloc(entropy_cap);

    if (!bbuf0 || !bbuf1 || !bbuf2 || !entropy_buf) {
        free(bbuf0); free(bbuf1); free(bbuf2); free(entropy_buf);
        return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    }

    mcx_genome genome = mcx_evolve(cctx->in_buf, cctx->in_pos, cctx->level);
    cctx->out_buf[4] = mcx_encode_genome(&genome);
    size_t payload_offset = 1;

    const uint8_t* stage_in = cctx->in_buf;
    size_t stage_size = cctx->in_pos;

    if (genome.use_delta) {
        if (stage_in != bbuf0) memcpy(bbuf0, stage_in, stage_size);
        mcx_delta_encode(bbuf0, stage_size);
        stage_in = bbuf0;
    }

    if (genome.use_bwt) {
        size_t pidx;
        size_t bwt_res = mcx_bwt_forward(bbuf1, &pidx, stage_in, stage_size);
        if (MCX_IS_ERROR(bwt_res)) {
            free(bbuf0); free(bbuf1); free(bbuf2); free(entropy_buf);
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
            free(bbuf0); free(bbuf1); free(bbuf2); free(entropy_buf);
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
        else strategy = MCX_STRATEGY_BEST;
        
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
        } else if (dctx->strategy == MCX_STRATEGY_DEFAULT || dctx->strategy == MCX_STRATEGY_BEST) {
            if (dctx->in_pos < 1) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
            mcx_genome genome = mcx_decode_genome(dctx->in_buf[0]);
            size_t payload_offset = 1;

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

            uint8_t* bbuf1 = (uint8_t*)malloc(rle_sz + 1024);
            uint8_t* bbuf2 = (uint8_t*)malloc(MCX_MAX_BLOCK_SIZE + 1024);
            if (!bbuf1 || !bbuf2) {
                if (bbuf1) free(bbuf1);
                if (bbuf2) free(bbuf2);
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

            if (MCX_IS_ERROR(dec_res)) { free(bbuf1); free(bbuf2); return dec_res; }

            uint8_t* stage_out = bbuf1;
            size_t stage_size = dec_res;

            if (genome.use_mtf_rle) {
                size_t rle_dec = mcx_rle_decode(bbuf2, MCX_MAX_BLOCK_SIZE + 1024, stage_out, stage_size);
                if (MCX_IS_ERROR(rle_dec)) { free(bbuf1); free(bbuf2); return rle_dec; }
                mcx_mtf_decode(bbuf2, rle_dec);
                stage_out = bbuf2;
                stage_size = rle_dec;
            }

            if (genome.use_bwt) {
                orig_block_size = mcx_bwt_inverse(dctx->out_buf, pidx, stage_out, stage_size);
                if (MCX_IS_ERROR(orig_block_size)) { free(bbuf1); free(bbuf2); return orig_block_size; }
                stage_out = dctx->out_buf;
                stage_size = orig_block_size; /* Inverse sets properly */
            } else {
                memcpy(dctx->out_buf, stage_out, stage_size);
                orig_block_size = stage_size;
                stage_out = dctx->out_buf;
            }

            if (genome.use_delta) {
                mcx_delta_decode(stage_out, orig_block_size);
            }

            free(bbuf1); free(bbuf2);
            
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
