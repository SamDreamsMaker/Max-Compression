#!/usr/bin/env python3
"""
Babel Transform Prototype — R&D for MaxCompression
====================================================
Test different bijective transforms to see if they reduce entropy
(making data more compressible by downstream LZ/ANS stages).

Concept: Like BWT reorganizes data for better compression,
a "Babel Transform" could reorganize data differently,
exploiting properties BWT misses.
"""

import math
import os
import sys
import time
from collections import Counter
from typing import Callable

# ══════════════════════════════════════════════════════════════════
#  Entropy measurement
# ══════════════════════════════════════════════════════════════════

def shannon_entropy(data: bytes) -> float:
    """Bits per byte (0 = perfectly uniform, 8 = max entropy)."""
    if len(data) == 0:
        return 0.0
    counts = Counter(data)
    n = len(data)
    entropy = 0.0
    for count in counts.values():
        p = count / n
        if p > 0:
            entropy -= p * math.log2(p)
    return entropy

def order1_entropy(data: bytes) -> float:
    """Context-1 entropy (bits/byte considering previous byte)."""
    if len(data) < 2:
        return shannon_entropy(data)
    
    # Count pairs
    pair_counts = Counter()
    ctx_counts = Counter()
    for i in range(len(data) - 1):
        ctx = data[i]
        sym = data[i + 1]
        pair_counts[(ctx, sym)] += 1
        ctx_counts[ctx] += 1
    
    n = len(data) - 1
    entropy = 0.0
    for (ctx, sym), count in pair_counts.items():
        p_pair = count / n
        p_sym_given_ctx = count / ctx_counts[ctx]
        if p_sym_given_ctx > 0:
            entropy -= p_pair * math.log2(p_sym_given_ctx)
    
    return entropy

def run_length_score(data: bytes) -> float:
    """Average run length — higher = more compressible by RLE."""
    if len(data) == 0:
        return 0.0
    runs = 1
    for i in range(1, len(data)):
        if data[i] != data[i - 1]:
            runs += 1
    return len(data) / runs

def lz_complexity(data: bytes, window: int = 1024) -> float:
    """Estimate LZ complexity: ratio of unique phrases to total length."""
    if len(data) == 0:
        return 0.0
    phrases = set()
    i = 0
    count = 0
    while i < len(data):
        best = 0
        for j in range(max(0, i - window), i):
            l = 0
            while i + l < len(data) and data[j + l] == data[i + l] and l < 255:
                l += 1
            if l > best:
                best = l
        if best < 3:
            i += 1
        else:
            i += best
        count += 1
    return count / len(data)  # Lower = more compressible


# ══════════════════════════════════════════════════════════════════
#  Transform 1: Babel Dictionary Transform (BDT)
#  
#  Idea: Sort all unique n-grams by frequency, replace each with
#  its rank. Frequent patterns → low numbers → byte clustering.
# ══════════════════════════════════════════════════════════════════

def babel_dict_transform(data: bytes, ngram_size: int = 2) -> bytes:
    """Replace n-grams with frequency-ranked indices."""
    if len(data) < ngram_size:
        return data
    
    # Count n-gram frequencies
    ngram_counts = Counter()
    for i in range(len(data) - ngram_size + 1):
        ngram = data[i:i + ngram_size]
        ngram_counts[ngram] += 1
    
    # Sort by frequency (most frequent = rank 0)
    sorted_ngrams = sorted(ngram_counts.keys(), key=lambda x: -ngram_counts[x])
    ngram_to_rank = {ng: rank for rank, ng in enumerate(sorted_ngrams)}
    
    # Encode: for each position, output the rank of the n-gram starting there
    # Use variable-width encoding: ranks < 256 = 1 byte, else 2 bytes
    result = bytearray()
    i = 0
    while i <= len(data) - ngram_size:
        ngram = data[i:i + ngram_size]
        rank = ngram_to_rank[ngram]
        if rank < 256:
            result.append(rank & 0xFF)
        else:
            result.append(0xFF)
            result.append((rank >> 8) & 0xFF)
            result.append(rank & 0xFF)
        i += ngram_size
    
    # Handle trailing bytes
    for j in range(i, len(data)):
        result.append(data[j])
    
    return bytes(result)


# ══════════════════════════════════════════════════════════════════
#  Transform 2: Context-Adaptive Babel Mapping (CABM)
#  
#  Idea: For each byte, remap it based on what's most likely in
#  its context. Most probable byte → 0, next → 1, etc.
#  This is like MTF but with learned probability tables.
# ══════════════════════════════════════════════════════════════════

