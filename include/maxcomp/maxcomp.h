/**
 * @file maxcomp.h
 * @brief MaxCompression — Revolutionary file compression library.
 *
 * Public API. This is the ONLY header users need to include.
 *
 * Usage:
 *   #include <maxcomp/maxcomp.h>
 *
 * Thread safety:
 *   - Compression: NOT thread-safe (uses internal state)
 *   - Decompression: Thread-safe (stateless)
 *
 * Error handling:
 *   Functions returning size_t use the top bit to signal errors.
 *   Always check with mcx_is_error() before using the result.
 *
 * @copyright GNU General Public License v3.0 (GPL-3.0)
 */

#ifndef MAXCOMP_H
#define MAXCOMP_H

#include <stddef.h>   /* size_t */
#include <stdint.h>   /* uint8_t, uint32_t, etc. */

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  Platform export macros
 * ═══════════════════════════════════════════════════════════════════════ */

#if defined(MCX_SHARED) && defined(_WIN32)
    #if defined(MCX_BUILDING)
        #define MCXAPI __declspec(dllexport)
    #else
        #define MCXAPI __declspec(dllimport)
    #endif
#elif defined(MCX_SHARED) && defined(__GNUC__)
    #define MCXAPI __attribute__((visibility("default")))
#else
    #define MCXAPI
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  Version
 * ═══════════════════════════════════════════════════════════════════════ */

#define MCX_VERSION_MAJOR  2
#define MCX_VERSION_MINOR  1
#define MCX_VERSION_PATCH  0

/** Returns version as a packed integer: (major*10000 + minor*100 + patch) */
MCXAPI unsigned mcx_version_number(void);

/** Returns version as a human-readable string, e.g. "1.1.0" */
MCXAPI const char* mcx_version_string(void);

/* ═══════════════════════════════════════════════════════════════════════
 *  Error handling
 * ═══════════════════════════════════════════════════════════════════════ */

/** Check if a result is an error (top bit set). */
MCXAPI int mcx_is_error(size_t result);

/** Get a human-readable error message for an error result. */
MCXAPI const char* mcx_get_error_name(size_t result);

/* ═══════════════════════════════════════════════════════════════════════
 *  Compression levels
 * ═══════════════════════════════════════════════════════════════════════ */

#define MCX_LEVEL_MIN       1
#define MCX_LEVEL_DEFAULT   3
#define MCX_LEVEL_MAX      26

/* ═══════════════════════════════════════════════════════════════════════
 *  Simple API — Single-shot, in-memory compression
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Returns the maximum compressed size for a given source size.
 * Useful for allocating the destination buffer.
 */
MCXAPI size_t mcx_compress_bound(size_t src_size);

/**
 * Compress data from src into dst.
 *
 * @param dst       Destination buffer (must be at least mcx_compress_bound(src_size) bytes)
 * @param dst_cap   Capacity of the destination buffer in bytes
 * @param src       Source data to compress
 * @param src_size  Size of the source data in bytes
 * @param level     Compression level (MCX_LEVEL_MIN to MCX_LEVEL_MAX)
 * @return          Compressed size in bytes, or an error (check with mcx_is_error)
 */
MCXAPI size_t mcx_compress(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size,
    int         level
);

/**
 * Decompress data from src into dst.
 *
 * @param dst       Destination buffer (must be large enough for the original data)
 * @param dst_cap   Capacity of the destination buffer in bytes
 * @param src       Compressed data
 * @param src_size  Size of the compressed data in bytes
 * @return          Decompressed size in bytes, or an error (check with mcx_is_error)
 */
MCXAPI size_t mcx_decompress(
    void*       dst,
    size_t      dst_cap,
    const void* src,
    size_t      src_size
);

/**
 * Read the decompressed size from a compressed frame header.
 *
 * @param src       Compressed data (only the header is read)
 * @param src_size  Size of the compressed data in bytes
 * @return          Original (decompressed) size, or 0 if unknown/error
 */
MCXAPI unsigned long long mcx_get_decompressed_size(
    const void* src,
    size_t      src_size
);

