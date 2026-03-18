/*
 * divsufsort.h for libdivsufsort (embedded, 32-bit version)
 * Copyright (c) 2003-2008 Yuta Mori All Rights Reserved.
 * MIT License
 */

#ifndef _DIVSUFSORT_H
#define _DIVSUFSORT_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#ifndef DIVSUFSORT_API
#define DIVSUFSORT_API
#endif

typedef uint8_t sauchar_t;
typedef int32_t saint_t;
typedef int32_t saidx_t;

#define PRIdSAINT_T PRId32
#define PRIdSAIDX_T PRId32

/**
 * Constructs the suffix array of a given string.
 * @param T[0..n-1] The input string.
 * @param SA[0..n-1] The output array of suffixes.
 * @param n The length of the input string.
 * @return 0 if no error, -1 or -2 on error.
 */
DIVSUFSORT_API
saint_t divsufsort(const sauchar_t *T, saidx_t *SA, saidx_t n);

/**
 * Constructs the BWT of a given string.
 * @param T[0..n-1] The input string.
 * @param U[0..n-1] The output string (BWT).
 * @param A[0..n-1] Temporary array.
 * @param n The length of the input string.
 * @return Primary index if no error, -1 or -2 on error.
 */
DIVSUFSORT_API
saidx_t divbwt(const sauchar_t *T, sauchar_t *U, saidx_t *A, saidx_t n);

#ifdef __cplusplus
}
#endif

#endif /* _DIVSUFSORT_H */
