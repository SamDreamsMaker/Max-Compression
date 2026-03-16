/**
 * @file rle2.c
 * @brief Enhanced RLE with exponential zero-run encoding (RUNA/RUNB style).
 *
 * After BWT+MTF, the output is dominated by zeros (60-80% typically).
 * Standard RLE encodes a run of N zeros as [0, N] — linear.
 * RUNA/RUNB encodes zero runs using binary numbering:
 *
 *   run=1: RUNA (symbol 0)
 *   run=2: RUNB (symbol 1)
 *   run=3: RUNA RUNA
 *   run=4: RUNB RUNA
 *   run=5: RUNA RUNB
 *   ...
 *
 * Each RUNA/RUNB adds a bit to the run length in a bijective base-2 system:
 *   runlength = sum of (symbol+1) * 2^position
 *
 * This is exponentially more compact: a run of 1000 zeros takes ~10 symbols
 * instead of ~4 symbols with byte-count RLE.
 *
 * Non-zero bytes are shifted: byte N > 0 becomes N+1 in the output.
 * (0 and 1 are reserved for RUNA/RUNB)
 *
 * Format: [4: original_size LE] [4: mtf_size LE] [encoded data]
 * mtf_size = size before RLE2 (needed to allocate decode buffer)
 */

#include "../internal.h"
#include <string.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════════════════════════════
 *  RLE2 Encode (RUNA/RUNB zero-run encoding)
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_rle2_encode(uint8_t* dst, size_t dst_cap,
                        const uint8_t* src, size_t src_size)
{
    if (!dst || !src || src_size == 0) return 0;
    
    /* No internal header — sizes are managed by the calling pipeline.
     * First 4 bytes: original (pre-RLE2) size for the decoder. */
    if (dst_cap < 4) return 0;
    
    uint32_t orig32 = (uint32_t)src_size;
    memcpy(dst, &orig32, 4);
    
    size_t op = 4;
    size_t ip = 0;
    
    while (ip < src_size) {
        if (src[ip] == 0) {
            /* Count zero run */
            size_t run = 0;
            while (ip < src_size && src[ip] == 0) {
                run++;
                ip++;
            }
            
            /* Encode run using bijective base-2 (RUNA=0, RUNB=1):
             * run = sum of (bit+1) * 2^pos, where bit is RUNA(0) or RUNB(1)
             * Encoding: while run > 0:
             *   if (run & 1) == 1: emit RUNA, run = (run-1)/2
             *   if (run & 1) == 0: emit RUNB, run = (run-2)/2 */
            while (run > 0) {
                if (op >= dst_cap) return 0;
                if (run % 2 == 1) {
                    dst[op++] = 0; /* RUNA */
                    run = (run - 1) / 2;
                } else {
                    dst[op++] = 1; /* RUNB */
                    run = (run - 2) / 2;
                }
            }
        } else {
            /* Non-zero byte: shift by +1 to avoid conflict with RUNA/RUNB.
             * Byte 255 would overflow, so use 2-byte escape: [255][orig_val] */
            if (src[ip] >= 254) {
                if (op + 2 > dst_cap) return 0;
                dst[op++] = 255; /* escape marker */
                dst[op++] = src[ip]; /* original value */
            } else {
                if (op >= dst_cap) return 0;
                dst[op++] = src[ip] + 1; /* shifted: 1→2, 2→3, ..., 253→254 */
            }
            ip++;
        }
    }
    
    return op;
}

/* ═══════════════════════════════════════════════════════════════════
 *  RLE2 Decode
 * ═══════════════════════════════════════════════════════════════════ */

size_t mcx_rle2_decode(uint8_t* dst, size_t dst_cap,
                        const uint8_t* src, size_t src_size)
{
    if (!dst || !src || src_size < 4) return 0;
    
    uint32_t orig32;
    memcpy(&orig32, src, 4);
    size_t orig_size = (size_t)orig32;
    
    if (orig_size > dst_cap) return 0;
    
    size_t op = 0;
    size_t ip = 4; /* skip size header */
    
    while (ip < src_size && op < orig_size) {
        if (src[ip] <= 1) {
            /* Decode RUNA/RUNB zero run */
            size_t run = 0;
            size_t power = 1;
            
            while (ip < src_size && src[ip] <= 1) {
                run += (src[ip] + 1) * power;
                power *= 2;
                ip++;
            }
            
            /* Emit 'run' zeros */
            if (op + run > orig_size) return 0;
            memset(dst + op, 0, run);
            op += run;
        } else if (src[ip] == 255) {
            /* Escape: next byte is the original value (254 or 255) */
            ip++;
            if (ip >= src_size || op >= orig_size) return 0;
            dst[op++] = src[ip];
            ip++;
        } else {
            /* Shifted byte: reverse the +1 shift */
            dst[op++] = src[ip] - 1;
            ip++;
        }
    }
    
    return op;
}