/**
 * Frame information structure.
 * Populated by mcx_get_frame_info() from a compressed frame header.
 */
typedef struct {
    unsigned long long original_size; /**< Decompressed size (0 if unknown) */
    unsigned version;                 /**< Format version */
    unsigned level;                   /**< Compression level used */
    unsigned strategy;                /**< Strategy ID (see internal.h) */
    unsigned flags;                   /**< Frame flags */
} mcx_frame_info;

/**
 * Read frame information from a compressed buffer.
 *
 * @param info      Output frame info structure
 * @param src       Compressed data (only header is read)
 * @param src_size  Size of compressed data in bytes
 * @return          0 on success, or an error (check with mcx_is_error)
 */
MCXAPI size_t mcx_get_frame_info(
    mcx_frame_info* info,
    const void*     src,
    size_t          src_size
);

/* ═══════════════════════════════════════════════════════════════════════
 *  Streaming API — Incremental processing
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    const void* src;  /* start of input buffer */
    size_t size;      /* size of input buffer */
    size_t pos;       /* position where reading stopped. Will be updated. */
} mcx_in_buffer;

typedef struct {
    void* dst;        /* start of output buffer */
    size_t size;      /* size of output buffer */
    size_t pos;       /* position where writing stopped. Will be updated. */
} mcx_out_buffer;

typedef struct mcx_cctx_s mcx_cctx;
typedef struct mcx_dctx_s mcx_dctx;

/** Allocate a new compression context */
MCXAPI mcx_cctx* mcx_create_cctx(void);

/** Free a compression context */
MCXAPI void mcx_free_cctx(mcx_cctx* cctx);

/**
 * Compress data from input to output using a streaming model.
 * 
 * You must provide an initialized mcx_cctx.
 * 
 * @param cctx      Compression context
 * @param output    Output buffer tracking (dst, size, pos)
 * @param input     Input buffer tracking (src, size, pos). Set src = NULL and size = 0 to flush final frame chunks.
 * @param level     Compression level (MCX_LEVEL_MIN to MCX_LEVEL_MAX)
 * @return          0 when frame finishes successfully, or an Error code (check with mcx_is_error), or 1 if it needs more input/output space.
 */
MCXAPI size_t mcx_compress_stream(
    mcx_cctx*       cctx,
    mcx_out_buffer* output,
    mcx_in_buffer*  input,
    int             level
);

/** Allocate a new decompression context */
MCXAPI mcx_dctx* mcx_create_dctx(void);

/** Free a decompression context */
MCXAPI void mcx_free_dctx(mcx_dctx* dctx);

/**
 * Decompress stream data incrementally.
 *
 * @param dctx      Decompression context
 * @param output    Output buffer tracking
 * @param input     Input buffer tracking
 * @return          0 if frame finishes successfully, 1 if more data/space needed, or an Error code.
 */
MCXAPI size_t mcx_decompress_stream(
    mcx_dctx*       dctx,
    mcx_out_buffer* output,
    mcx_in_buffer*  input
);

/* ═══════════════════════════════════════════════════════════════════════
 *  Frame format magic bytes
 * ═══════════════════════════════════════════════════════════════════════ */

/** Magic number at the start of every .mcx file: "MCX\x01" */
#define MCX_MAGIC  0x0158434D

/* ═══════════════════════════════════════════════════════════════════════
 *  Experimental: Ultra-Speed Mathematical LZ Prototype (mcx_lz_fast)
 *  AVX2 parallel hashing + branchless SIMD decompression.
 *  WARNING: Own token format — NOT compatible with standard .mcx frames.
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct { uint32_t dict[2][65536]; } mcx_lz_fast_ctx;

MCXAPI void   mcx_lz_fast_init      (mcx_lz_fast_ctx* ctx);
MCXAPI size_t mcx_lz_fast_compress  (uint8_t* dst, size_t dst_cap,
                                     const uint8_t* src, size_t src_size,
                                     mcx_lz_fast_ctx* ctx);
MCXAPI size_t mcx_lz_fast_decompress(uint8_t* dst, size_t dst_cap,
                                     const uint8_t* src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif /* MAXCOMP_H */
