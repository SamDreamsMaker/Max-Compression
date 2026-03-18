# MCX API Reference

## Overview

MaxCompression provides a simple C API with two primary functions for one-shot compression and decompression. The library handles all format details internally.

## Header

```c
#include <maxcomp/maxcomp.h>
```

## Functions

### `mcx_compress`

```c
size_t mcx_compress(void* dst, size_t dst_cap,
                    const void* src, size_t src_size,
                    int level);
```

Compress `src_size` bytes from `src` into `dst`.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `dst` | Output buffer for compressed data |
| `dst_cap` | Capacity of output buffer in bytes |
| `src` | Input data to compress |
| `src_size` | Size of input data in bytes |
| `level` | Compression level (1–20, see below) |

**Returns:** Compressed size in bytes, or an error code (check with `mcx_is_error`).

**Buffer sizing:** The output buffer should be at least `src_size + 4096` bytes to handle incompressible data safely.

---

### `mcx_decompress`

```c
size_t mcx_decompress(void* dst, size_t dst_cap,
                      const void* src, size_t src_size);
```

Decompress `src_size` bytes from `src` into `dst`.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `dst` | Output buffer for decompressed data |
| `dst_cap` | Capacity of output buffer in bytes |
| `src` | Compressed data (MCX format) |
| `src_size` | Size of compressed data in bytes |

**Returns:** Decompressed size in bytes, or an error code.

**Note:** The original size is stored in the MCX frame header. Use `dst_cap >= original_size`.

---

### `mcx_is_error`

```c
int mcx_is_error(size_t result);
```

Check if a return value from `mcx_compress` or `mcx_decompress` indicates an error.

**Returns:** Non-zero if `result` is an error code, zero if it's a valid size.

---

### `mcx_get_error_name`

```c
const char* mcx_get_error_name(size_t result);
```

Get a human-readable error description.

**Returns:** Static string describing the error, or `"No error"` if the result is valid.

## Compression Levels

| Level | Strategy | Description |
|-------|----------|-------------|
| 1–3 | LZ77 greedy | Fast compression, lowest ratio |
| 4–8 | LZ77 lazy HC | Hash chain match finder, better ratio |
| 9 | LZ77 + AAC | Adaptive arithmetic coding, best LZ ratio |
| 10–14 | BWT + rANS | Burrows-Wheeler transform, high ratio |
| **20** | **Smart Mode** | **Auto-detect data type, best ratio** |

**Recommendation:** Use level 20 for maximum compression, level 6 for general purpose, level 3 for speed.

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| `MCX_ERR_INVALID_PARAM` | Invalid parameter | NULL pointer or zero size |
| `MCX_ERR_ALLOC_FAILED` | Allocation failed | Out of memory |
| `MCX_ERR_DST_TOO_SMALL` | Buffer too small | Output buffer insufficient |
| `MCX_ERR_CORRUPT` | Corrupt data | Invalid or corrupted input |
| `MCX_ERR_CHECKSUM` | Checksum mismatch | CRC32 verification failed |

## Thread Safety

- `mcx_compress` and `mcx_decompress` are **reentrant** — no global state.
- Multiple threads can compress/decompress simultaneously with independent buffers.
- OpenMP parallelism is used internally for block-level compression at higher levels.

## Example

```c
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    // Read input
    FILE* f = fopen("input.bin", "rb");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    // Compress
    size_t comp_cap = size + 4096;
    void* comp = malloc(comp_cap);
    size_t comp_size = mcx_compress(comp, comp_cap, data, size, 20);

    if (mcx_is_error(comp_size)) {
        fprintf(stderr, "Compression failed: %s\n",
                mcx_get_error_name(comp_size));
        return 1;
    }

    printf("Compressed: %zu → %zu (%.2f×)\n",
           size, comp_size, (double)size / comp_size);

    // Decompress and verify
    void* dec = malloc(size);
    size_t dec_size = mcx_decompress(dec, size, comp, comp_size);

    if (dec_size == size && memcmp(data, dec, size) == 0) {
        printf("Roundtrip OK\n");
    }

    free(data);
    free(comp);
    free(dec);
    return 0;
}
```

**Compile:**
```bash
gcc -O2 -o example example.c -lmaxcomp -lm -lpthread
```
