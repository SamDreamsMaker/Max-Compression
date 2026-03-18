# maxcomp — Python bindings for MaxCompression

High-ratio lossless data compression library. Beats bzip2 on all file types,
competitive with xz/LZMA2.

## Installation

```bash
pip install maxcomp
```

Requires CMake and a C compiler (GCC, Clang, or MSVC).

## Quick Start

```python
import maxcomp

# Compress
data = open("myfile.bin", "rb").read()
compressed = maxcomp.compress(data, level=20)  # level 1-26

# Decompress
original = maxcomp.decompress(compressed)
assert original == data

# Inspect compressed data
info = maxcomp.get_frame_info(compressed)
print(f"Original size: {info['original_size']}, Level: {info['level']}")

# Version
print(maxcomp.version())  # "2.1.0"
```

## Compression Levels

| Level | Strategy | Speed | Ratio |
|-------|----------|-------|-------|
| 1–3 | LZ77 greedy | Fast | Low |
| 6 | LZ-HC + rANS | Medium | Medium |
| 9 | LZ-HC + AAC | Slow | Good |
| 12 | BWT + rANS | Very slow | Very good |
| **20** | **Smart auto** | **Slowest** | **Best** |
| 24 | LZRC-HC | Medium | Good (binary) |
| 26 | LZRC-BT | Slow | Best (binary) |

## Links

- [GitHub](https://github.com/SamDreamsMaker/Max-Compression)
- [Full Documentation](https://github.com/SamDreamsMaker/Max-Compression/tree/main/docs)
- [Changelog](https://github.com/SamDreamsMaker/Max-Compression/blob/main/CHANGELOG.md)
