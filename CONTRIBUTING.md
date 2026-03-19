# Contributing to MaxCompression

Thank you for your interest in contributing to MaxCompression! This document provides guidelines and information for contributors.

## Getting Started

### Prerequisites

- C99-compatible compiler (GCC ≥ 7, Clang ≥ 6, MSVC ≥ 2019)
- CMake ≥ 3.10
- Git
- Optional: OpenMP for multi-threaded compression

### Building from Source

```bash
git clone https://github.com/SamDreamsMaker/Max-Compression.git
cd Max-Compression
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DMCX_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

### Running Tests

```bash
cd build && ctest --output-on-failure
```

For comprehensive roundtrip tests across all levels:

```bash
./bin/test_lzrc          # LZRC v2.0 tests
./bin/test_roundtrip     # Core roundtrip tests
./bin/test_bwt_levels    # BWT path tests (L10-L22)
```

## How to Contribute

### Reporting Bugs

- Open an issue on GitHub with a clear title and description
- Include: MCX version, OS, compiler, and steps to reproduce
- If possible, attach the input file that triggers the bug
- For compression bugs: include the compression level used

### Suggesting Features

- Open a discussion or issue describing the feature
- Explain the use case and expected behavior
- Reference relevant compression literature if applicable

### Submitting Code

1. **Fork** the repository
2. **Create a branch** from `main`: `git checkout -b feature/my-feature`
3. **Write code** following the style guide below
4. **Add tests** — all changes must include roundtrip verification
5. **Run the full test suite** — no regressions allowed
6. **Submit a pull request** with a clear description

## Code Style

### C Code

- **C99 standard** — no GNU extensions in library code (CLI may use POSIX)
- **4-space indentation**, no tabs
- **`snake_case`** for functions and variables
- **`MCX_` prefix** for all public symbols
- **`mcx_` prefix** for all public functions
- **Braces** on the same line for control structures
- **Comments**: `/* C89-style */` for multi-line, `//` allowed for single-line in .c files

```c
/* Good */
size_t mcx_lzrc_compress(uint8_t* dst, size_t dst_cap,
                          const uint8_t* src, size_t src_size,
                          int window_log, int bt_depth) {
    if (!dst || !src || src_size == 0) return 0;
    
    for (size_t i = 0; i < src_size; i++) {
        /* Process byte */
    }
    return result;
}
```

### File Organization

| Directory | Purpose |
|-----------|---------|
| `include/maxcomp/` | Public API only (`maxcomp.h`) |
| `lib/` | Library implementation (internal) |
| `lib/entropy/` | Entropy coders (rANS, FSE, AC, range coder) |
| `lib/lz/` | LZ match finders and compressors |
| `lib/preprocess/` | Transforms (BWT, MTF, RLE, delta, E8/E9) |
| `cli/` | Command-line tool |
| `tests/` | Test files (registered via CMake `add_test`) |
| `docs/` | Documentation and specifications |

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: Add ARM64 BCJ filter
fix: Correct BWT inverse for blocks > 32MB
perf: Optimize multi-rANS K-means clustering (-36% time)
docs: Update FORMAT.md with LZRC block type
test: Add LZRC roundtrip tests
refactor: Extract distance model into lz_models.h
```

## Pull Request Checklist

Before submitting a PR, verify:

- [ ] Code compiles clean with `-Wall -Wextra` (no new warnings)
- [ ] All existing tests pass: `cd build && ctest --output-on-failure`
- [ ] Roundtrip verified: compressed → decompressed matches original exactly
- [ ] New tests added for any new functionality
- [ ] No compression ratio regression on Canterbury/Silesia corpora
- [ ] Commit messages follow Conventional Commits format
- [ ] Documentation updated (README, man page, CHANGELOG) if user-facing
- [ ] No hardcoded paths or platform-specific code in library (CLI may use POSIX)
- [ ] Memory: no leaks under valgrind for typical usage paths

## Testing Requirements

### All Changes Must:

1. **Pass roundtrip** — compressed data must decompress to the exact original
2. **Not regress** — no compression ratio decrease on Canterbury or Silesia corpora
3. **Handle edge cases** — empty input, single byte, incompressible data, max-size blocks

### For Compression Changes:

Include before/after benchmarks:

```
File          Before    After     Change
alice29.txt   3.53×     3.55×     +0.6%
mozilla       2.93×     2.93×     =
```

### For Speed Changes:

Include timing measurements:

```
File          Before        After
mozilla       0.3 MB/s      0.4 MB/s (+33%)
```

## Architecture Notes

### Adding a New Compression Strategy

1. Define the strategy in `lib/internal.h` (`MCX_STRATEGY_*`)
2. Add compression logic in `lib/core.c` (block loop)
3. Add decompression logic in `lib/core.c` (block type dispatch)
4. Assign a block type byte (see `docs/FORMAT.md`)
5. Add to multi-trial at L20 if appropriate
6. Add roundtrip tests in `tests/`
7. Update `docs/FORMAT.md` with the new block type

### Adding a New Entropy Coder

1. Create files in `lib/entropy/`
2. Add to `lib/CMakeLists.txt`
3. Integrate as a new option in the relevant compression path
4. Signal via the genome byte or block type byte

## License

By contributing, you agree that your contributions will be licensed under the GPL-3.0 license.

## Questions?

Open an issue or discussion on GitHub. We're happy to help!
