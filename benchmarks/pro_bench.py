import os
import sys
import ctypes
import time
import csv
import zlib
import multiprocessing
import argparse
from corpus_manager import get_corpus_files

try:
    import psutil
    import zstandard as zstd
    import lz4.block as lz4b
    import statistics
    import json
except ImportError:
    print("Please install basic requirements: pip install zstandard lz4 psutil")
    sys.exit(1)

try:
    import numpy as np
    import matplotlib.pyplot as plt
    HAS_PLOT = True
except ImportError:
    HAS_PLOT = False

# ─── CTYPES BINDING FOR MAXCOMPRESSION ──────────────────────────────
# We load the DLL to do purely in-memory compression without I/O bias
DLL_PATH = os.path.join(os.path.dirname(__file__), '..', 'build', 'bin', 'Release', 'maxcomp.dll')
if os.path.exists(DLL_PATH):
    mcx_lib = ctypes.CDLL(DLL_PATH)
    
    # size_t mcx_compress(void* dst, size_t dst_cap, const void* src, size_t src_size, int level);
    mcx_lib.mcx_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    mcx_lib.mcx_compress.restype = ctypes.c_size_t
    
    # size_t mcx_decompress(void* dst, size_t dst_cap, const void* src, size_t src_size);
    mcx_lib.mcx_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    mcx_lib.mcx_decompress.restype = ctypes.c_size_t
    
    # size_t mcx_compress_bound(size_t src_size);
    mcx_lib.mcx_compress_bound.argtypes = [ctypes.c_size_t]
    mcx_lib.mcx_compress_bound.restype = ctypes.c_size_t
    
    # unsigned long long mcx_get_decompressed_size(const void* src, size_t src_size);
    mcx_lib.mcx_get_decompressed_size.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    mcx_lib.mcx_get_decompressed_size.restype = ctypes.c_ulonglong
else:
    mcx_lib = None

# ─── CTYPES BINDING FOR MCX_LZ_FAST (SIMD/AVX2 Experimental) ────────
if mcx_lib:
    # void mcx_lz_fast_init(mcx_lz_fast_ctx* ctx)  -- ctx is uint32_t[32768]
    mcx_lz_fast_ctx_t = ctypes.c_uint32 * 32768
    mcx_lib.mcx_lz_fast_init.argtypes = [ctypes.POINTER(mcx_lz_fast_ctx_t)]
    mcx_lib.mcx_lz_fast_init.restype = None

    mcx_lib.mcx_lz_fast_compress.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(mcx_lz_fast_ctx_t)
    ]
    mcx_lib.mcx_lz_fast_compress.restype = ctypes.c_size_t

    mcx_lib.mcx_lz_fast_decompress.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t
    ]
    mcx_lib.mcx_lz_fast_decompress.restype = ctypes.c_size_t

    # Pre-allocate a single context (re-init before each compress call)
    _fast_ctx = mcx_lz_fast_ctx_t()

