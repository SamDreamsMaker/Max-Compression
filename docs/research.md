# MaxCompression: Advanced Mathematical & Algorithmic Research

## 1. The Speed Wall (LZ4 vs MaxCompression)
MaxCompression currently pushes ~300 MB/s decompression. LZ4 pushes ~2,500 MB/s. 
To bridge an 8x speed gap, we cannot rely on incremental loop unrolling. We must mathematically eliminate CPU branches and vectorize the core parsing loops using SIMD (AVX2/NEON).

---

## 2. Vectorized Hashing (Match Finder)
Currently, finding matches requires hashing 4-byte sequences one by one.
Standard Knuth Hash: `(val * 2654435761U) >> shifted`

### The Mathematical Vectorization Approach
Instead of calculating $h(x_i)$ sequentially, we calculate $h(x_i), h(x_{i+1}), h(x_{i+2}), h(x_{i+3})$ simultaneously using AVX2.

```c
// Pseudo-AVX2 Parallel Hashing
__m128i values = _mm_loadu_si128((__m128i*)ip); // Load 16 bytes
__m128i multiplier = _mm_set1_epi32(2654435761U);
__m128i hashes = _mm_mullo_epi32(values, multiplier);
hashes = _mm_srli_epi32(hashes, hash_shift);
```

By hashing 4 positions per CPU cycle, we mathematically guarantee a 4x throughput increase in dictionary lookup pre-fetching.

### Rolling Hash vs Re-computation
A Rabin-Karp polynomial rolling hash maintains state:
$H(x_{1 \dots k}) = \sum_{i=1}^k c_i b^{k-i} \pmod m$

Moving the window by 1 byte requires just two operations (one subtraction, one addition), which is theoretically $O(1)$. We must evaluate if a vectorized rolling hash outperforms vectorized parallel Knuth hashing on modern superscalar CPUs where multiplication throughput is high.

---

## 3. Branchless SIMD State Machines (Decompression)
The primary bottleneck in decompression is branch misprediction (`if length < 15`, `if offset == 0`). A pipeline flush costs ~15-20 cycles.

### The "Iguana" Mathematical Model
We can encode LZ77 tokens such that their values directly index into a pre-computed SIMD shuffle mask, entirely avoiding `if` statements.

$T_{literal\_len} \rightarrow M_{shuffle\_mask}$

```c
// Mathematical Branchless Execution
uint8_t token = *ip++;
uint32_t lit_len = decode_table[token].lit_len;
uint32_t match_len = decode_table[token].match_len;

// Unconditional SIMD load/store based on mathematical offset mapping
__m256i literals = _mm256_loadu_si256((__m256i*)ip);
__m256i mask = _mm256_loadu_si256((__m256i*)&shuffle_masks[lit_len]);
literals = _mm256_shuffle_epi8(literals, mask);
_mm256_storeu_si256((__m256i*)op, literals);
```

This transforms compression into a pure stream of deterministic vector operations, hitting the theoretical limit of the memory bandwidth.

---

## 4. Perfect Hashing (Cuckoo) for Match Finding
Standard hash tables suffer from collisions, leading to linear chaining (following pointers), which destroys cache locality.

If we use **Cuckoo Hashing** (two independent mathematical hashes $h_1(x)$ and $h_2(x)$), we theoretically guarantee $O(1)$ worst-case lookup time. 
If both slots are full, the older match is evicted. For LZ77, this is naturally optimal as we prefer recent matches (smaller offsets to encode).

$h_1(x) = (x \cdot m_1) \gg s$
$h_2(x) = (x \cdot m_2) \gg s$

Using AVX2, evaluating both hashes concurrently is mathematically free.

---

## Next Steps
1. Create a `lib/lz/mcx_lz_fast.c` implementation relying strictly on vectorized hashing and branchless execution over 16-32 byte boundaries.
2. Verify compression speed via `pro_bench.py` targeting $>1$ GB/s.
