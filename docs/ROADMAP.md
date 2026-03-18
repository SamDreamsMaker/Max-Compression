# MCX Roadmap

## v1.x — BWT-Based Compression (Current)

### Completed
- [x] LZ77 (greedy, lazy, hash chains) + tANS/FSE
- [x] BWT (SA-IS) + MTF + RLE + rANS
- [x] Multi-table rANS (K-means clustering, 4–6 tables)
- [x] RLE2 (RUNA/RUNB exponential zero-run encoding)
- [x] Stride-delta preprocessing (auto-detected)
- [x] E8/E9 x86 BCJ filter
- [x] Adaptive arithmetic coding (order-1, Fenwick-tree decode)
- [x] Smart Mode (L20) — auto-detect + multi-trial
- [x] OpenMP block parallelism
- [x] Python bindings
- [x] 204 comprehensive roundtrip tests

### Results
- Beats bzip2 on 100% of Silesia files
- Beats xz on 75% of Silesia files
- kennedy.xls: 50.1× (2.4× better than xz)
- Silesia total: 4.21×

## v2.0 — LZ + Range Coder Engine

### Completed
- [x] Subbotin-style range coder with carry propagation
- [x] Binary tree match finder (16 MB+ window)
- [x] Distance slot model (64 slots)
- [x] LZRC encoder + decoder (roundtrip verified)
- [x] Rep match support

### In Progress
- [ ] Optimal parsing (price-based match decisions)
- [ ] Multiple rep distances (rep0–rep3)
- [ ] Context-mixed literal coding

### Planned
- [ ] New block type integration
- [ ] Multi-trial: BWT vs LZRC per file
- [ ] E8/E9 + LZRC for executables

### Target
- Close -24% gap vs xz on mozilla
- Close -12% gap vs xz on samba
- Maintain advantage on text (BWT path)

## v3.0 — Future

- [ ] ARM/ARM64 BCJ filter
- [ ] Streaming API (arbitrary-length input)
- [ ] WASM build for browser usage
- [ ] GPU-accelerated BWT (CUDA/OpenCL)
- [ ] Per-block strategy selection (heterogeneous archives)
