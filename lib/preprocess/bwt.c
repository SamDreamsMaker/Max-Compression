/**
 * @file bwt.c
 * @brief Burrows-Wheeler Transform — powered by SA-IS (linear time).
 *
 * The BWT reorders data so that similar characters cluster together,
 * drastically improving subsequent compression (especially with MTF + RLE).
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  SA-IS Algorithm (Nong, Zhang & Chan, 2009)                        │
 * │                                                                     │
 * │  Builds a suffix array in O(n) time and O(n) space.                │
 * │  Key idea: classify each suffix as S-type or L-type, find          │
 * │  the leftmost-S (LMS) suffixes, sort them by induced sorting,      │
 * │  then induce the full suffix array from the LMS order.             │
 * │                                                                     │
 * │  For BWT, we append a sentinel byte (value 0, position n)          │
 * │  which is lexicographically smaller than all other characters.      │
 * │  This ensures correct cyclic rotation sorting.                      │
 * └─────────────────────────────────────────────────────────────────────┘
 */

#include "preprocess.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef MCX_USE_DIVSUFSORT
#include "divsufsort.h"
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  SA-IS: Suffix Array by Induced Sorting
 * ═══════════════════════════════════════════════════════════════════════ */

#define SAIS_EMPTY ((int32_t)-1)

/* Cached bucket computation — avoids re-counting the entire input each call */
static void sais_count(const int32_t* T, int32_t* counts, int32_t n, int32_t K)
{
    int32_t i;
    for (i = 0; i < K; i++) counts[i] = 0;
    for (i = 0; i < n; i++) counts[T[i]]++;
}

static void sais_get_buckets_from_counts(const int32_t* counts, int32_t* bkt,
                                          int32_t K, int end)
{
    int32_t i, sum = 0;
    for (i = 0; i < K; i++) {
        sum += counts[i];
        bkt[i] = end ? sum : (sum - counts[i]);
    }
}

static void sais_get_buckets(const int32_t* T, int32_t* bkt,
                             int32_t n, int32_t K, int end)
{
    int32_t i, sum;
    for (i = 0; i < K; i++) bkt[i] = 0;
    for (i = 0; i < n; i++) bkt[T[i]]++;
    sum = 0;
    for (i = 0; i < K; i++) {
        sum += bkt[i];
        bkt[i] = end ? sum : (sum - bkt[i]);
    }
}

static void sais_induce_L(const int32_t* T, int32_t* SA,
                          const uint8_t* type, int32_t* bkt,
                          const int32_t* counts, int32_t n, int32_t K)
{
    int32_t i, j;
    sais_get_buckets_from_counts(counts, bkt, K, 0);
    for (i = 0; i < n; i++) {
        if (SA[i] == SAIS_EMPTY) continue;
        j = SA[i] - 1;
        if (j >= 0 && !type[j]) {
            SA[bkt[T[j]]++] = j;
        }
    }
}

static void sais_induce_S(const int32_t* T, int32_t* SA,
                          const uint8_t* type, int32_t* bkt,
                          const int32_t* counts, int32_t n, int32_t K)
{
    int32_t i, j;
    sais_get_buckets_from_counts(counts, bkt, K, 1);
    for (i = n - 1; i >= 0; i--) {
        if (SA[i] == SAIS_EMPTY) continue;
        j = SA[i] - 1;
        if (j >= 0 && type[j]) {
            SA[--bkt[T[j]]] = j;
        }
    }
}

static int sais_lms_equal(const int32_t* T, const uint8_t* type,
                          int32_t n, int32_t p1, int32_t p2)
{
    int32_t k;
    if (p1 < 0 || p2 < 0) return 0;
    for (k = 0; ; k++) {
        if (p1 + k >= n || p2 + k >= n) return 0;
        if (T[p1 + k] != T[p2 + k]) return 0;
        if (k > 0 && type[p1 + k] != type[p2 + k]) return 0;
        int lms1 = (k > 0 && type[p1 + k] && (p1 + k == 0 || !type[p1 + k - 1]));
        int lms2 = (k > 0 && type[p2 + k] && (p2 + k == 0 || !type[p2 + k - 1]));
        if (lms1 || lms2) return (lms1 && lms2);
    }
}