def context_babel_transform(data: bytes, order: int = 1) -> bytes:
    """Remap each byte to its frequency rank within its context."""
    if len(data) <= order:
        return data
    
    # Pass 1: Build context frequency tables
    ctx_freq = {}  # context -> Counter of following bytes
    for i in range(order, len(data)):
        ctx = data[i - order:i]
        sym = data[i]
        if ctx not in ctx_freq:
            ctx_freq[ctx] = Counter()
        ctx_freq[ctx][sym] += 1
    
    # Build rank tables: for each context, rank bytes by frequency
    ctx_ranks = {}
    for ctx, freq in ctx_freq.items():
        sorted_syms = sorted(range(256), key=lambda s: -freq.get(s, 0))
        rank_table = [0] * 256
        for rank, sym in enumerate(sorted_syms):
            rank_table[sym] = rank
        ctx_ranks[ctx] = rank_table
    
    # Pass 2: Transform
    result = bytearray(data[:order])  # Copy context prefix unchanged
    for i in range(order, len(data)):
        ctx = data[i - order:i]
        sym = data[i]
        if ctx in ctx_ranks:
            result.append(ctx_ranks[ctx][sym])
        else:
            result.append(sym)
    
    return bytes(result)


# ══════════════════════════════════════════════════════════════════
#  Transform 3: Babel XOR Differential
#  
#  Idea: XOR each byte with a "predicted" value based on local
#  context hash. If prediction is good, output clusters near 0.
# ══════════════════════════════════════════════════════════════════

def babel_xor_predict(data: bytes, ctx_len: int = 2) -> bytes:
    """XOR each byte with prediction from context hash table."""
    if len(data) <= ctx_len:
        return data
    
    # Pass 1: Learn most common byte after each context
    ctx_predict = {}
    ctx_freq = {}
    for i in range(ctx_len, len(data)):
        ctx = data[i - ctx_len:i]
        sym = data[i]
        if ctx not in ctx_freq:
            ctx_freq[ctx] = Counter()
        ctx_freq[ctx][sym] += 1
    
    for ctx, freq in ctx_freq.items():
        ctx_predict[ctx] = freq.most_common(1)[0][0]
    
    # Pass 2: XOR with prediction
    result = bytearray(data[:ctx_len])
    for i in range(ctx_len, len(data)):
        ctx = data[i - ctx_len:i]
        prediction = ctx_predict.get(ctx, 0)
        result.append(data[i] ^ prediction)
    
    return bytes(result)


# ══════════════════════════════════════════════════════════════════
#  Transform 4: Babel Interleave (Bit-plane separation)
#  
#  Idea: Separate data into 8 bit-planes (all bit0s, all bit1s, etc.)
#  Each plane has lower entropy than the original bytes.
# ══════════════════════════════════════════════════════════════════

