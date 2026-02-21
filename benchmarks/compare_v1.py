"""
MaxCompression v1.0 — Head-to-Head Performance Comparison
Compares the new LZ77 engine against Zstd, LZ4, Gzip on multiple datasets.
"""
import os, sys, time, ctypes, struct, zlib, subprocess, tempfile

# Try importing competitors
try:
    import zstandard as zstd
    HAS_ZSTD = True
except ImportError:
    HAS_ZSTD = False

try:
    import lz4.block as lz4b
    HAS_LZ4 = True
except ImportError:
    HAS_LZ4 = False

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False

# ── MCX LZ77 via ctypes ────────────────────────────────────────────
MCX_DLL = os.path.join(os.path.dirname(__file__), '..', 'build', 'bin', 'Release', 'maxcomp.dll')
MCX_EXE = os.path.join(os.path.dirname(__file__), '..', 'build', 'bin', 'Release', 'mcx.exe')

def mcx_compress_cli(data, level=3):
    """Use the CLI tool for full MCX pipeline compression."""
    with tempfile.NamedTemporaryFile(delete=False, suffix='.bin') as f:
        f.write(data)
        in_path = f.name
    out_path = in_path + '.mcx'
    try:
        t0 = time.perf_counter()
        subprocess.run([MCX_EXE, 'compress', in_path, '-o', out_path, '-l', str(level)], 
                      capture_output=True, timeout=30)
        t1 = time.perf_counter()
        if os.path.exists(out_path):
            comp_size = os.path.getsize(out_path)
            # Decompress
            t2 = time.perf_counter()
            dec_path = out_path + '.dec'
            subprocess.run([MCX_EXE, 'decompress', out_path, '-o', dec_path],
                          capture_output=True, timeout=30)
            t3 = time.perf_counter()
            os.unlink(dec_path) if os.path.exists(dec_path) else None
            os.unlink(out_path)
            os.unlink(in_path)
            return comp_size, t1 - t0, t3 - t2
    except Exception:
        pass
    for p in [in_path, out_path]:
        if os.path.exists(p): os.unlink(p)
    return None, None, None

# ── Dataset generators ──────────────────────────────────────────────
import random

def gen_zeros(n):
    return b'\x00' * n

def gen_repeated_pattern(n):
    pattern = b'ABCDEFGH' * 16  # 128 byte pattern
    return (pattern * (n // len(pattern) + 1))[:n]

def gen_text(n):
    words = "the quick brown fox jumps over lazy dog and compression algorithm data entropy coding huffman lz77 dictionary match".split()
    random.seed(42)
    text = ' '.join(random.choice(words) for _ in range(n // 5))
    return text.encode('utf-8')[:n]

def gen_random(n):
    random.seed(123)
    return bytes(random.getrandbits(8) for _ in range(n))

def gen_structured(n):
    # JSON-like structured data
    return bytes((i * 7 + i // 256) & 0xFF for i in range(n))

# ── Benchmark engine ────────────────────────────────────────────────

def bench_algo(name, compress_fn, decompress_fn, data, verify=True):
    """Benchmark a single algorithm, return (comp_size, comp_time, dec_time)."""
    # Warmup
    try:
        comp = compress_fn(data)
    except Exception as e:
        return None, None, None

    # Compress (3 runs, best time)
    best_ct = float('inf')
    for _ in range(3):
        t0 = time.perf_counter()
        comp = compress_fn(data)
        t1 = time.perf_counter()
        best_ct = min(best_ct, t1 - t0)

    # Decompress (3 runs, best time)
    best_dt = float('inf')
    for _ in range(3):
        t0 = time.perf_counter()
        dec = decompress_fn(comp)
        t1 = time.perf_counter()
        best_dt = min(best_dt, t1 - t0)

    if verify and dec != data:
        print(f"  ⚠ {name}: verification FAILED (dec_size={len(dec)} vs orig={len(data)})")

    return len(comp), best_ct, best_dt

def format_speed(size, time_s):
    if time_s <= 0: return "∞"
    mbps = (size / (1024 * 1024)) / time_s
    if mbps >= 1000: return f"{mbps:.0f} MB/s"
    return f"{mbps:.1f} MB/s"

def format_ratio(orig, comp):
    if comp <= 0: return "∞"
    return f"{orig / comp:.2f}x"

# ── Main ────────────────────────────────────────────────────────────

datasets = [
    ("1MB Zeros",           gen_zeros(1_000_000)),
    ("1MB Repeated Pattern", gen_repeated_pattern(1_000_000)),
    ("1MB Text",            gen_text(1_000_000)),
    ("1MB Structured",      gen_structured(1_000_000)),
    ("1MB Random",          gen_random(1_000_000)),
    ("10MB Text",           gen_text(10_000_000)),
]

algorithms = []

# Gzip (always available)
algorithms.append(("gzip-6", 
    lambda d: zlib.compress(d, 6),
    lambda d: zlib.decompress(d)))

# Zstd
if HAS_ZSTD:
    zctx = zstd.ZstdCompressor(level=3)
    zdctx = zstd.ZstdDecompressor()
    algorithms.append(("zstd-3",
        lambda d: zctx.compress(d),
        lambda d: zdctx.decompress(d, max_output_size=len(d)*10)))

    zctx9 = zstd.ZstdCompressor(level=9)
    algorithms.append(("zstd-9",
        lambda d: zctx9.compress(d),
        lambda d: zdctx.decompress(d, max_output_size=len(d)*10)))

# LZ4
if HAS_LZ4:
    algorithms.append(("lz4",
        lambda d: lz4b.compress(d, store_size=True),
        lambda d: lz4b.decompress(d)))

# MCX full pipeline (via CLI)
if os.path.exists(MCX_EXE):
    pass # Will be tested separately

print("=" * 90)
print("MaxCompression v1.0 — Performance Comparison")
print("=" * 90)
print()

# Print installed algorithms
print(f"Algorithms: gzip-6" + (", zstd-3, zstd-9" if HAS_ZSTD else "") + (", lz4" if HAS_LZ4 else ""))
has_mcx = os.path.exists(MCX_EXE)
if has_mcx:
    print(f"MCX CLI: {MCX_EXE}")
print()

# Column headers
header = f"{'Dataset':<22} {'Algorithm':<12} {'Ratio':>8} {'Compress':>12} {'Decompress':>12}"
print(header)
print("-" * len(header))

for ds_name, data in datasets:
    orig_size = len(data)
    results = []

    for algo_name, comp_fn, dec_fn in algorithms:
        cs, ct, dt = bench_algo(algo_name, comp_fn, dec_fn, data)
        if cs is not None:
            results.append((algo_name, cs, ct, dt))

    # MCX CLI
    if has_mcx:
        cs, ct, dt = mcx_compress_cli(data, level=3)
        if cs is not None:
            results.append(("mcx-3", cs, ct, dt))
            
        cs, ct, dt = mcx_compress_cli(data, level=9)
        if cs is not None:
            results.append(("mcx-9", cs, ct, dt))

    for algo, cs, ct, dt in results:
        ratio = format_ratio(orig_size, cs)
        comp_spd = format_speed(orig_size, ct)
        dec_spd = format_speed(orig_size, dt)
        print(f"  {ds_name:<20} {algo:<12} {ratio:>8} {comp_spd:>12} {dec_spd:>12}")
    print()

print("=" * 90)
print("Done.")