/** Core SA-IS on int32_t alphabet [0..K-1]. SA must be pre-allocated size n. */
static void sais_core(const int32_t* T, int32_t* SA, int32_t n, int32_t K)
{
    uint8_t* type;
    int32_t* bkt;
    int32_t  i, j, n1, name, prev;
    int32_t* SA1;
    int32_t* T1;

    if (n <= 1) { if (n == 1) SA[0] = 0; return; }
    if (n == 2) {
        if (T[0] < T[1]) { SA[0] = 0; SA[1] = 1; }
        else              { SA[0] = 1; SA[1] = 0; }
        return;
    }

    type = (uint8_t*)calloc((size_t)n, 1);
    bkt  = (int32_t*)malloc((size_t)K * sizeof(int32_t));
    if (!type || !bkt) { free(type); free(bkt); return; }

    /* Step 1: Classify S/L types */
    type[n - 1] = 1;
    for (i = n - 2; i >= 0; i--) {
        if (T[i] < T[i + 1])      type[i] = 1;
        else if (T[i] > T[i + 1]) type[i] = 0;
        else                       type[i] = type[i + 1];
    }

    /* Count characters once (avoid re-scanning in each bucket computation) */
    int32_t* counts = (int32_t*)malloc((size_t)K * sizeof(int32_t));
    if (!counts) { free(type); free(bkt); return; }
    sais_count(T, counts, n, K);

    /* Step 2: Place LMS suffixes at bucket ends */
    sais_get_buckets_from_counts(counts, bkt, K, 1);
    for (i = 0; i < n; i++) SA[i] = SAIS_EMPTY;

    n1 = 0;
    for (i = 1; i < n; i++) {
        if (type[i] && !type[i - 1]) {
            SA[--bkt[T[i]]] = i;
            n1++;
        }
    }

    /* No LMS? All same char -> SA = [n-1, n-2, ..., 0] */
    if (n1 == 0) {
        for (i = 0; i < n; i++) SA[i] = n - 1 - i;
        free(counts); free(bkt); free(type);
        return;
    }

    /* Step 3: Induce L and S */
    sais_induce_L(T, SA, type, bkt, counts, n, K);
    sais_induce_S(T, SA, type, bkt, counts, n, K);

    /* Step 4: Compact sorted LMS suffixes */
    n1 = 0;
    for (i = 0; i < n; i++) {
        if (SA[i] > 0 && type[SA[i]] && !type[SA[i] - 1]) {
            SA[n1++] = SA[i];
        }
    }

    /* Step 5: Name LMS substrings */
    for (i = n1; i < n; i++) SA[i] = SAIS_EMPTY;
    name = 0; prev = -1;
    for (i = 0; i < n1; i++) {
        if (!sais_lms_equal(T, type, n, SA[i], prev)) name++;
        prev = SA[i];
        SA[n1 + (SA[i] >> 1)] = name - 1;
    }
    j = 0;
    for (i = n1; i < n; i++) {
        if (SA[i] != SAIS_EMPTY) SA[n1 + j++] = SA[i];
    }

    /* Step 6: Recurse or direct */
    SA1 = SA;
    T1  = SA + n - n1;
    /* Safe right-shift memmove equivalent: iterate backwards! */
    for (i = n1 - 1; i >= 0; i--) T1[i] = SA[n1 + i];

    if (name < n1) {
        sais_core(T1, SA1, n1, name);
    } else {
        for (i = 0; i < n1; i++) SA1[T1[i]] = i;
    }

    j = 0;
    for (i = 1; i < n; i++) {
        if (type[i] && !type[i - 1]) T1[j++] = i;
    }
    for (i = 0; i < n1; i++) SA1[i] = T1[SA1[i]];
    for (i = n1; i < n; i++) SA[i] = SAIS_EMPTY;

    sais_get_buckets_from_counts(counts, bkt, K, 1);
    for (i = n1 - 1; i >= 0; i--) {
        j = SA[i]; SA[i] = SAIS_EMPTY;
        SA[--bkt[T[j]]] = j;
    }
    sais_induce_L(T, SA, type, bkt, counts, n, K);
    sais_induce_S(T, SA, type, bkt, counts, n, K);

    free(counts);
    free(bkt);
    free(type);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  BWT via Suffix Array with Sentinel
 *
 *  For BWT we need to sort all ROTATIONS, not just suffixes.
 *  The standard trick: append a sentinel character (value 0) that is
 *  smaller than all real bytes. We shift real bytes to [1..256].
 *  The sentinel creates a unique "end of string" that makes suffix
 *  sorting equivalent to rotation sorting.
 * ═══════════════════════════════════════════════════════════════════════ */

size_t mcx_bwt_forward(uint8_t* dst, size_t* primary_idx,
                       const uint8_t* src, size_t size)
{
#ifdef MCX_USE_DIVSUFSORT
    /* Fast path: use libdivsufsort (MIT licensed, embedded).
     * divbwt works directly on uint8_t, avoids int32_t copy overhead.
     * ~2-3x faster than our SA-IS on typical data. */
    if (dst == NULL || src == NULL || size == 0 || primary_idx == NULL) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }
    if (size == 1) {
        dst[0] = src[0];
        *primary_idx = 1;
        return 1;
    }
    if (size > (size_t)0x7FFFFFFE) return MCX_ERROR(MCX_ERR_GENERIC);
    
    /* divbwt needs a temporary suffix array */
    saidx_t* A = (saidx_t*)malloc(size * sizeof(saidx_t));
    if (!A) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);
    
    saidx_t pidx = divbwt(src, dst, A, (saidx_t)size);
    free(A);
    
    if (pidx < 0) return MCX_ERROR(MCX_ERR_GENERIC);
    *primary_idx = (size_t)pidx;
    return size;
