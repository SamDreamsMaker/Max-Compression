<p align="center">
  <h1 align="center">MaxCompression</h1>
  <p align="center">
    <strong>High-ratio lossless data compression library and CLI</strong>
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

MaxCompression (MCX) is a lossless compression library and CLI written in portable C99. It combines multiple compression strategies — LZ77 with adaptive entropy coding, Burrows-Wheeler Transform with multi-table rANS, LZRC (LZ + range coder), and stride-delta preprocessing — under a unified API that automatically selects the best pipeline for each data type.

MCX targets **maximum compression ratio** while maintaining practical speeds. It beats bzip2 on 100% of standard benchmark files and competes with xz/LZMA2 on most data types.

## Highlights

| Metric | MCX | Best Alternative |
|--------|-----|-----------------|
| kennedy.xls (structured binary) | **50.1×** | xz: 21.0× — **2.4× better** |
| nci (chemical text, 33 MB) | **25.7×** | xz: 19.3× — **33% better** |
| alice29.txt (English text) | **3.53×** | bzip2: 3.52× — **beats bzip2** |
| mozilla (50 MB binary archive) | **3.22×** | xz: 3.55× — **91% of xz** |
| enwik8 (100 MB Wikipedia) | **4.04×** | xz: 3.89× — **beats xz by 4%** |
| Silesia corpus (202 MB total) | **4.35×** | bzip2: 3.89× — **+12%** |

## Features

### Compression Engines
- **Smart Mode (L20)** — automatically detects data type and selects the optimal pipeline
- **LZ77** (L1–L9) — fast compression with greedy/lazy matching and hash chain match finders
- **BWT + multi-table rANS** (L10–L14) — Burrows-Wheeler Transform with K-means clustered frequency tables
- **LZRC v2.0** (L24–L26) — LZ + adaptive range coder with binary tree or hash chain match finder, LZMA-style matched literal coding, 4-state machine, rep-match distances
- **Stride-Delta** — auto-detects fixed-width records (1–512 byte stride) for structured binary data

### Entropy Coding
- **Multi-table rANS** — 4–6 frequency tables with K-means clustering, within 0.01 bits/symbol of entropy
- **Adaptive Arithmetic Coding** — order-1 AC with Fenwick-tree accelerated decoding (O(log n) per symbol)
- **Adaptive Range Coder** — bit-level context modeling with matched literal coding for LZRC
- **tANS/FSE** — 4-stream interleaved table ANS for fast LZ decompression

### Preprocessing
- **E8/E9 x86 filter** — CALL/JMP address normalization (+16% on x86 binaries)
- **RLE2** — bijective base-2 zero-run encoding (log₂(N) symbols for N zeros)
- **Genetic optimizer** — evolves pipeline configuration per block at L10–L14

### CLI
- **30+ subcommands** — compress, decompress, verify, diff, bench, stat, hash, checksum, upgrade, pipe, and more
- **Multi-file and recursive** — `mcx compress -r ./data/` with glob exclusion patterns
- **Rich benchmarking** — JSON/CSV/Markdown output, `--compare` against gzip/bzip2/xz, `--aggregate` for directories
- **Decompress aliases** — `mcx x`, `mcx d`, `mcx extract`
- **Shell completions** — Bash, Zsh, Fish

### Library
- **Simple C API** — `mcx_compress()`, `mcx_decompress()`, `mcx_get_frame_info()`
- **Python bindings** — ctypes-based, pip-installable
- **OpenMP parallel** — block-level parallelism, configurable thread count
- **Pure C99** — no C++ dependency, compiles with GCC, Clang, MSVC
- **Cross-platform** — Linux, macOS, Windows

## Quick Start

```bash
# Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Compress
./build/bin/mcx compress myfile.txt                # fast (L3)
./build/bin/mcx compress --best myfile.txt          # max compression (L20)

# Decompress
./build/bin/mcx decompress myfile.txt.mcx

# Benchmark
./build/bin/mcx bench myfile.txt
./build/bin/mcx bench --compare mydir/              # vs gzip/bzip2/xz

# Run tests
cd build && ctest --output-on-failure
```

## Installation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build       # installs to /usr/local

# Shell completions
cp completions/mcx.bash ~/.local/share/bash-completion/completions/mcx
cp completions/mcx.zsh /usr/local/share/zsh/site-functions/_mcx
cp completions/mcx.fish ~/.config/fish/completions/mcx.fish

