<p align="center">
  <h1 align="center">MaxCompression</h1>
  <p align="center">
    <strong>High-ratio lossless data compression library</strong>
  </p>
  <p align="center">
    <a href="https://github.com/SamDreamsMaker/Max-Compression/actions"><img src="https://img.shields.io/github/actions/workflow/status/SamDreamsMaker/Max-Compression/ci.yml?style=flat-square&label=CI" alt="CI"></a>
    <a href="https://github.com/SamDreamsMaker/Max-Compression/releases"><img src="https://img.shields.io/github/v/tag/SamDreamsMaker/Max-Compression?style=flat-square&label=version" alt="Version"></a>
    <a href="#benchmarks"><img src="https://img.shields.io/badge/Silesia_corpus-4.35×-blue?style=flat-square" alt="Silesia"></a>
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-green?style=flat-square" alt="License"></a>
    <img src="https://img.shields.io/badge/language-C99-orange?style=flat-square" alt="C99">
    <img src="https://img.shields.io/badge/platform-Linux_%7C_macOS_%7C_Windows-lightgrey?style=flat-square" alt="Platform">
    <img src="https://img.shields.io/badge/version-2.2.0-purple?style=flat-square" alt="Version">
  </p>
</p>

---

MaxCompression (MCX) is a lossless compression library written in portable C99. It combines multiple compression strategies — LZ77 with adaptive entropy coding, Burrows-Wheeler Transform with multi-table rANS, and stride-delta preprocessing — under a unified API that automatically selects the best pipeline for each data type.

MCX targets **maximum compression ratio** while maintaining practical speeds. It beats bzip2 on 100% of standard benchmark files and competes with xz/LZMA2 on most data types.

## Key Results

| Metric | MCX | Best Alternative |
|--------|-----|-----------------|
| kennedy.xls (structured binary) | **50.1×** | xz: 21.0× — **2.4× better** |
| nci (chemical text, 33 MB) | **25.7×** | xz: 19.3× — **33% better** |
| alice29.txt (English text) | **3.53×** | bzip2: 3.52× — **beats bzip2** |
| mozilla (50 MB binary archive) | **3.22×** | xz: 3.55× — **closes gap** |
| enwik8 (100 MB Wikipedia) | **4.04×** | xz: 3.89× — **beats xz by 4%** |
| Silesia corpus (202 MB total) | **4.35×** | bzip2: ~3.3× — **+32%** |

## Features

- **Smart Mode** — automatically detects data type and selects the optimal compression pipeline
- **Multiple strategies** — LZ77 (fast), BWT+rANS (high ratio), LZRC v2.0 (binary), stride-delta (structured binary)
- **LZRC v2.0 engine** — LZ + adaptive range coder with binary tree/hash chain match finder
- **Adaptive entropy coding** — order-1 arithmetic coding on LZ output, multi-table rANS on BWT output
- **E8/E9 x86 filter** — preprocesses executables for better compression (+16% on x86 binaries)
- **RLE2 encoding** — exponential zero-run coding using bijective base-2 numbering
- **Multi-threaded** — OpenMP block-level parallelism
- **Fast BWT** — embedded libdivsufsort for 2× faster suffix sorting
- **Pure C99** — minimal dependencies, compiles everywhere (Linux, macOS, Windows)
- **Simple API** — two functions: `mcx_compress()` and `mcx_decompress()`

## Building

```bash
# Standard build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Build shared library
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

**Requirements:** C99 compiler (GCC, Clang, MSVC), CMake ≥ 3.10. Optional: OpenMP for multi-threading.

## Installation

```bash
# Build and install (default: /usr/local)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build

# Custom prefix
sudo cmake --install build --prefix /opt/mcx
```

This installs:
- `mcx` binary → `<prefix>/bin/mcx`
- `libmaxcomp.a` → `<prefix>/lib/libmaxcomp.a`
- Headers → `<prefix>/include/maxcomp/`
- pkg-config → `<prefix>/lib/pkgconfig/maxcomp.pc`

### Shell Completions

```bash
# Bash
cp completions/mcx.bash /etc/bash_completion.d/mcx
# or: cp completions/mcx.bash ~/.local/share/bash-completion/completions/mcx

# Zsh
cp completions/mcx.zsh /usr/local/share/zsh/site-functions/_mcx

