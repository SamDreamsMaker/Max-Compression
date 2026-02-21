/**
 * @file rle.c
 * @brief Run-Length Encoding — compress consecutive repeated bytes.
 *
 * Uses a simple escape-based scheme:
 *   - If a byte appears 3+ times in a row, emit: ESCAPE, byte, count-3
 *   - ESCAPE itself is escaped as: ESCAPE, ESCAPE
 *   - Everything else is literal
 *
 * This works well after BWT + MTF, where long runs of 0s appear.
 */

#include "preprocess.h"

#define RLE_ESCAPE  0xFF
#define RLE_MIN_RUN 3
#define RLE_MAX_RUN (RLE_MIN_RUN + 255)

size_t mcx_rle_encode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size)
{
    if (dst == NULL || src == NULL) return MCX_ERROR(MCX_ERR_GENERIC);

    size_t di = 0;

    for (size_t si = 0; si < src_size; ) {
        uint8_t byte = src[si];

        /* Count run length */
        size_t run = 1;
        while (si + run < src_size && src[si + run] == byte && run < RLE_MAX_RUN) {
            run++;
        }

        if (run >= RLE_MIN_RUN) {
            /* Encode run: ESCAPE, byte, (run - RLE_MIN_RUN) */
            if (di + 3 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
            dst[di++] = RLE_ESCAPE;
            dst[di++] = byte;
            dst[di++] = (uint8_t)(run - RLE_MIN_RUN);
            si += run;
        } else {
            /* Emit literals */
            for (size_t r = 0; r < run; r++) {
                if (byte == RLE_ESCAPE) {
                    if (di + 2 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
                    dst[di++] = RLE_ESCAPE;
                    dst[di++] = RLE_ESCAPE;
                } else {
                    if (di + 1 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
                    dst[di++] = byte;
                }
            }
            si += run;
        }
    }

    return di;
}

size_t mcx_rle_decode(uint8_t* dst, size_t dst_cap,
                      const uint8_t* src, size_t src_size)
{
    if (dst == NULL || src == NULL) return MCX_ERROR(MCX_ERR_GENERIC);

    size_t di = 0;

    for (size_t si = 0; si < src_size; ) {
        if (src[si] == RLE_ESCAPE) {
            si++;
            if (si >= src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);

            if (src[si] == RLE_ESCAPE) {
                /* Escaped escape = literal 0xFF */
                if (di + 1 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
                dst[di++] = RLE_ESCAPE;
                si++;
            } else {
                /* Run: byte, count */
                uint8_t byte = src[si++];
                if (si >= src_size) return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
                size_t run = (size_t)src[si++] + RLE_MIN_RUN;

                if (di + run > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
                memset(dst + di, byte, run);
                di += run;
            }
        } else {
            if (di + 1 > dst_cap) return MCX_ERROR(MCX_ERR_DST_TOO_SMALL);
            dst[di++] = src[si++];
        }
    }

    return di;
}
