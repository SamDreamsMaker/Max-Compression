/**
 * probe12_stage.c — Identifies which decompression stage fails for kennedy.xls blocks 0 and 5.
 * Reads the compressed output and manually parses the genome byte.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/maxcomp/maxcomp.h"
#include "lib/optimizer/genetic.h"
#include "lib/internal.h"

#define BLOCK_SIZE (131072)

static void probe_block(const uint8_t* src, size_t bsz, int block_idx)
{
    size_t cap = mcx_compress_bound(bsz);
    uint8_t* comp = (uint8_t*)malloc(cap);
    size_t cs = mcx_compress(comp, cap, src, bsz, 12);
    if (mcx_is_error(cs)) {
        printf("  Block %d: COMPRESS ERROR\n", block_idx);
        free(comp); return;
    }
    printf("  Block %d: cs=%zu\n", block_idx, cs);

    /* Parse the MCX frame manually */
    /* Frame header: 20 bytes */
    /* num_blocks: 4 bytes */
    /* block_sizes: 4 bytes each */
    size_t hdr = 20;
    uint32_t num_blocks; memcpy(&num_blocks, comp + hdr, 4); hdr += 4;
    uint32_t bsize32;    memcpy(&bsize32, comp + hdr, 4);    hdr += 4;
    printf("    num_blocks=%u  bsize=%u\n", num_blocks, bsize32);

    /* Block data starts at hdr */
    const uint8_t* bdata = comp + hdr;
    size_t bdata_size = (size_t)bsize32;

    /* genome byte */
    mcx_genome genome = mcx_decode_genome(bdata[0]);
    printf("    genome: use_bwt=%d use_mtf_rle=%d entropy=%d use_delta=%d\n",
           genome.use_bwt, genome.use_mtf_rle, genome.entropy_coder, genome.use_delta);

    size_t po = 1;

    uint64_t pidx64 = 0;
    if (genome.use_bwt) {
        memcpy(&pidx64, bdata + po, 8); po += 8;
        printf("    primary_idx=%llu\n", (unsigned long long)pidx64);
    }

    uint32_t rle32 = 0;
    if (genome.use_mtf_rle) {
        memcpy(&rle32, bdata + po, 4); po += 4;
        printf("    rle_size=%u\n", rle32);
    }

    size_t entropy_payload_size = bdata_size - po;
    printf("    entropy payload: %zu bytes\n", entropy_payload_size);

    /* Try rANS header check */
    if (genome.entropy_coder == 1) { /* rANS */
        size_t hdr_size = 4 + 256*2 + 8;
        if (entropy_payload_size < hdr_size) {
            printf("    rANS: payload too small (%zu < %zu)\n", entropy_payload_size, hdr_size);
        } else {
            uint32_t rans_orig; memcpy(&rans_orig, bdata + po, 4);
            printf("    rANS orig_size=%u\n", rans_orig);
            /* Check freq sum */
            uint16_t freq[256];
            memcpy(freq, bdata + po + 4, 256*2);
            uint32_t cum = 0;
            for (int i = 0; i < 256; i++) cum += freq[i];
            printf("    rANS freq_sum=%u (expected 4096=%d, match=%d)\n",
                   cum, 4096, cum==4096);
        }
    }

    /* Attempt decompression and report */
    uint8_t* dec = (uint8_t*)malloc(bsz + 64);
    size_t ds = mcx_decompress(dec, bsz + 64, comp, cs);
    if (mcx_is_error(ds)) {
        printf("    DECOMP ERROR code=%zu\n", ds & ~((size_t)1<<63));
    } else {
        printf("    DECOMP OK  ds=%zu  match=%d\n", ds,
               (int)(ds == bsz && memcmp(src, dec, bsz) == 0));
    }

    free(comp); free(dec);
}

int main(void)
{
    const char* path = "benchmarks/corpus/canterbury/kennedy.xls";
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    rewind(f);
    uint8_t* src = (uint8_t*)malloc(n);
    fread(src, 1, n, f);
    fclose(f);

    printf("=== kennedy.xls block stage probe ===\n\n");
    /* Test block 0 and block 5 (the failing ones) */
    probe_block(src + 0,      BLOCK_SIZE, 0);
    printf("\n");
    probe_block(src + 5*BLOCK_SIZE, BLOCK_SIZE, 5);

    free(src);
    return 0;
}