def mcx_fast_compress_in_memory(data_bytes):
    if not mcx_lib: return None
    src_size = len(data_bytes)
    dst_cap = max(src_size + src_size // 4 + 32, 256)
    src_buf = ctypes.create_string_buffer(data_bytes, src_size)
    dst_buf = ctypes.create_string_buffer(dst_cap)
    mcx_lib.mcx_lz_fast_init(ctypes.byref(_fast_ctx))
    comp_size = mcx_lib.mcx_lz_fast_compress(dst_buf, dst_cap, src_buf, src_size, ctypes.byref(_fast_ctx))
    if comp_size == 0: return None
    # Prepend original size (8 bytes) so decompressor knows output capacity
    import struct
    return struct.pack('<Q', src_size) + dst_buf.raw[:comp_size]

def mcx_fast_decompress_in_memory(comp_bytes):
    if not mcx_lib: return None
    import struct
    if len(comp_bytes) < 8: return None
    orig_size = struct.unpack('<Q', comp_bytes[:8])[0]
    payload = comp_bytes[8:]
    src_size = len(payload)
    src_buf = ctypes.create_string_buffer(payload, src_size)
    dst_buf = ctypes.create_string_buffer(orig_size + 64)  # +64 for wild-copy padding
    dec_size = mcx_lib.mcx_lz_fast_decompress(dst_buf, orig_size + 64, src_buf, src_size)
    if dec_size != orig_size: return None
    return dst_buf.raw[:dec_size]


def mcx_compress_in_memory(data_bytes, level=3):
    if not mcx_lib: return None
    src_size = len(data_bytes)
    dst_cap = mcx_lib.mcx_compress_bound(src_size)
    
    src_buf = ctypes.create_string_buffer(data_bytes, src_size)
    dst_buf = ctypes.create_string_buffer(dst_cap)
    
    comp_size = mcx_lib.mcx_compress(dst_buf, dst_cap, src_buf, src_size, level)
    
    if comp_size == 0 or comp_size > dst_cap + 1000:
        return None
    return dst_buf.raw[:comp_size]


def mcx_decompress_in_memory(comp_bytes):
    if not mcx_lib: return None
    src_size = len(comp_bytes)
    src_buf = ctypes.create_string_buffer(comp_bytes, src_size)
    
    orig_size = mcx_lib.mcx_get_decompressed_size(src_buf, src_size)
    if orig_size == 0: return None
        
    dst_buf = ctypes.create_string_buffer(orig_size)
    dec_size = mcx_lib.mcx_decompress(dst_buf, orig_size, src_buf, src_size)
    
    if dec_size != orig_size: return None
    return dst_buf.raw[:dec_size]


# ─── BENCHMARK CORE ──────────────────────────────────────────────────

def get_algorithms(threads=1):
    return {
        "lz4-fast": (
            lambda d: lz4b.compress(d, store_size=True),
            lambda d: lz4b.decompress(d)
        ),
        "lz4-hc": (
            lambda d: lz4b.compress(d, mode='high_compression', store_size=True),
            lambda d: lz4b.decompress(d)
        ),
        "zstd-3": (
            lambda d: zstd.ZstdCompressor(level=3, threads=threads).compress(d),
            lambda d: zstd.ZstdDecompressor().decompress(d)
        ),
        "zstd-9": (
            lambda d: zstd.ZstdCompressor(level=9, threads=threads).compress(d),
            lambda d: zstd.ZstdDecompressor().decompress(d)
        ),
        "zstd-19": (
            lambda d: zstd.ZstdCompressor(level=19, threads=threads).compress(d),
            lambda d: zstd.ZstdDecompressor().decompress(d)
        ),
        "mcx-3": (
            lambda d: mcx_compress_in_memory(d, 3),
            lambda d: mcx_decompress_in_memory(d)
        ),
        "mcx-9": (
            lambda d: mcx_compress_in_memory(d, 9),
            lambda d: mcx_decompress_in_memory(d)
        ),
        "mcx-12": (
            lambda d: mcx_compress_in_memory(d, 12),
            lambda d: mcx_decompress_in_memory(d)
        ),
        "mcx-fast": (
            lambda d: mcx_fast_compress_in_memory(d),
            lambda d: mcx_fast_decompress_in_memory(d)
        )
    }

import gc

def measure_rss_usage(func, arg):
    """Run func and return its Peak RSS delta in MB."""
    gc.collect()
    process = psutil.Process()
    baseline = process.memory_info().rss
    
    # Run Function
    res = func(arg)
    
    peak = process.memory_info().rss
    # Delete result and force collection to restore state for next run
    del res
    gc.collect()
    
    return max(0, peak - baseline) / (1024*1024)

import platform
import hashlib

def run_benchmark(ds_name, data, iterations=3, threads=1):
    results = []
    orig_size = len(data)
    checksum = hashlib.sha256(data).hexdigest()[:8]
    
    # Set OpenMP threads for MCX
    os.environ['OMP_NUM_THREADS'] = str(threads)
    
    algorithms = get_algorithms(threads)
    print(f"\n--- Corpus: {ds_name} ({orig_size / (1024*1024):.2f} MB) | Threads: {threads} | SHA256: {checksum} ---")
    
    for algo_name, (comp_fn, dec_fn) in algorithms.items():
        if algo_name in ["zstd-19", "mcx-12"] and orig_size >= 100 * 1024 * 1024:
            print(f"  Skipping {algo_name} (corpus too large)...")
            continue
        if "mcx" in algo_name and not mcx_lib:
            continue
            
        print(f"  Testing {algo_name}...", end='', flush=True)
        try:
            # 1. Warm-up & Size measure
            comp_data = comp_fn(data)
            if comp_data is None: 
                print(" ERROR")
                continue
            
            comp_size = len(comp_data)
            dec_data = dec_fn(comp_data)
            
            if not dec_data or len(dec_data) != orig_size:
                print(" VERIFY FAIL")
                continue
                
            # 2. Benchmark Compress
            c_times = []
            for _ in range(iterations):
                t0 = time.perf_counter()
                comp_fn(data)
                c_times.append(time.perf_counter() - t0)
            
            # 3. Benchmark Decompress
            d_times = []
            for _ in range(iterations):
                t0 = time.perf_counter()
                dec_fn(comp_data)
                d_times.append(time.perf_counter() - t0)
                
            # 4. Measure Peak Memory & Leak check
            gc.collect()
            baseline_rss = psutil.Process().memory_info().rss / (1024*1024)
            peak_ram_mb = measure_rss_usage(comp_fn, data)
            
            # Post-decompression baseline to check for leaks
            dec_data = dec_fn(comp_data)
            del dec_data
            gc.collect()
            leak_mb = max(0, (psutil.Process().memory_info().rss / (1024*1024)) - baseline_rss)

            # 5. Math & Stats
            ratio = orig_size / comp_size if comp_size > 0 else 0
            
            c_times_ms = [t * 1000 for t in c_times]
            d_times_ms = [t * 1000 for t in d_times]
            
            c_mean = statistics.mean(c_times_ms)
            c_std = statistics.stdev(c_times_ms) if len(c_times_ms) > 1 else 0
            d_mean = statistics.mean(d_times_ms)
            d_std = statistics.stdev(d_times_ms) if len(d_times_ms) > 1 else 0
            
            c_mbps = (orig_size / (1024*1024)) / (c_mean / 1000)
            d_mbps = (orig_size / (1024*1024)) / (d_mean / 1000)
            
            # Weissman Score (using zstd-3 single-threaded as reference if possible, or just self-referential)
            # W = (r / r_ref) * (log(T_ref) / log(T))
            # For simplicity, we just store the components for now or use a constant ref
            weissman = (ratio / 1.0) * (1.0 / (1.0 + c_mean/1000)) # Placeholder basic score

            print(f" OK ({ratio:.2f}x, {c_mbps:.0f} MB/s, {d_mbps:.0f} MB/s)")
            
            results.append({
                "Corpus": ds_name,
                "Algorithm": algo_name,
                "Threads": threads,
                "Original_Size": orig_size,
                "Compressed_Size": comp_size,
                "Ratio": round(ratio, 4),
                "Comp_Time_Mean_ms": round(c_mean, 2),
                "Comp_Time_Std_ms": round(c_std, 2),
                "Comp_Time_Min_ms": round(min(c_times_ms), 2),
                "Comp_Time_Max_ms": round(max(c_times_ms), 2),
                "Decomp_Time_Mean_ms": round(d_mean, 2),
                "Decomp_Time_Std_ms": round(d_std, 2),
                "Decomp_MBps": round(d_mbps, 2),
                "Comp_MBps": round(c_mbps, 2),
                "Peak_RAM_MB": round(peak_ram_mb, 2),
                "Leak_MB": round(leak_mb, 2),
                "Variance_Rel": round(c_std / c_mean if c_mean > 0 else 0, 4)
            })
            
        except Exception as e:
            print(f" EXCEPTION: {e}")
            import traceback
            traceback.print_exc()
            
    return results

def generate_plots(results, output_dir="plots"):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Example: Ratio vs Comp Speed
    plt.figure(figsize=(10, 6))
    for algo in set(r["Algorithm"] for r in results):
        algo_res = [r for r in results if r["Algorithm"] == algo]
        plt.scatter([r["Comp_MBps"] for r in algo_res], [r["Ratio"] for r in algo_res], label=algo)
    
    plt.xlabel("Compression Speed (MB/s)")
    plt.ylabel("Compression Ratio")
    plt.title("Compression Ratio vs Speed")
    plt.legend()
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, "ratio_vs_speed.png"))
    plt.close()

    # Scaling plot if multiple threads
    threads = sorted(list(set(r["Threads"] for r in results)))
    if len(threads) > 1:
        plt.figure(figsize=(10, 6))
        for algo in set(r["Algorithm"] for r in results):
            algo_res = [r for r in results if r["Algorithm"] == algo]
            # Average across corpora for scaling
            scaling = []
            for t in threads:
                t_res = [r["Comp_MBps"] for r in algo_res if r["Threads"] == t]
                if t_res:
                    scaling.append(statistics.mean(t_res))
                else:
                    scaling.append(0)
            plt.plot(threads, scaling, marker='o', label=algo)
        
        plt.xlabel("Threads")
        plt.ylabel("Avg Compression Speed (MB/s)")
        plt.title("Multi-thread Scaling")
        plt.legend()
        plt.grid(True)
        plt.savefig(os.path.join(output_dir, "scaling.png"))
        plt.close()