def babel_bitplane_split(data: bytes) -> bytes:
    """Separate bytes into 8 bit planes."""
    n = len(data)
    planes = [bytearray(n) for _ in range(8)]
    
    for i, byte in enumerate(data):
        for bit in range(8):
            planes[bit][i] = (byte >> bit) & 1
    
    # Pack each plane: 8 bits → 1 byte
    result = bytearray()
    for plane in planes:
        packed = bytearray((n + 7) // 8)
        for i in range(n):
            if plane[i]:
                packed[i // 8] |= 1 << (i % 8)
        result.extend(packed)
    
    return bytes(result)


# ══════════════════════════════════════════════════════════════════
#  Transform 5: Babel Word Index (the closest to the original idea)
#  
#  Treat data as "words" (byte sequences between separators),
#  assign each word a position in a sorted Babel dictionary,
#  output positions as variable-length integers.
# ══════════════════════════════════════════════════════════════════

def babel_word_index(data: bytes, separators: bytes = b' \t\n\r.,;:!?()[]{}') -> bytes:
    """Replace words with their Babel dictionary positions."""
    # Split into tokens
    tokens = []
    current = bytearray()
    for b in data:
        if b in separators:
            if current:
                tokens.append((bytes(current), False))
                current = bytearray()
            tokens.append((bytes([b]), True))
        else:
            current.append(b)
    if current:
        tokens.append((bytes(current), False))
    
    # Build frequency-sorted dictionary
    word_freq = Counter()
    for token, is_sep in tokens:
        if not is_sep:
            word_freq[token] += 1
    
    sorted_words = sorted(word_freq.keys(), key=lambda w: -word_freq[w])
    word_to_idx = {w: i for i, w in enumerate(sorted_words)}
    
    # Encode
    result = bytearray()
    for token, is_sep in tokens:
        if is_sep:
            result.append(token[0])  # Separator as-is
        else:
            idx = word_to_idx[token]
            # Variable-length encoding of index
            if idx < 128:
                result.append(idx)
            elif idx < 16384:
                result.append(0x80 | (idx >> 8))
                result.append(idx & 0xFF)
            else:
                result.append(0xC0 | (idx >> 16))
                result.append((idx >> 8) & 0xFF)
                result.append(idx & 0xFF)
    
    return bytes(result)


# ══════════════════════════════════════════════════════════════════
#  BWT Reference (for comparison)
# ══════════════════════════════════════════════════════════════════

def bwt_transform(data: bytes) -> bytes:
    """Simple BWT for comparison (suffix array based)."""
    n = len(data)
    if n == 0:
        return data
    if n > 100000:
        # Too slow for naive BWT, skip
        return data
    
    # Build suffix array (naive — O(n² log n))
    doubled = data + data
    indices = sorted(range(n), key=lambda i: doubled[i:i + n])
    
    # BWT output: last column
    result = bytearray(n)
    for i, idx in enumerate(indices):
        result[i] = data[(idx - 1) % n]
    
    return bytes(result)


# ══════════════════════════════════════════════════════════════════
#  Test harness
# ══════════════════════════════════════════════════════════════════

def measure_transform(name: str, data: bytes, transform_fn: Callable, **kwargs) -> dict:
    """Apply transform and measure all metrics."""
    t0 = time.perf_counter()
    try:
        transformed = transform_fn(data, **kwargs)
    except Exception as e:
        return {"name": name, "error": str(e)}
    elapsed = time.perf_counter() - t0
    
    orig_h0 = shannon_entropy(data)
    orig_h1 = order1_entropy(data)
    orig_rle = run_length_score(data)
    
    trans_h0 = shannon_entropy(transformed)
    trans_h1 = order1_entropy(transformed)
    trans_rle = run_length_score(transformed)
    
    size_ratio = len(transformed) / len(data) if len(data) > 0 else 0
    
    return {
        "name": name,
        "orig_size": len(data),
        "trans_size": len(transformed),
        "size_ratio": size_ratio,
        "orig_h0": orig_h0,
        "trans_h0": trans_h0,
        "h0_delta": trans_h0 - orig_h0,
        "orig_h1": orig_h1,
        "trans_h1": trans_h1,
        "h1_delta": trans_h1 - orig_h1,
        "orig_rle": orig_rle,
        "trans_rle": trans_rle,
        "time_ms": elapsed * 1000,
    }


def print_results(results: list, file_name: str):
    """Pretty-print results table."""
    print(f"\n{'=' * 80}")
    print(f"  FILE: {file_name} ({results[0]['orig_size']:,} bytes)")
    print(f"  Original H0: {results[0]['orig_h0']:.3f} bits/byte | H1: {results[0]['orig_h1']:.3f} bits/byte | RLE: {results[0]['orig_rle']:.2f}")
    print(f"{'=' * 80}")
    print(f"  {'Transform':<30} {'Size':>8} {'H0':>7} {'ΔH0':>7} {'H1':>7} {'ΔH1':>7} {'RLE':>6} {'ms':>8}")
    print(f"  {'-' * 30} {'-' * 8} {'-' * 7} {'-' * 7} {'-' * 7} {'-' * 7} {'-' * 6} {'-' * 8}")
    
    for r in results:
        if "error" in r:
            print(f"  {r['name']:<30} ERROR: {r['error']}")
            continue
        
        h0_arrow = "↓" if r["h0_delta"] < -0.01 else ("↑" if r["h0_delta"] > 0.01 else "=")
        h1_arrow = "↓" if r["h1_delta"] < -0.01 else ("↑" if r["h1_delta"] > 0.01 else "=")
        
        print(f"  {r['name']:<30} {r['trans_size']:>7}B {r['trans_h0']:>6.3f} {r['h0_delta']:>+6.3f}{h0_arrow} {r['trans_h1']:>6.3f} {r['h1_delta']:>+6.3f}{h1_arrow} {r['trans_rle']:>5.2f} {r['time_ms']:>7.1f}")


def generate_test_data():
    """Generate various test data types."""
    tests = {}
    
    # English text
    tests["english_text"] = (b"The quick brown fox jumps over the lazy dog. " * 200 +
                              b"In the beginning was the Word, and the Word was with God. " * 150 +
                              b"To be or not to be, that is the question. " * 180)
    
    # Structured data (JSON-like)
    tests["json_like"] = (b'{"name":"Alice","age":30,"city":"Paris"},' * 300 +
                           b'{"name":"Bob","age":25,"city":"Lyon"},' * 300)
    
    # Binary with patterns
    tests["binary_pattern"] = bytes([(i * 7 + 13) & 0xFF for i in range(10000)] +
                                     [0x00] * 2000 + [0xFF] * 2000 +
                                     [(i * 3) & 0xFF for i in range(6000)])
    
    # Source code
    tests["source_code"] = (b"int main(int argc, char** argv) {\n" * 100 +
                             b"    printf(\"Hello World\\n\");\n" * 200 +
                             b"    for (int i = 0; i < n; i++) {\n" * 150 +
                             b"        sum += array[i];\n" * 200 +
                             b"    }\n    return 0;\n}\n" * 100)
    
    # High entropy (pseudo-random)
    import hashlib
    random_data = bytearray()
    seed = b"babel_seed_42"
    for i in range(1000):
        seed = hashlib.sha256(seed + i.to_bytes(4, 'little')).digest()
        random_data.extend(seed)
    tests["high_entropy"] = bytes(random_data[:20000])
    
    return tests


def main():
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║     BABEL TRANSFORM PROTOTYPE — MaxCompression R&D         ║")
    print("║     Testing entropy reduction of novel transforms          ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    
    # Define transforms to test
    transforms = [
        ("BWT (reference)", bwt_transform, {}),
        ("Babel Dict (2-gram)", babel_dict_transform, {"ngram_size": 2}),
        ("Babel Dict (3-gram)", babel_dict_transform, {"ngram_size": 3}),
        ("Context Babel (order-1)", context_babel_transform, {"order": 1}),
        ("Context Babel (order-2)", context_babel_transform, {"order": 2}),
        ("Babel XOR Predict (ctx=2)", babel_xor_predict, {"ctx_len": 2}),
        ("Babel XOR Predict (ctx=3)", babel_xor_predict, {"ctx_len": 3}),
        ("Babel Bitplane Split", babel_bitplane_split, {}),
        ("Babel Word Index", babel_word_index, {}),
    ]
    
    # Test on generated data
    tests = generate_test_data()
    
    # Also test on real files if available
    real_files = [
        "/home/overmind/.openclaw/workspace/max-compression/README.md",
        "/home/overmind/.openclaw/workspace/max-compression/lib/core.c",
    ]
    
    for path in real_files:
        if os.path.exists(path):
            with open(path, "rb") as f:
                data = f.read()
            if len(data) > 0:
                name = os.path.basename(path)
                # Cap at 100KB for BWT speed
                tests[f"real:{name}"] = data[:100000]
    
    # Run all tests
    all_results = {}
    for test_name, test_data in tests.items():
        results = []
        for t_name, t_fn, t_kwargs in transforms:
            # Skip BWT on large files
            if "BWT" in t_name and len(test_data) > 100000:
                continue
            r = measure_transform(t_name, test_data, t_fn, **t_kwargs)
            results.append(r)
        
        print_results(results, test_name)
        all_results[test_name] = results
    
    # Summary: which transforms consistently reduce entropy?
    print(f"\n{'=' * 80}")
    print(f"  SUMMARY: Average H0 delta across all test files")
    print(f"{'=' * 80}")
    
    transform_scores = {}
    for test_name, results in all_results.items():
        for r in results:
            if "error" in r:
                continue
            name = r["name"]
            if name not in transform_scores:
                transform_scores[name] = []
            # Effective compression potential = entropy reduction × size ratio
            effective = r["h0_delta"] * r["size_ratio"]
            transform_scores[name].append(effective)
    
    sorted_transforms = sorted(transform_scores.items(), 
                                key=lambda x: sum(x[1]) / len(x[1]))
    
    for name, scores in sorted_transforms:
        avg = sum(scores) / len(scores)
        arrow = "✅" if avg < -0.1 else ("⚠️" if avg < 0 else "❌")
        print(f"  {arrow} {name:<35} avg effective ΔH0: {avg:>+.3f}")
    
    print(f"\n  Legend: ✅ = promising (reduces entropy) | ⚠️ = marginal | ❌ = increases entropy")
    print(f"\n  Lower (more negative) = better compression potential")


if __name__ == "__main__":
    main()