# Fish
cp completions/mcx.fish ~/.config/fish/completions/mcx.fish
```

### Man Page

```bash
sudo cp docs/mcx.1 /usr/local/share/man/man1/
sudo mandb  # rebuild man database
man mcx
```

## Usage

### Command Line

```bash
# Compress (default level 6 — good balance of speed and ratio)
mcx compress input.bin

# Maximum compression (Smart Mode — recommended for archival)
mcx compress -l 20 input.bin

# Decompress
mcx decompress input.bin.mcx

# Inspect files
mcx info archive.mcx          # detailed info
mcx ls *.mcx                   # compact multi-file listing
mcx diff old.mcx new.mcx      # compare two compressed files

# Multi-file
mcx compress *.txt             # compress all .txt files
mcx decompress *.mcx           # decompress all .mcx files

# Utilities
mcx verify archive.mcx         # integrity check
mcx verify archive.mcx orig    # verify against original
mcx cat archive.mcx            # decompress to stdout
mcx bench input.bin            # benchmark all levels
mcx test                       # self-test all levels

# Show help
mcx --help
```

### C API

```c
#include <maxcomp/maxcomp.h>

// Compress
size_t compressed_size = mcx_compress(dst, dst_capacity, src, src_size, level);
if (mcx_is_error(compressed_size)) {
    fprintf(stderr, "Error: %s\n", mcx_get_error_name(compressed_size));
}

// Decompress
size_t original_size = mcx_decompress(dst, dst_capacity, src, compressed_size);
```

### Python

```python
import maxcomp