def main():
    parser = argparse.ArgumentParser(description="MaxCompression Professional Benchmark Suite")
    parser.add_argument('--iter', type=int, default=3, help='Iterations per test')
    parser.add_argument('--threads', type=str, default='1', help='Comma-separated list of threads to test (e.g. 1,2,4)')
    parser.add_argument('--export-csv', type=str, default='benchmark_results.csv', help='CSV output file')
    parser.add_argument('--export-md', type=str, default='benchmark_results.md', help='Markdown output file')
    parser.add_argument('--export-json', type=str, default='benchmark_results.json', help='JSON output file')
    parser.add_argument('--plot', action='store_true', help='Generate plots')
    parser.add_argument('--download', action='store_true', default=True, help='Auto-download corpus if missing')
    args = parser.parse_args()

    # Auto-provisioning
    from corpus_manager import download_and_extract, get_corpus_files
    if args.download:
        download_and_extract()

    thread_list = [int(t) for t in args.threads.split(',')]

    # Load corpus files
    corpora = get_corpus_files()
    if not corpora:
        print("No corpora found. Run corpus_manager.py first.")
        return
        
    all_results = []
    
    # We need a reference for Weissman Score calculation
    # Let's say zstd-3 at 1 thread is our baseline
    ref_algo = "zstd-3"
    ref_threads = 1
    ref_data = {} # corpus_name -> (ratio, comp_time)
    
    # First pass: find references if possible
    # (Or just use the first algo we encounter for each corpus as ref)
    
    print("Pre-scanning for Weissman baseline...")
    # ... actually let's just compute it after we have all results.
    
    for threads in thread_list:
        for c_name, c_files in corpora.items():
            if c_name == 'enwik9':
                continue
            data = bytearray()
            for f in c_files:
                try:
                    with open(f, 'rb') as fp:
                        data.extend(fp.read())
                except Exception as e:
                    print(f"Skipping {f}: {e}")
            
            if len(data) > 0:
                res = run_benchmark(c_name, bytes(data), iterations=args.iter, threads=threads)
                all_results.extend(res)

    # Calculate Weissman Scores
    for r in all_results:
        # Find reference for this corpus
        corpus_ref = [b for b in all_results if b["Corpus"] == r["Corpus"] and b["Algorithm"] == ref_algo and b["Threads"] == ref_threads]
        if corpus_ref:
            ref = corpus_ref[0]
            # Weissman: W = (r/r_ref) * (log(T_ref) / log(T))
            # using log10
            import math
            try:
                # Use mean times
                t_ref = ref["Comp_Time_Mean_ms"]
                t_curr = r["Comp_Time_Mean_ms"]
                w_score = (r["Ratio"] / ref["Ratio"]) * (math.log10(t_ref) / math.log10(t_curr))
                r["Weissman_Score"] = round(w_score, 4)
            except:
                r["Weissman_Score"] = 1.0
        else:
            r["Weissman_Score"] = 1.0

    # Export CSV

    # Export CSV
    if all_results and args.export_csv:
        keys = all_results[0].keys()
        with open(args.export_csv, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=keys)
            writer.writeheader()
            writer.writerows(all_results)
        print(f"\n[+] Exported CSV to {args.export_csv}")

    # Export JSON
    if all_results and args.export_json:
        with open(args.export_json, 'w', encoding='utf-8') as f:
            json.dump(all_results, f, indent=4)
        print(f"[+] Exported JSON to {args.export_json}")

    # Export Markdown
    if all_results and args.export_md:
        with open(args.export_md, 'w', encoding='utf-8') as f:
            f.write("# MaxCompression v1.0.0 Professional Benchmark\n\n")
            f.write(f"**Hardware Details:** `{platform.processor()}` on `{platform.platform()}`\n\n")
            f.write("| Corpus | Algorithm | Threads | Ratio | Comp Speed (MB/s) | Peak RAM (MB) | Weissman | Var Rel |\n")
            f.write("|--------|-----------|---------|-------|-------------------|---------------|----------|---------|\n")
            
            for r in all_results:
                f.write(f"| {r['Corpus']} | **{r['Algorithm']}** | {r['Threads']} | {r['Ratio']:.2f}x | {r['Comp_MBps']:.0f} | {r['Peak_RAM_MB']:.1f} | {r.get('Weissman_Score', 1.0):.2f} | {r['Variance_Rel']:.2%} |\n")
        print(f"[+] Exported Markdown to {args.export_md}")

    if args.plot:
        if HAS_PLOT:
            generate_plots(all_results)
            print("[+] Generated plots in ./plots")
        else:
            print("[!] Cannot generate plots: matplotlib/numpy not installed.")

if __name__ == '__main__':
    multiprocessing.freeze_support()
    main()