# Man page
sudo cp docs/mcx.1 /usr/local/share/man/man1/
```

**Requirements:** C99 compiler (GCC, Clang, MSVC), CMake ≥ 3.10. Optional: OpenMP for multi-threading.

## Usage

### CLI

```bash
# Compression
mcx compress input.bin                     # default level (L3)
mcx compress -l 20 input.bin               # max compression
mcx compress --fast input.bin              # L3
mcx compress --best input.bin              # L20 Smart Mode
mcx compress -l 26 binary.bin              # LZRC (best for binaries)

# Multi-file & recursive
mcx compress *.txt                         # compress all .txt files
mcx compress -r ./data/ --exclude "*.log"  # recursive with exclusion
mcx decompress *.mcx                       # decompress all

# Inspection
mcx info archive.mcx                       # detailed frame info
mcx info --blocks archive.mcx              # per-block details
mcx ls *.mcx                               # compact multi-file listing
mcx diff old.mcx new.mcx                   # compare two archives
mcx stat rawfile.bin                       # entropy and byte distribution

# Integrity
mcx verify archive.mcx                     # decompress and verify CRC
mcx verify archive.mcx original.bin        # verify against original
mcx checksum archive.mcx                   # verify header CRC32
mcx hash archive.mcx                       # CRC32/FNV of content

# Utilities
mcx cat archive.mcx                        # decompress to stdout
mcx cat archive.mcx | head -c 1024        # pipe first 1KB
mcx pipe -l 6 < input > output.mcx        # stdin/stdout mode
mcx upgrade -l 20 --in-place old.mcx      # recompress at higher level

# Benchmarking
mcx bench input.bin                        # all default levels
mcx bench --all-levels input.bin           # L1-L26
mcx bench --compare input.bin             # vs gzip/bzip2/xz
mcx bench -r ./corpus/ --aggregate        # directory totals
mcx bench --format json input.bin         # JSON output
mcx bench --format csv input.bin          # CSV output
mcx bench --format markdown input.bin     # Markdown table

# Advanced
mcx compress --decompress-check input.bin  # roundtrip verify in memory
mcx compress --atomic input.bin            # crash-safe write
mcx compress --preserve-mtime input.bin    # preserve timestamps
mcx compress --dry-run input.bin           # analyze without writing
mcx compress --estimate input.bin          # fast size estimate
mcx compress --adaptive-level input.bin    # entropy-based auto level

# Self-test
mcx test                                   # built-in roundtrip tests
mcx version --build                        # detailed build info
```

### C API

```c
#include <maxcomp/maxcomp.h>

// Compress
size_t bound = mcx_compress_bound(src_size);
uint8_t* dst = malloc(bound);
size_t comp_size = mcx_compress(dst, bound, src, src_size, 20);
if (mcx_is_error(comp_size)) {
    fprintf(stderr, "Error: %s\n", mcx_get_error_name(comp_size));
}

// Decompress
size_t orig_size = mcx_decompress(out, out_cap, dst, comp_size);

// Inspect
mcx_frame_info info;
mcx_get_frame_info(dst, comp_size, &info);
printf("Original: %zu, Level: %d\n", info.original_size, info.level);

// Version
printf("MCX %s\n", mcx_version_string());
```

### Python

```python
import maxcomp

data = open("input.bin", "rb").read()
compressed = maxcomp.compress(data, level=20)
restored = maxcomp.decompress(compressed)
assert restored == data

