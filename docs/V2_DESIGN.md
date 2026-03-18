# MCX v2.0 Design — LZ + Context-Mixed Range Coder

**Status:** In development  
**Goal:** Close the compression gap with xz/LZMA2 on binary data

## Motivation

MCX v1.x uses BWT for high-ratio compression. BWT excels on text and structured data, but has an inherent limitation on binary archives: it requires the entire block to be sorted, limiting the effective "context" to the block size. LZMA2 (used by xz) uses a sliding window up to 64 MB with context-mixed entropy coding, which is fundamentally better for binary data with long-range repetitions.

**Current gaps (v1.9.3 vs xz -9):**
- mozilla: 2.93× vs 3.83× (-24%)
- samba: 5.03× vs 5.74× (-12%)
- sao: 1.48× vs 1.64× (-10%)

## Architecture

```
Input → Binary Tree Match Finder (16 MB+ window)
              │
              ├─ Match: encode (length, distance) with adaptive models
              │   ├─ Length: bit-tree coding (4–273)
              │   ├─ Distance: slot-based (64 slots + extra bits + alignment)
              │   └─ Rep match: reuse last distance (1 bit)
              │
              └─ Literal: context-dependent byte coding
                  └─ 16 contexts (prev_byte >> 5, after_match flag)
              │
              ▼
         Adaptive Range Coder (Subbotin-style, carry-based)
```

## Components

### Binary Tree Match Finder (`lib/lz/bt_match.c`)
- Position-indexed binary search tree
- O(log n) match finding per position
- Configurable window (up to 64 MB) and tree depth
- Hash table for 4-byte prefix → tree root

### Range Coder (`lib/entropy/range_coder.c`)
- 32-bit range, carry-based output
- Bit-tree encoding for all symbols
- 11-bit probability with shift-5 adaptation

### Distance Model (`lib/entropy/lz_models.h`)
- 64 distance slots (covers 64 MB+ offsets)
- Slots 0–3: no extra bits
- Slots 4–17: 1–6 context-coded extra bits
- Slots 18+: direct bits (fixed 50/50) + 4 alignment bits

### LZRC Compressor (`lib/lz/lzrc.c`)
- Complete encoder + decoder with verified roundtrip
- Rep match support (last distance reuse)
- Context: 256 literal contexts + 16 post-match contexts

## Prototype Results

With 1 MB window (w=20) and 16 MB window (w=24):

| File | L9 (AAC) | L20 (BWT) | LZRC v2.0 | xz -9 |
|------|----------|-----------|-----------|-------|
| mozilla | 2.60× | 2.93× | **3.07×** | 3.83× |
| samba | 3.64× | 5.03× | 4.90× | 5.74× |
| ooffice | 1.86× | 2.53× | 2.16× | 2.54× |
| dickens | 2.34× | 4.07× | 3.13× | 3.60× |
| alice29 | 2.34× | 3.53× | 2.93× | 3.14× |

**Key finding:** LZRC beats L20 BWT on mozilla (+5%) — the first time any LZ approach in MCX outperforms BWT on a large binary file.

## Development Phases

### Phase 1: Foundation ✅
- [x] Range coder (Subbotin-style, carry-based)
- [x] Bit-tree encoding/decoding
- [x] Distance slot model (64 slots)
- [x] Basic length model (3-tier: short/medium/extra)

### Phase 2: Match Finder ✅
- [x] Binary tree match finder
- [x] Configurable window size (up to 64 MB)
- [x] Hash-based initial lookup + tree walk

### Phase 3: Integration ✅
- [x] LZRC encoder with rep matches
- [x] LZRC decoder with full roundtrip verification
- [x] Context-dependent literal coding

### Phase 4: Optimization (Next)
- [ ] Optimal parsing (price-based match/literal decisions)
- [ ] Multiple rep distances (rep0, rep1, rep2, rep3)
- [ ] Match-distance-dependent literal contexts
- [ ] Lazy evaluation with 2-byte lookahead

### Phase 5: Integration into MCX
- [ ] New block type for LZRC data
- [ ] Multi-trial at L20: try BWT, LZRC, keep smaller
- [ ] E8/E9 filter before LZRC for executables

### Phase 6: Advanced (Future)
- [ ] Context-mixed literal coding (use match distance as context)
- [ ] Aligned offset coding for specific distance ranges
- [ ] Position-dependent models for heterogeneous data
