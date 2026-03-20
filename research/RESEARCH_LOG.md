# MCX Research Log — Breakthrough Compression

## Philosophy
We're not looking for +0.5%. We're looking for paradigm shifts.
Every known compressor uses the same 3 ideas from the 1970s-90s (LZ77, BWT, AC/ANS).
We explore what NO ONE has tried.

## Exploration Tracks

### Track 1: Learned Context Modeling (Priority: HIGH)
**Hypothesis:** Current compressors use fixed context models (prev byte, position, state).
What if we LEARN the optimal context model from the data itself?

**Approach:** Two-pass compression:
1. Pass 1: Analyze data, build optimal context → prediction mapping
2. Pass 2: Encode using the learned model
3. Store the model compactly in the header

**Why it could work:** Fixed contexts waste model capacity. A 2-byte context has 65536 
states but most are unused. Learned contexts could achieve same prediction quality with 
100x fewer states, leaving more capacity for the states that matter.

**Status:** NOT STARTED

### Track 2: Recursive Compression / Kolmogorov Approximation
**Hypothesis:** After BWT+MTF+RLE2, the output has residual structure that rANS doesn't 
fully capture. What if we compress the COMPRESSED data again, looking for higher-order 
patterns?

**Status:** NOT STARTED

### Track 3: Cross-Block Prediction
**Hypothesis:** Current compressors treat blocks independently. But blocks share patterns 
(same vocabulary, same structures). Sharing context models across blocks could improve 
ratio especially on large files.

**Status:** NOT STARTED

### Track 4: Spectral / Transform-Based Compression
**Hypothesis:** BWT is the only "transform" in text compression. But there might be 
other transforms (DCT-like, wavelet-like) that work on byte streams and expose 
compressible structure that BWT misses.

**Status:** NOT STARTED

### Track 5: Asymptotic Optimality via Finite-State Entropy
**Hypothesis:** Multi-table rANS approximates the true source entropy. Can we build a 
finite-state machine that asymptotically reaches the true entropy rate, even for 
non-stationary sources?

**Status:** NOT STARTED

---

## Experiments Log

### Experiment 0: Baseline v2.2.0
- Date: 2026-03-20
- Composite bpb: TBD
- This is our reference point. Every improvement is measured against this.