info = maxcomp.get_frame_info(compressed)
print(f"Original: {info['original_size']}, Level: {info['level']}")
```

## Compression Levels

| Level | Strategy | Compress Speed | Decompress Speed | Use Case |
|-------|----------|---------------|------------------|----------|
| 1–3 | LZ77 greedy + tANS | ~5–10 MB/s | ~15–35 MB/s | Real-time, streaming |
| 6 | LZ77 lazy + rANS | ~3–5 MB/s | ~10–20 MB/s | General purpose |
| 7–9 | LZ77 lazy + adaptive AC | ~2–4 MB/s | ~3–14 MB/s | Best LZ ratio |
| 10–14 | BWT + MTF + RLE2 + multi-rANS | ~1–3 MB/s | ~5–10 MB/s | Text, structured data |
| **20** | **Smart Mode (auto-detect)** | ~0.3–1 MB/s | ~3–7 MB/s | **Maximum compression** |
| 24 | LZRC fast (hash chains) | ~1–2 MB/s | ~4–5 MB/s | Fast binary compression |
| 26 | LZRC best (binary tree) | ~0.3–0.5 MB/s | ~4–5 MB/s | Best for binary data |

**Shortcuts:** `--fast` (L3), `--default` (L6), `--best` (L20)

### Smart Mode (Level 20)

Analyzes each block and automatically routes to the best pipeline:

- **Structured binary** (spreadsheets, audio) → stride-delta + RLE2 + rANS → kennedy.xls **50×**
- **Text** (UTF-8, source code) → BWT + MTF + RLE2 + multi-rANS → alice29 **3.53×**
- **x86 executables** → E8/E9 filter + BWT → ooffice **2.56×**
- **Mixed/binary** → multi-trial (tries BWT, LZ, LZRC, keeps smallest)
- **Incompressible** → stored uncompressed (no expansion)

## Benchmarks

Single-threaded, in-memory, roundtrip-verified. System gzip, bzip2, and xz for baselines.

### Canterbury Corpus

| File | Size | gzip -9 | bzip2 -9 | xz -6 | **MCX L20** | Winner |
|------|------|---------|----------|--------|-------------|--------|
| alice29.txt | 152 KB | 2.81× | 3.52× | 3.14× | **3.53×** | **MCX** |
| asyoulik.txt | 125 KB | 2.56× | 3.16× | 2.81× | **3.15×** | bzip2 ≈ MCX |
| lcet10.txt | 427 KB | 2.95× | 3.96× | 3.57× | **3.98×** | **MCX** |
| plrabn12.txt | 482 KB | 2.48× | 3.31× | 2.91× | **3.33×** | **MCX** |
| kennedy.xls | 1.0 MB | 4.91× | 7.90× | 20.97× | **50.1×** | **MCX** (2.4× better than xz) |
| ptt5 | 513 KB | 9.80× | 10.31× | 12.22× | **10.19×** | xz |

### Silesia Corpus (202 MB)

The standard benchmark for evaluating compression on real-world data.

| File | Size | gzip -9 | bzip2 -9 | xz -9 | **MCX L20** | vs bzip2 | vs xz |
|------|------|---------|----------|-------|-------------|----------|-------|
| dickens | 9.7 MB | 2.65× | 3.64× | 3.60× | **4.07×** | +12% | +13% |
| mozilla | 48.8 MB | 2.70× | 2.86× | 3.83× | **3.22×** | +13% | -16% |
| mr | 9.5 MB | 2.71× | 4.08× | 3.63× | **4.28×** | +5% | +18% |
| nci | 32.0 MB | 11.23× | 18.51× | 19.30× | **25.65×** | +39% | +33% |
| ooffice | 5.9 MB | 1.99× | 2.15× | 2.54× | **2.56×** | +19% | +1% |
| osdb | 9.6 MB | 2.71× | 3.60× | 3.54× | **4.04×** | +12% | +14% |
| reymont | 6.3 MB | 3.64× | 5.32× | 5.03× | **5.93×** | +11% | +18% |
| samba | 20.6 MB | 4.00× | 4.75× | 5.74× | **5.05×** | +6% | -12% |
| sao | 6.9 MB | 1.36× | 1.47× | 1.64× | **1.48×** | +1% | -10% |
| webster | 39.5 MB | 3.44× | 4.80× | 4.94× | **5.81×** | +21% | +18% |
| xml | 5.1 MB | 8.07× | 12.12× | 11.79× | **12.86×** | +6% | +9% |
| x-ray | 8.1 MB | 1.40× | 2.09× | 1.89× | **2.15×** | +3% | +14% |
| **Total** | **202 MB** | **3.13×** | **3.89×** | **4.34×** | **4.35×** | **+12%** | **≈** |

**Score: MCX beats gzip 12/12, bzip2 12/12, xz 9/12.**

xz leads on 3 binary-heavy files (mozilla, samba, sao) where LZMA2's large-window optimal parsing has an advantage. MCX's LZRC engine (L26) narrows this gap: mozilla 3.22× vs xz 3.55×.

### Large Files

| File | Size | xz -9 | **MCX L20** | Notes |
|------|------|-------|-------------|-------|
| enwik8 | 95.4 MB | 3.89× | **4.04×** | Wikipedia — **beats xz by 4%** |
| enwik9 | 953 MB | 4.12× | **4.28×** | 1 GB Wikipedia dump |

## Architecture

```
Input → [Block Analyzer] → Strategy Selection
                               │
         ┌─────────┬──────────┼──────────┬──────────┐
         ▼         ▼          ▼          ▼          ▼
    LZ Pipeline  BWT Pipe  Stride-Δ   LZRC-HC    LZRC-BT
    (L1–L9)      (L10–14)  (L20 auto) (L24)      (L26)
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

