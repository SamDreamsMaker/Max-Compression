/**
 * @file compat.h
 * @brief Cross-platform compatibility macros.
 *
 * Must be included BEFORE system headers to ensure _POSIX_C_SOURCE is defined.
 */
#ifndef MCX_COMPAT_H
#define MCX_COMPAT_H

/* Ensure POSIX APIs (clock_gettime, etc.) are available on Linux/Clang */
#if !defined(_WIN32) && !defined(__APPLE__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <stdint.h>

/* ── Count trailing zeros (portable) ─────────────────────────────── */
#if defined(_MSC_VER)
#include <intrin.h>
static __inline int mcx_ctzll(uint64_t val) {
    unsigned long idx;
#if defined(_M_X64) || defined(_M_ARM64)
    _BitScanForward64(&idx, val);
#else
    /* 32-bit MSVC fallback */
    if (_BitScanForward(&idx, (uint32_t)val))
        return (int)idx;
    _BitScanForward(&idx, (uint32_t)(val >> 32));
    return (int)(idx + 32);
#endif
    return (int)idx;
}
#else
#define mcx_ctzll(x) __builtin_ctzll(x)
#endif

/* ── Portable high-resolution timer ──────────────────────────────── */
#if defined(_WIN32)
#include <windows.h>
typedef struct { LARGE_INTEGER t; } mcx_timer_t;
static __inline void mcx_timer_start(mcx_timer_t* t) { QueryPerformanceCounter(&t->t); }
static __inline double mcx_timer_elapsed_ms(mcx_timer_t* start, mcx_timer_t* end) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return (double)(end->t.QuadPart - start->t.QuadPart) * 1000.0 / (double)freq.QuadPart;
}
#elif defined(__APPLE__)
#include <mach/mach_time.h>
typedef struct { uint64_t t; } mcx_timer_t;
static inline void mcx_timer_start(mcx_timer_t* t) { t->t = mach_absolute_time(); }
static inline double mcx_timer_elapsed_ms(mcx_timer_t* start, mcx_timer_t* end) {
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    return (double)(end->t - start->t) * info.numer / info.denom / 1000000.0;
}
#else
/* POSIX: Linux, FreeBSD, etc. */
#include <time.h>
typedef struct { struct timespec t; } mcx_timer_t;
static inline void mcx_timer_start(mcx_timer_t* timer) { clock_gettime(CLOCK_MONOTONIC, &timer->t); }
static inline double mcx_timer_elapsed_ms(mcx_timer_t* start, mcx_timer_t* end) {
    return (end->t.tv_sec - start->t.tv_sec) * 1000.0 + (end->t.tv_nsec - start->t.tv_nsec) / 1000000.0;
}
#endif

#endif /* MCX_COMPAT_H */
