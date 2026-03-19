/**
 * @file test_streaming_decompress.c
 * @brief Test streaming decompression: compress via streaming API, then
 *        decompress in small chunks, verify output matches original.
 *
 * NOTE: The streaming decompress API only works with data compressed via
 * the streaming compress API (MCX_FLAG_STREAMING). One-shot compressed
 * data uses a different block format and must be decompressed one-shot.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>

/**
 * Compress data using the streaming API (produces streaming-format output).
 */
static int stream_compress(const uint8_t* src, size_t src_size, int level,
                            uint8_t** out_data, size_t* out_size)
{
    mcx_cctx* cctx = mcx_create_cctx();
    if (!cctx) return -1;

    size_t out_cap = mcx_compress_bound(src_size) + 4096;
    uint8_t* out = (uint8_t*)malloc(out_cap);
    uint8_t* in_chunk = (uint8_t*)malloc(4096);
    uint8_t* out_chunk = (uint8_t*)malloc(out_cap);
    if (!out || !in_chunk || !out_chunk) {
        free(out); free(in_chunk); free(out_chunk);
        mcx_free_cctx(cctx);
        return -1;
    }

    size_t total_out = 0;
    size_t in_pos = 0;
    size_t result = 1;

    while (result != 0) {
        mcx_in_buffer in_b;
        size_t avail = src_size - in_pos;
        if (avail > 0) {
            size_t chunk = avail > 4096 ? 4096 : avail;
            memcpy(in_chunk, src + in_pos, chunk);
            in_b.src = in_chunk;
            in_b.size = chunk;
            in_b.pos = 0;
        } else {
            in_b.src = NULL;
            in_b.size = 0;
            in_b.pos = 0;
        }

        mcx_out_buffer out_b = { out_chunk, out_cap, 0 };

        result = mcx_compress_stream(cctx, &out_b, &in_b, level);

        if (mcx_is_error(result)) {
            free(out); free(in_chunk); free(out_chunk);
            mcx_free_cctx(cctx);
            return -1;
        }

        if (out_b.pos > 0) {
            if (total_out + out_b.pos > out_cap) {
                free(out); free(in_chunk); free(out_chunk);
                mcx_free_cctx(cctx);
                return -1;
            }
            memcpy(out + total_out, out_chunk, out_b.pos);
            total_out += out_b.pos;
        }

        in_pos += in_b.pos;
    }

    mcx_free_cctx(cctx);
    free(in_chunk);
    free(out_chunk);

    *out_data = out;
    *out_size = total_out;
    return 0;
}

/**
 * Decompress streaming data in small chunks.
 */
static int stream_decompress(const uint8_t* comp, size_t comp_size,
                              size_t feed_chunk,
                              uint8_t** out_data, size_t* out_size,
                              size_t max_out)
{
    mcx_dctx* dctx = mcx_create_dctx();
    if (!dctx) return -1;

    size_t out_buf_cap = 4096;
    uint8_t* out_buf = (uint8_t*)malloc(out_buf_cap);
    uint8_t* full_out = (uint8_t*)malloc(max_out);
    if (!out_buf || !full_out) {
        free(out_buf); free(full_out);
        mcx_free_dctx(dctx);
        return -1;
    }

    size_t total_out = 0;
    size_t in_pos = 0;
    int done = 0;

    while (!done) {
        size_t avail = comp_size - in_pos;
        if (avail > feed_chunk) avail = feed_chunk;

        mcx_in_buffer in_b;
        if (avail > 0) {
            in_b.src = comp + in_pos;
            in_b.size = avail;
            in_b.pos = 0;
        } else {
            in_b.src = NULL;
            in_b.size = 0;
            in_b.pos = 0;
        }

        mcx_out_buffer out_b = { out_buf, out_buf_cap, 0 };

        size_t r = mcx_decompress_stream(dctx, &out_b, &in_b);

        if (mcx_is_error(r)) {
            fprintf(stderr, "  stream decompress error: %s (in_pos=%zu/%zu)\n",
                    mcx_get_error_name(r), in_pos, comp_size);
            free(out_buf); free(full_out);
            mcx_free_dctx(dctx);
            return -1;
        }

        if (out_b.pos > 0) {
            if (total_out + out_b.pos > max_out) {
                fprintf(stderr, "  stream output overflow\n");
                free(out_buf); free(full_out);
                mcx_free_dctx(dctx);
                return -1;
            }
            memcpy(full_out + total_out, out_buf, out_b.pos);
            total_out += out_b.pos;
        }

        in_pos += in_b.pos;
        if (r == 0) done = 1;
    }

    mcx_free_dctx(dctx);
    free(out_buf);

    *out_data = full_out;
    *out_size = total_out;
    return 0;
}

