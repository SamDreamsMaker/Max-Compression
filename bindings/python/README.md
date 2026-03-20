# maxcomp — Python bindings for MaxCompression

[![PyPI](https://img.shields.io/badge/pypi-maxcomp-blue)](https://pypi.org/project/maxcomp/)
[![License](https://img.shields.io/badge/license-GPL--3.0-green)](https://github.com/SamDreamsMaker/Max-Compression/blob/main/LICENSE)
[![Python](https://img.shields.io/badge/python-≥3.7-yellow)](https://www.python.org/)

High-ratio lossless data compression for Python. Wraps the [MaxCompression](https://github.com/SamDreamsMaker/Max-Compression) C library via ctypes — no Cython or pybind11 required.

MCX beats bzip2 on all standard benchmark files and competes with xz/LZMA2 on most data types, with standout results on structured and repetitive data (up to **50× on spreadsheets**).

## Installation

### From PyPI (recommended)

```bash
pip install maxcomp
```

This builds the C library from source via CMake. **Requirements:**
- Python ≥ 3.7
- CMake ≥ 3.15
- C compiler (GCC, Clang, or MSVC)

### From source

```bash
git clone https://github.com/SamDreamsMaker/Max-Compression.git
cd Max-Compression

# Build the shared library
mkdir build && cd build
cmake -S .. -B . -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install the Python package
cd ../bindings/python
pip install -e .
```

### Verify installation

```python
import maxcomp
print(maxcomp.__version__)   # "2.1.1"
print(maxcomp.version())     # Native library version
```

## Quick Start

```python
import maxcomp

# Compress data
data = b"Hello, MaxCompression! " * 1000
compressed = maxcomp.compress(data, level=6)
print(f"Compressed {len(data)} → {len(compressed)} bytes ({len(data)/len(compressed):.1f}×)")

# Decompress
original = maxcomp.decompress(compressed)
assert original == data  # Always lossless
```

## Usage Examples

### File compression

```python
import maxcomp

def compress_file(input_path: str, output_path: str, level: int = 12) -> float:
    """Compress a file and return the compression ratio."""
    with open(input_path, "rb") as f:
        data = f.read()
    
    compressed = maxcomp.compress(data, level=level)
    
    with open(output_path, "wb") as f:
        f.write(compressed)
    
    ratio = len(data) / len(compressed)
    print(f"{input_path}: {len(data):,} → {len(compressed):,} bytes ({ratio:.2f}×)")
    return ratio


def decompress_file(input_path: str, output_path: str) -> None:
    """Decompress a .mcx file back to original."""
    with open(input_path, "rb") as f:
        compressed = f.read()
    
    data = maxcomp.decompress(compressed)
    
    with open(output_path, "wb") as f:
        f.write(data)
    
    print(f"{input_path}: {len(compressed):,} → {len(data):,} bytes")
```

### Inspecting compressed frames

```python
import maxcomp

with open("archive.mcx", "rb") as f:
    compressed = f.read()

info = maxcomp.get_frame_info(compressed)
print(f"Original size : {info['original_size']:,} bytes")
print(f"MCX version   : {info['version']}")
print(f"Level         : {info['level']}")
print(f"Strategy      : {info['strategy']}")
print(f"Flags         : {info['flags']:#x}")
```

### Choosing the right level

```python
import maxcomp
import time

data = open("largefile.bin", "rb").read()

for level in [3, 6, 9, 12, 20]:
    start = time.perf_counter()
    compressed = maxcomp.compress(data, level=level)
    elapsed = time.perf_counter() - start
    ratio = len(data) / len(compressed)
    speed = len(data) / (1024 * 1024) / elapsed
    print(f"L{level:2d}: {ratio:5.2f}× | {speed:6.1f} MB/s | {len(compressed):,} bytes | {elapsed:.2f}s")
    
    # Verify roundtrip
    assert maxcomp.decompress(compressed) == data
```

### Pre-allocating output buffers

```python
import maxcomp

data = b"some data to compress"
max_size = maxcomp.compress_bound(len(data))
print(f"Max compressed size for {len(data)} bytes: {max_size} bytes")
# Useful for pre-allocating buffers in performance-critical code
```

### Verifying compressed data

```python
import maxcomp

data = b"Important data" * 100
compressed = maxcomp.compress(data, level=6)

# Basic integrity check
result = maxcomp.verify(compressed)
print(f"Valid: {result['valid']}, ratio: {result['ratio']:.2f}×")

# Roundtrip verification (checks decompress matches original)
result = maxcomp.verify(compressed, original=data)
print(f"Roundtrip OK: {result['roundtrip_ok']}")
```

### Comparing compressed archives

```python
import maxcomp

data = b"Hello, World! " * 10000
fast = maxcomp.compress(data, level=3)
best = maxcomp.compress(data, level=20)

diff = maxcomp.diff(fast, best)
print(f"Size delta: {diff['size_delta']:+,} bytes")
# Negative delta = second archive is smaller
```

### Error handling

```python
import maxcomp

try:
    maxcomp.decompress(b"not a valid mcx frame")
except maxcomp.MaxCompressionError as e:
    print(f"Decompression error: {e}")

try:
    maxcomp.compress("not bytes")  # Must be bytes or bytearray
except TypeError as e:
    print(f"Type error: {e}")
```

## API Reference

### `maxcomp.compress(data, level=3) → bytes`

Compress a bytes object using MaxCompression.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `data` | `bytes \| bytearray` | — | Input data to compress |
| `level` | `int` | `3` | Compression level (1–26) |

**Returns:** Compressed bytes in MCX frame format.

**Raises:** `TypeError` if data is not bytes/bytearray. `MaxCompressionError` on compression failure.

### `maxcomp.decompress(data) → bytes`

Decompress an MCX-encoded bytes object.

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `bytes \| bytearray` | MCX compressed data |

**Returns:** Original uncompressed bytes.

**Raises:** `TypeError` if data is not bytes/bytearray. `MaxCompressionError` on decompression failure or size mismatch.

### `maxcomp.get_frame_info(data) → dict`

Read metadata from an MCX frame header without decompressing.

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `bytes \| bytearray` | MCX compressed data (only header is read) |

**Returns:** Dictionary with keys:

| Key | Type | Description |
|-----|------|-------------|
| `original_size` | `int` | Original uncompressed size in bytes |
| `version` | `int` | MCX format version |
| `level` | `int` | Compression level used |
| `strategy` | `int` | Compression strategy ID |
| `flags` | `int` | Frame flags |

**Raises:** `MaxCompressionError` if the header is invalid.

### `maxcomp.compress_bound(size) → int`

Return the maximum possible compressed size for a given input size. Useful for pre-allocating buffers.

| Parameter | Type | Description |
|-----------|------|-------------|
| `size` | `int` | Input data size in bytes |

**Returns:** Upper bound on compressed output size.

### `maxcomp.verify(compressed, original=None) → dict`

Verify integrity of compressed data. If `original` is provided, also checks roundtrip correctness.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `compressed` | `bytes \| bytearray` | — | MCX compressed data |
| `original` | `bytes \| bytearray \| None` | `None` | Original data for roundtrip check |

**Returns:** Dictionary with keys: `valid` (bool), `original_size` (int), `compressed_size` (int), `ratio` (float), and optionally `roundtrip_ok` (bool).

### `maxcomp.diff(compressed_a, compressed_b) → dict`

Compare two MCX compressed archives, showing differences in size, ratio, and strategy.

| Parameter | Type | Description |
|-----------|------|-------------|
| `compressed_a` | `bytes \| bytearray` | First MCX archive |
| `compressed_b` | `bytes \| bytearray` | Second MCX archive |

**Returns:** Dictionary with comparison details: sizes, ratios, levels, strategies, and size delta.

### `maxcomp.version() → str`

Return the native MaxCompression library version string (e.g., `"2.1.1"`).

### `maxcomp.MaxCompressionError`

Exception raised when the native compression/decompression layer encounters an error.

## Compression Levels

| Level | Strategy | Speed | Ratio | Best for |
|-------|----------|-------|-------|----------|
| 1–3 | LZ77 greedy | ★★★★★ | ★★☆☆☆ | Quick compression, real-time |
| 4–6 | LZ-HC + rANS | ★★★★☆ | ★★★☆☆ | General-purpose |
| 7–9 | LZ-HC + AAC | ★★★☆☆ | ★★★★☆ | Good ratio, reasonable time |
| 10–12 | BWT + rANS | ★★☆☆☆ | ★★★★★ | Text, DNA, structured data |
| 13–20 | Smart auto | ★☆☆☆☆ | ★★★★★ | Maximum ratio (tries multiple strategies) |
| 21–23 | LZRC | ★★★☆☆ | ★★★★☆ | Binary, executables |
| 24–26 | LZRC-HC/BT | ★★☆☆☆ | ★★★★★ | Binary, maximum ratio |

**Recommended levels:**
- **L3** (default) — fast, decent ratio
- **L6** — good balance for most files
- **L12** — high ratio, great for archiving
- **L20** — maximum ratio, auto-selects best strategy per block

## Platform Support

| Platform | Status |
|----------|--------|
| Linux (x64, ARM) | ✅ Fully supported |
| macOS (x64, ARM) | ✅ Fully supported |
| Windows (MSVC) | ✅ Supported |

## Thread Safety

The `compress()` and `decompress()` functions are safe to call from multiple threads — each call uses independent buffers. The underlying C library is stateless for one-shot operations.

## Links

- **GitHub:** [Max-Compression](https://github.com/SamDreamsMaker/Max-Compression)
- **Documentation:** [docs/](https://github.com/SamDreamsMaker/Max-Compression/tree/main/docs)
- **Changelog:** [CHANGELOG.md](https://github.com/SamDreamsMaker/Max-Compression/blob/main/CHANGELOG.md)
- **Issues:** [GitHub Issues](https://github.com/SamDreamsMaker/Max-Compression/issues)
- **License:** [GPL-3.0](https://github.com/SamDreamsMaker/Max-Compression/blob/main/LICENSE)