data = open("input.bin", "rb").read()
compressed = maxcomp.compress(data, level=20)
restored = maxcomp.decompress(compressed)
assert restored == data
```

See [`bindings/python/`](bindings/python/) for setup instructions.

## Compression Levels

MCX provides a range of compression levels trading speed for ratio:

| Level | Strategy | Speed | Ratio | Best For |
|-------|----------|-------|-------|----------|
| 1–3 | LZ77 greedy + tANS | ●●●●○ | ●●○○○ | Real-time, streaming |
| 4–8 | LZ77 lazy + hash chains + FSE | ●●●○○ | ●●●○○ | General purpose |
| 9 | LZ77 lazy + adaptive arithmetic | ●●○○○ | ●●●●○ | Best LZ ratio |
| 10–14 | BWT + MTF + RLE2 + multi-rANS | ●○○○○ | ●●●●○ | Text, structured data |
| **20** | **Smart Mode (auto-detect)** | ●○○○○ | ●●●●● | **Recommended for max ratio** |
| 24 | LZRC fast (LZ + hash chains) | ●●○○○ | ●●●●○ | Fast binary compression |
| 26 | LZRC best (LZ + binary tree) | ●○○○○ | ●●●●● | Best for binary archives |

### Smart Mode (Level 20)

Smart Mode analyzes each input and automatically selects the optimal pipeline:

- **Structured binary** (spreadsheets, images, audio) → stride-delta preprocessing + RLE2 + rANS
- **Text** (ASCII, UTF-8) → BWT + MTF + RLE2 + multi-table rANS
- **x86 executables** → E8/E9 address filter + BWT pipeline
- **Mixed/generic** → multi-trial (tries BWT and LZ, keeps the smaller result)
- **Incompressible** → stored uncompressed (no expansion)

## Benchmarks

All benchmarks: single-threaded, in-memory, roundtrip-verified. System `gzip`, `bzip2`, and `xz` used for baseline measurements.

### Canterbury Corpus

<details>
<summary><strong>Text files</strong> — MCX beats bzip2 on large text, competitive on small</summary>

| File | Size | gzip -9 | bzip2 -9 | xz -6 | MCX L9 | MCX L20 |
|------|------|---------|----------|--------|--------|---------|
| alice29.txt | 152 KB | 2.81× | 3.52× | 3.14× | 2.42× | **3.53×** |
| asyoulik.txt | 125 KB | 2.56× | 3.16× | 2.81× | 2.34× | **3.15×** |
| lcet10.txt | 427 KB | 2.95× | 3.96× | 3.57× | 2.97× | **3.98×** |
| plrabn12.txt | 482 KB | 2.48× | 3.31× | 2.91× | 2.50× | **3.33×** |

</details>

<details>
<summary><strong>Binary files</strong> — kennedy.xls 50× compression (2.4× better than xz)</summary>

| File | Size | gzip -9 | bzip2 -9 | xz -6 | MCX L9 | MCX L20 |
|------|------|---------|----------|--------|--------|---------|
| kennedy.xls | 1.0 MB | 4.91× | 7.90× | 20.97× | 9.04× | **50.12×** |
| ptt5 | 513 KB | 9.80× | 10.31× | 12.22× | 8.65× | **10.19×** |
| sum | 38 KB | 2.99× | 2.96× | 4.05× | 2.62× | **2.84×** |

</details>

### Silesia Corpus (202 MB)

The [Silesia corpus](https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia) is the standard benchmark for evaluating compression algorithms on real-world data.

| File | Size | gzip -9 | bzip2 -9 | xz -9 | **MCX L20** | vs gzip | vs bzip2 | vs xz |
|------|------|---------|----------|-------|-------------|---------|----------|-------|
| dickens | 9.7 MB | 2.65× | 3.64× | 3.60× | **4.07×** | +35% | +11% | +12% |
| mozilla | 48.8 MB | 2.70× | 2.86× | 3.83× | **3.22×** | +16% | +11% | -19% |
| mr | 9.5 MB | 2.71× | 4.08× | 3.63× | **4.28×** | +37% | +5% | +15% |
| nci | 32.0 MB | 11.23× | 18.51× | 19.30× | **25.65×** | +56% | +28% | +25% |
| ooffice | 5.9 MB | 1.99× | 2.15× | 2.54× | **2.56×** | +22% | +16% | +1% |
| osdb | 9.6 MB | 2.71× | 3.60× | 3.54× | **4.04×** | +33% | +11% | +12% |
| reymont | 6.3 MB | 3.64× | 5.32× | 5.03× | **5.93×** | +39% | +10% | +15% |
| samba | 20.6 MB | 4.00× | 4.75× | 5.74× | **5.05×** | +21% | +6% | -14% |
| sao | 6.9 MB | 1.36× | 1.47× | 1.64× | **1.48×** | +8% | +1% | -11% |
| webster | 39.5 MB | 3.44× | 4.80× | 4.94× | **5.81×** | +41% | +17% | +15% |
| xml | 5.1 MB | 8.07× | 12.12× | 11.79× | **12.86×** | +37% | +6% | +8% |
| x-ray | 8.1 MB | 1.40× | 2.09× | 1.89× | **2.15×** | +35% | +3% | +12% |
| **TOTAL** | **202 MB** | **3.13×** | **3.89×** | **4.34×** | **4.35×** | **+28%** | **+11%** | **≈** |

**Summary:**
- Beats **gzip -9** on 12/12 files (100%) — 28% smaller on average
- Beats **bzip2 -9** on 12/12 files (100%) — 11% smaller on average
- Beats **xz -9** on 9/12 files (75%) — ties on total corpus
- xz leads on 3 binary-heavy files (mozilla, samba, sao) where LZMA2's position-dependent context modeling has an edge

> **v2.1:** LZRC engine (LZ + range coder) with embedded libdivsufsort (BWT 2× faster). Use `mcx compress -l 26` for LZRC mode, or `-l 24` for 3× faster LZRC.

### Speed

Single-threaded on Intel Xeon E-2386G @ 3.50 GHz:

| Level | Compress | Decompress | Notes |
|-------|----------|------------|-------|
| L3 | 4–9 MB/s | 12–36 MB/s | LZ77 + tANS |
| L9 | 2–4 MB/s | 3–14 MB/s | LZ77 + adaptive AC |
| L12 | 0.3–12 MB/s | 3–20 MB/s | BWT + multi-rANS |
| L20 | 0.1–0.3 MB/s | 2.8–5 MB/s | Smart Mode (multi-trial) |

MCX prioritizes compression ratio over speed. For speed-critical applications, use level 3. For maximum compression, use level 20.

## Architecture

```
Input → [Block Analyzer] → Strategy Selection
                               │
         ┌─────────┬──────────┼──────────┬──────────┐
         ▼         ▼          ▼          ▼          ▼
    LZ Pipeline  BWT Pipe  Stride-Δ   LZRC-HC    LZRC-BT
    (L1–L9)      (L10–19)  (L20 auto) (L24)      (L26)
         │         │          │          │          │
    LZ77 Match  divsufsort  Delta @   HC Match   BT Match
    Finding     +MTF+RLE2   stride    Finder     Finder
         │         │          │          │          │
    tANS/FSE/   Multi-tbl  RLE2+rANS  Adaptive  Adaptive
    Adaptive AC  rANS                  Range RC   Range RC
         │         │          │          │          │
         └─────────┴──────────┼──────────┴──────────┘
                               ▼
                    [Block Multiplexer]
                    OpenMP Parallelism
                               ▼
                         .mcx output
