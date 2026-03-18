/**
 * @file compat.h
 * @brief Cross-platform compatibility macros.
 */
#ifndef MCX_COMPAT_H
#define MCX_COMPAT_H

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

#endif /* MCX_COMPAT_H */
