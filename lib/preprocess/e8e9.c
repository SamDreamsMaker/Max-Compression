/**
 * @file e8e9.c
 * @brief E8/E9 x86 CALL/JMP address preprocessing filter.
 *
 * Converts relative addresses in x86 CALL (0xE8) and JMP (0xE9) instructions
 * to absolute addresses, improving compression of executable binary files.
 * This is the same technique used by BCJ filters in 7z/xz/LZMA2.
 *
 * The filter is reversible (lossless) — the inverse transform recovers
 * the original data exactly.
 */

#include <stdint.h>
#include <stddef.h>

/**
 * Apply E8/E9 forward transform (relative → absolute addresses).
 * Modifies data in-place.
 *
 * @param data  Input/output buffer
 * @param size  Buffer size
 * @return Number of bytes processed (always == size)
 */
size_t mcx_e8e9_encode(uint8_t* data, size_t size) {
    if (size < 5) return size;
    
    /* Process up to size-4 to avoid reading past the end.
     * Each E8/E9 instruction is 5 bytes: opcode + 4-byte relative offset. */
    size_t limit = size - 4;
    
    for (size_t i = 0; i < limit; i++) {
        if (data[i] == 0xE8 || data[i] == 0xE9) {
            /* Read 32-bit little-endian relative offset */
            int32_t rel = (int32_t)(
                (uint32_t)data[i+1] |
                ((uint32_t)data[i+2] << 8) |
                ((uint32_t)data[i+3] << 16) |
                ((uint32_t)data[i+4] << 24)
            );
            
            /* Convert to absolute: abs = rel + current_position */
            int32_t abs_addr = rel + (int32_t)i;
            
            /* Write back as little-endian */
            data[i+1] = (uint8_t)(abs_addr);
            data[i+2] = (uint8_t)(abs_addr >> 8);
            data[i+3] = (uint8_t)(abs_addr >> 16);
            data[i+4] = (uint8_t)(abs_addr >> 24);
            
            /* Skip the 4 address bytes */
            i += 4;
        }
    }
    
    return size;
}

/**
 * Apply E8/E9 inverse transform (absolute → relative addresses).
 * Modifies data in-place.
 *
 * @param data  Input/output buffer
 * @param size  Buffer size  
 * @return Number of bytes processed (always == size)
 */
size_t mcx_e8e9_decode(uint8_t* data, size_t size) {
    if (size < 5) return size;
    
    size_t limit = size - 4;
    
    for (size_t i = 0; i < limit; i++) {
        if (data[i] == 0xE8 || data[i] == 0xE9) {
            /* Read 32-bit little-endian absolute offset */
            int32_t abs_addr = (int32_t)(
                (uint32_t)data[i+1] |
                ((uint32_t)data[i+2] << 8) |
                ((uint32_t)data[i+3] << 16) |
                ((uint32_t)data[i+4] << 24)
            );
            
            /* Convert back to relative: rel = abs - current_position */
            int32_t rel = abs_addr - (int32_t)i;
            
            /* Write back as little-endian */
            data[i+1] = (uint8_t)(rel);
            data[i+2] = (uint8_t)(rel >> 8);
            data[i+3] = (uint8_t)(rel >> 16);
            data[i+4] = (uint8_t)(rel >> 24);
            
            /* Skip the 4 address bytes */
            i += 4;
        }
    }
    
    return size;
}
