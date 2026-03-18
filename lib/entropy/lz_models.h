/**
 * @file lz_models.h
 * @brief Probability models for LZ + Range Coder (v2.0).
 *
 * Distance encoding uses slot-based scheme similar to DEFLATE/LZMA2:
 * - 16 offset slots (0-15), each covering a range of offsets
 * - Slot 0: offset 1
 * - Slot 1: offset 2
 * - Slot 2-3: offsets 3-4 (1 extra bit)
 * - Slot 4-5: offsets 5-8 (2 extra bits)
 * - Slot 6-7: offsets 9-16 (3 extra bits)
 * - ...
 * - Slot 14-15: offsets 8193-16384 (12 extra bits)
 *
 * Extra bits < 4 are context-coded (adaptive).
 * Extra bits >= 4 are direct-coded (fixed 50/50).
 */

#ifndef MCX_LZ_MODELS_H
#define MCX_LZ_MODELS_H

#include <stdint.h>

#define LZ_NUM_DIST_SLOTS  16
#define LZ_NUM_LEN_SLOTS   16
#define LZ_ALIGN_BITS      4
#define LZ_NUM_ALIGN       (1 << LZ_ALIGN_BITS)

/* Context indices for is_match */
#define LZ_CTX_PREV_BYTE   0   /* Previous byte (0-255) */
#define LZ_CTX_AFTER_MATCH 256 /* Just after a match */

typedef struct {
    /* Match/literal flag — context = (prev_byte, after_match) */
    uint16_t is_match[512];  /* 256 prev_byte + 256 after_match */
    
    /* Literal model: bit-tree with 16 contexts (prev_byte >> 4) */
    uint16_t lit_probs[16][256];
    
    /* Match length model */
    uint16_t len_choice;            /* 0 = short (2-9), 1 = long (10+) */
    uint16_t len_short[8];          /* 3-bit code for lengths 2-9 */
    uint16_t len_long_choice;       /* 0 = medium (10-17), 1 = extra (18+) */
    uint16_t len_medium[8];         /* 3-bit code for lengths 10-17 */
    uint16_t len_extra[256];        /* 8-bit code for lengths 18-273 */
    
    /* Distance model */
    uint16_t dist_slot[LZ_NUM_DIST_SLOTS];  /* Slot selector (4-bit tree) */
    uint16_t dist_align[LZ_NUM_ALIGN];      /* Low 4 bits for large distances */
    uint16_t dist_spec[4][8];               /* Special: 1-3 extra bits per slot */
    
    /* Recent distance cache (rep matches) */
    uint32_t rep_dist[4];
    uint16_t is_rep[4];             /* P(repeat distance i) */
} LZCMModel;

/* Get distance slot for an offset */
static inline int dist_to_slot(uint32_t dist) {
    if (dist < 2) return (int)dist;
    int slot = 2;
    uint32_t d = dist;
    while (d >= 4) { d >>= 1; slot += 2; }
    return slot + (int)(d & 1);
}

/* Get base distance and extra bits for a slot */
static inline void slot_to_dist(int slot, uint32_t* base, int* extra_bits) {
    if (slot < 2) {
        *base = (uint32_t)slot;
        *extra_bits = 0;
    } else {
        *extra_bits = (slot >> 1) - 1;
        *base = (2 | (slot & 1)) << *extra_bits;
    }
}

#endif /* MCX_LZ_MODELS_H */