static int test_streaming_roundtrip(const uint8_t* src, size_t src_size,
                                     int level, size_t feed_chunk)
{
    uint8_t* comp = NULL;
    size_t comp_size = 0;

    /* Compress via streaming API */
    if (stream_compress(src, src_size, level, &comp, &comp_size) != 0) {
        fprintf(stderr, "  streaming compress failed\n");
        return 1;
    }

    /* Decompress via streaming API in small chunks */
    uint8_t* dec = NULL;
    size_t dec_size = 0;
    if (stream_decompress(comp, comp_size, feed_chunk, &dec, &dec_size,
                           src_size + 1024) != 0) {
        free(comp);
        return 1;
    }
    free(comp);

    /* Verify */
    int ok = (dec_size == src_size && memcmp(dec, src, src_size) == 0);
    free(dec);

    if (!ok) {
        fprintf(stderr, "  MISMATCH: original=%zu, decompressed=%zu\n",
                src_size, dec_size);
        return 1;
    }

    return 0;
}

int main(void)
{
    int pass = 0, fail = 0;

    /* Test data: repeated text */
    const char* text = "The quick brown fox jumps over the lazy dog. ";
    size_t tlen = strlen(text);

    /* Streaming API uses block-level chunking — test various sizes */
    size_t sizes[] = { 100, 1024, 8192, 65536 };
    /* Streaming compress only uses level for block strategy selection;
     * levels 1-9 use LZ/Huffman path in streaming mode */
    int levels[] = { 1, 3, 6 };
    size_t chunks[] = { 1, 7, 64, 256, 2048 };

    for (int si = 0; si < (int)(sizeof(sizes)/sizeof(sizes[0])); si++) {
        size_t sz = sizes[si];
        uint8_t* data = (uint8_t*)malloc(sz);
        if (!data) continue;
        for (size_t i = 0; i < sz; i++) data[i] = text[i % tlen];

        for (int li = 0; li < (int)(sizeof(levels)/sizeof(levels[0])); li++) {
            for (int ci = 0; ci < (int)(sizeof(chunks)/sizeof(chunks[0])); ci++) {
                int r = test_streaming_roundtrip(data, sz, levels[li], chunks[ci]);
                if (r == 0) {
                    pass++;
                } else {
                    fprintf(stderr, "FAIL: size=%zu level=%d chunk=%zu\n",
                            sz, levels[li], chunks[ci]);
                    fail++;
                }
            }
        }
        free(data);
    }

    /* Random data test */
    {
        size_t sz = 4096;
        uint8_t* rdata = (uint8_t*)malloc(sz);
        if (rdata) {
            uint32_t seed = 0xDEADBEEF;
            for (size_t i = 0; i < sz; i++) {
                seed = seed * 1103515245 + 12345;
                rdata[i] = (uint8_t)(seed >> 16);
            }
            int r = test_streaming_roundtrip(rdata, sz, 3, 37);
            if (r == 0) pass++; else { fail++; fprintf(stderr, "FAIL: random L3\n"); }
            free(rdata);
        }
    }

    printf("Streaming decompress: %d/%d passed%s\n", pass, pass + fail,
           fail ? " — FAILURES" : " — all OK");
    return fail > 0 ? 1 : 0;
}