```

### Algorithm Overview

| Component | Description |
|-----------|-------------|
| **BWT (SA-IS)** | O(n) suffix-array induced sorting for Burrows-Wheeler Transform |
| **Multi-table rANS** | 4–6 frequency tables with K-means clustering (group size 50), within 0.01 bits/symbol of entropy |
| **Adaptive AC** | Order-1 arithmetic coding with Fenwick-tree accelerated decoding |
| **Stride-Delta** | Auto-detects fixed-width records (1–512 byte stride) via correlation analysis |
| **RLE2** | Bijective base-2 zero-run encoding — log₂(N) symbols for N zeros |
| **E8/E9 filter** | x86 CALL/JMP relative-to-absolute address conversion |
| **tANS/FSE** | 4-stream interleaved table ANS for fast LZ decompression |
| **Genetic optimizer** | Evolves pipeline configuration per block at L10–L14 |

## File Format

MCX uses a frame-based format with a 20-byte header and variable-size blocks. See [`docs/FORMAT.md`](docs/FORMAT.md) for the full specification.

- Magic: `MCX\x01` (`0x0158434D`)
- Block size: up to 64 MB
- Per-block strategy selection
- CRC32 header checksum
- Optional E8/E9 preprocessing flag

## Project Structure

```
maxcomp/
├── include/maxcomp/    Public API (maxcomp.h)
├── lib/
│   ├── entropy/        tANS, FSE, rANS, multi-rANS, adaptive AC, range coder
│   ├── lz/             LZ77 (greedy, lazy HC, LZ24), binary tree match finder
│   ├── preprocess/     BWT, MTF, RLE, RLE2, delta, E8/E9 filter
│   ├── babel/          Stride-delta transform
│   ├── optimizer/      Genetic pipeline optimizer
│   ├── analyzer/       Block analysis (entropy, structure, stride detection)
│   └── simd/           SSE4.1 / AVX2 helpers
├── cli/                Command-line tool (mcx)
├── bindings/python/    Python ctypes bindings
├── tests/              Unit tests, roundtrip tests, comprehensive suite (204 tests)
├── docs/               Format spec, design docs, research notes
└── CMakeLists.txt
```

## Roadmap

### v1.x (Current)
- [x] BWT + multi-table rANS — beats bzip2 on all standard benchmarks
- [x] Adaptive arithmetic coding on LZ output — best-in-class LZ ratios
- [x] Smart Mode with stride-delta, E8/E9, multi-trial
- [x] OpenMP block parallelism

### v2.0 (In Development)
- [x] Range coder foundation (Subbotin-style, verified)
- [x] Binary tree match finder (16 MB window)
- [x] LZ + range coder prototype — mozilla 3.07× (+5% vs v1.x)
- [ ] Optimal parsing (price-based match selection)
- [ ] Context-mixed literal coding
- [ ] Integration as new block type with multi-trial

### Future
- [ ] ARM/ARM64 BCJ filter
- [ ] Streaming API for arbitrary-length input
- [ ] WASM build for browser usage

## Contributing

Contributions are welcome. Please ensure all changes pass the roundtrip test suite:

```bash
cd build && ctest --output-on-failure
```

For compression ratio changes, include before/after benchmarks on the Canterbury and Silesia corpora.

## License

[GNU General Public License v3.0](LICENSE)

Free for everyone, forever. GPL-3.0 ensures the library and all derivatives remain open source.

---

<sub>MaxCompression is developed by [Dreams-Makers Studio](https://github.com/Dreams-Makers-Studio).</sub>