## File Format

MCX uses a frame-based format with a 20-byte header and variable-size blocks (up to 64 MB). See [`docs/FORMAT.md`](docs/FORMAT.md) for the full specification.

## Project Stats

- **~17,400 lines** of C code
- **478+ commits** across the project
- **21 test suites** — unit tests, roundtrip, fuzz, stress, regression, integration
- **CI** — Linux (GCC + Clang), macOS, Windows, Valgrind, coverage, Python bindings

## Project Structure

```
maxcomp/
├── include/maxcomp/    Public API (maxcomp.h)
├── lib/
│   ├── entropy/        tANS, FSE, rANS, multi-rANS, adaptive AC, range coder
│   ├── lz/             LZ77, LZRC v2.0, binary tree + hash chain match finders
│   ├── preprocess/     BWT (divsufsort), MTF, RLE2, delta, E8/E9 filter
│   ├── babel/          Stride-delta transform
│   ├── optimizer/      Genetic pipeline optimizer
│   ├── analyzer/       Block analysis (entropy, structure, stride detection)
│   ├── external/       Embedded libdivsufsort (MIT license)
│   └── compat.h        Cross-platform portability layer
├── cli/                Command-line tool (30+ subcommands)
├── bindings/python/    Python ctypes bindings
├── completions/        Bash, Zsh, Fish shell completions
├── tests/              21 test suites (unit, integration, fuzz, stress)
├── docs/               Format spec, API docs, benchmarks, man page
├── valgrind.supp       Valgrind suppressions
└── CMakeLists.txt
```

## Documentation

- **[FORMAT.md](docs/FORMAT.md)** — MCX file format specification
- **[API.md](docs/API.md)** — C API reference
- **[DESIGN.md](docs/V2_DESIGN.md)** — v2.0 architecture and design decisions
- **[BENCHMARKS.md](docs/BENCHMARKS.md)** — Comprehensive benchmark tables
- **[ROADMAP.md](docs/ROADMAP.md)** — Development roadmap and research log
- **[CHANGELOG.md](CHANGELOG.md)** — Version history
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — Contribution guidelines
- **`man mcx`** — Man page (installed with `cmake --install`)

## Roadmap

### Completed ✅
- BWT + multi-table rANS — beats bzip2 on all standard benchmarks
- Adaptive arithmetic coding on LZ output — best-in-class LZ ratios
- Smart Mode (L20) with stride-delta, E8/E9, multi-trial strategy selection
- LZRC v2.0 — LZ + range coder with BT/HC match finders, rep-matches, matched literals
- OpenMP block parallelism
- Embedded libdivsufsort (2× faster BWT)
- Rich CLI with 30+ commands, multi-file, recursive, benchmarking
- Python bindings with pip install support
- Cross-platform CI (Linux, macOS, Windows)

### Future
- [ ] Context-mixed literal coding for LZRC
- [ ] ARM/ARM64 BCJ filter
- [ ] Streaming API for arbitrary-length input
- [ ] WASM build for browser usage
- [ ] v3.0 format: Huffman-coded LZ tokens (close gap with gzip at same speed)

## Contributing

Contributions are welcome! Please ensure all changes pass the test suite:

```bash
cd build && ctest --output-on-failure
```

For compression ratio changes, include before/after benchmarks on Canterbury and Silesia corpora. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[GNU General Public License v3.0](LICENSE) — Free for everyone, forever.

---

<sub>MaxCompression is developed by [Dreams-Makers Studio](https://github.com/Dreams-Makers-Studio).</sub>