#else
    /* Fallback: our SA-IS implementation */
    int32_t* T;
    int32_t* SA;
    int32_t  n;
    size_t   i;
    int32_t  j;
    int32_t  out_idx;

    if (dst == NULL || src == NULL || size == 0 || primary_idx == NULL) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }
    if (size == 1) {
        dst[0] = src[0];
        *primary_idx = 1;
        return 1;
    }
    if (size > (size_t)0x7FFFFFFE) return MCX_ERROR(MCX_ERR_GENERIC);

    n = (int32_t)(size + 1); /* +1 for sentinel */

    T  = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    SA = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    if (!T || !SA) { free(T); free(SA); return MCX_ERROR(MCX_ERR_ALLOC_FAILED); }

    /* Copy data with +1 offset: real bytes [1..256], sentinel = 0 */
    for (i = 0; i < size; i++) {
        T[i] = (int32_t)src[i] + 1;
    }
    T[size] = 0; /* sentinel */

    /* Build suffix array */
    sais_core(T, SA, n, 257); /* alphabet [0..256] */

    /* Extract BWT from suffix array */
    out_idx = 0;
    *primary_idx = 0;
    for (j = 0; j < n; j++) {
        if (SA[j] == 0) {
            *primary_idx = (size_t)out_idx;
        } else {
            dst[out_idx++] = src[SA[j] - 1];
        }
    }

    free(T);
    free(SA);

    if ((size_t)out_idx != size) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }

    return size;
#endif
}

/* ─── Inverse BWT ────────────────────────────────────────────────────── */

size_t mcx_bwt_inverse(uint8_t* dst, size_t primary_idx,
                       const uint8_t* src, size_t size)
{
    size_t count[256];
    size_t cumul[256];
    size_t sum;
    size_t i;
    int c;

    if (dst == NULL || src == NULL || size == 0) {
        return MCX_ERROR(MCX_ERR_GENERIC);
    }
    if (primary_idx > size) {
        return MCX_ERROR(MCX_ERR_SRC_CORRUPTED);
    }

    /* Count occurrences of each byte */
    memset(count, 0, sizeof(count));
    for (i = 0; i < size; i++) {
        count[src[i]]++;
    }

    /* Cumulative counts (start positions in sorted order) */
    sum = 0;
    for (c = 0; c < 256; c++) {
        cumul[c] = sum;
        sum += count[c];
    }

    /* Build LF mapping vector T.
     * Use uint32_t — block size ≤ 64MB fits in 32 bits. */
    uint32_t* T = (uint32_t*)malloc(size * sizeof(uint32_t));
    if (T == NULL) return MCX_ERROR(MCX_ERR_ALLOC_FAILED);

    for (i = 0; i < size; i++) {
        T[i] = (uint32_t)cumul[src[i]]++;
    }

    /* Reconstruct original data tracing backwards.
     * Branchless sentinel adjustment: idx_L = r - (r >= primary_idx) */
    uint32_t r = 0;
    uint32_t pidx32 = (uint32_t)primary_idx;
    for (i = 0; i < size; i++) {
        uint32_t idx_L = r - (r >= pidx32);
        dst[size - 1 - i] = src[idx_L];
        r = T[idx_L] + 1;
    }

    free(T);
    return size;
}
