# MCX v2.0 Design — Closing the Gap to xz/LZMA2

## Current State (v1.9.3)

MCX beats bzip2 on 100% of tested files and xz on 75%.
The remaining gaps are on binary/mixed data:
- mozilla: 2.93× vs xz 3.83× (-24%)
- samba: 5.03× vs xz 5.74× (-12%)
- sao: 1.48× vs xz 1.64× (-10%)

## Why xz Wins on Binary

LZMA2 has three structural advantages:

### 1. Large Dictionary (64MB+)
- LZMA2 uses a sliding window up to 64MB-1.5GB
- Matches references up to 1.5GB apart
- MCX BWT is limited to block size (64MB) — but this is comparable

### 2. Context-Mixed Literal Coding
- After a match, LZMA2 predicts literals using:
  - Previous byte
  - Match distance (literals near recently-matched positions are correlated)
  - Position context
- MCX's AAC only uses previous byte (order-1)
- This is worth ~5-15% on binary data

### 3. Match-Length/Distance Context
- LZMA2 uses adaptive probability models for match lengths and distances
- Separate probability tables based on match position and length range
- MCX uses fixed-width fields (2-byte offsets, varint lengths)

## v2.0 Architecture Options

### Option A: LZ + Context Mixer (LZMA2-like)
Replace the LZ+AAC pipeline with a proper context-mixed LZ codec:
- **Pros**: Directly addresses the gap, proven approach
- **Cons**: Major rewrite, months of work, reinventing LZMA2
- **Estimated gain**: +15-25% on binary files

### Option B: PPM (Prediction by Partial Matching)
Context-based byte prediction with escape mechanism:
- **Pros**: Simple concept, excellent on text
- **Cons**: Slow, high memory, doesn't help binary much
- **Estimated gain**: +5% text, 0% binary

### Option C: Asymmetric Numeral Systems + Context Mixing
Keep the ANS backend but add:
1. Order-2/3 context model for BWT output → better than multi-rANS
2. Match-dependent literal prediction for LZ output
- **Pros**: Incremental improvement, keeps existing architecture
- **Cons**: Limited gains (~5%)

### Option D: Neural Network Compression (cmix-like)
Ensemble of context models + mixing:
- **Pros**: Potentially SOTA compression ratio
- **Cons**: Extremely slow (KB/s), impractical for general use

## Recommended Path: Option A (LZ + Context Mixer)

### Phase 1: Range Coder Foundation (2 weeks)
- Implement byte-aligned range coder (32-bit state)
- Bit-level encoding with adaptive binary models
- Probability update with exponential smoothing

### Phase 2: LZ Match Finder (3 weeks)
- Binary tree match finder (O(log n) per position)
- Optional hash chains for speed mode
- 64MB-256MB sliding window

### Phase 3: Context-Mixed Literal Coding (4 weeks)
- Literal coding using:
  - High nibble/low nibble split
  - Match distance context
  - Previous byte context
  - Position-dependent context
- Context mixing via logistic mixing (weighted average in log domain)

### Phase 4: Distance/Length Models (2 weeks)
- Distance slots (like LZMA2's 64 distance slots)
- Distance alignment bits
- Length encoding with context-dependent models

### Phase 5: Integration & Testing (3 weeks)
- New block type for LZ-CM output
- Multi-trial: L20 tries BWT, LZ-CM, LZ-AAC, keeps best
- Full corpus testing

### Total: ~3 months of focused work

## Alternative Quick Wins (v1.9.x)

These could be done before v2.0:
1. **ELF/Alpha BCJ filter**: Like E8/E9 but for ELF binaries → helps mozilla
2. **Parallel BWT**: Multi-threaded SA-IS for faster compression
3. **divsufsort integration**: 2-3× faster BWT construction
4. **WASM port**: Browser-based compression/decompression
