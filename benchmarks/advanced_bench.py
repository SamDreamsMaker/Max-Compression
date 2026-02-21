import os
import sys
import time
import psutil
import subprocess
import urllib.request
import tarfile
import multiprocessing
import lz4.frame
import zstandard as zstd
import gzip

# Config
BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
CORPUS_DIR = os.path.join(BENCH_DIR, "corpus_adv")
MCX_BIN = os.path.abspath(os.path.join(BENCH_DIR, "..", "build", "bin", "Release", "mcx.exe"))

os.makedirs(CORPUS_DIR, exist_ok=True)

class BenchResult:
    def __init__(self, name, size_mb):
        self.name = name
        self.size_mb = size_mb
        self.comp_speed = 0.0
        self.dec_speed = 0.0
        self.ratio = 0.0
        self.peak_mem_mb = 0.0
        self.success = False
        self.error = ""

def track_memory(pid, peak_mem_val):
    try:
        proc = psutil.Process(pid)
        while proc.is_running():
            mem_info = proc.memory_info()
            # On Windows, peak_wset is often the most accurate representation of peak RAM usage
            current_peak = mem_info.peak_wset / (1024 * 1024)
            if current_peak > peak_mem_val.value:
                peak_mem_val.value = current_peak
            time.sleep(0.01)
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        pass

def py_compress_worker(algo, in_path, out_path, peak_mem_val, time_val, ret_val):
    try:
        # Start a memory tracker thread on OUR process
        import threading
        tracker = threading.Thread(target=track_memory, args=(os.getpid(), peak_mem_val), daemon=True)
        tracker.start()

        with open(in_path, 'rb') as f:
            data = f.read()

        t0 = time.time()
        if algo == "zstd":
            cctx = zstd.ZstdCompressor(level=3) # default
            cdata = cctx.compress(data)
        elif algo == "lz4":
            cdata = lz4.frame.compress(data)
        elif algo == "gzip":
            cdata = gzip.compress(data, compresslevel=6)
        t1 = time.time()

        with open(out_path, 'wb') as f:
            f.write(cdata)

        time_val.value = t1 - t0
        ret_val.value = 1
    except Exception as e:
        ret_val.value = 0
        print(f"Worker comp error: {e}")

def py_decompress_worker(algo, in_path, out_path, peak_mem_val, time_val, ret_val):
    try:
        import threading
        tracker = threading.Thread(target=track_memory, args=(os.getpid(), peak_mem_val), daemon=True)
        tracker.start()

        with open(in_path, 'rb') as f:
            data = f.read()

        t0 = time.time()
        if algo == "zstd":
            dctx = zstd.ZstdDecompressor()
            ddata = dctx.decompress(data)
        elif algo == "lz4":
            ddata = lz4.frame.decompress(data)
        elif algo == "gzip":
            ddata = gzip.decompress(data)
        t1 = time.time()

        with open(out_path, 'wb') as f:
            f.write(ddata)

        time_val.value = t1 - t0
        ret_val.value = 1
    except Exception as e:
        ret_val.value = 0
        print(f"Worker dec error: {e}")


def benchmark_python_algo(algo_name, fpath, file_size):
    res = BenchResult(algo_name, file_size / (1024*1024))
    cmp_path = fpath + f".{algo_name}"
    dec_path = fpath + f".{algo_name}.dec"

    # COMPRESSION
    peak_mem = multiprocessing.Value('d', 0.0)
    time_taken = multiprocessing.Value('d', 0.0)
    ret_ok = multiprocessing.Value('i', 0)

    p = multiprocessing.Process(target=py_compress_worker, args=(algo_name, fpath, cmp_path, peak_mem, time_taken, ret_ok))
    p.start()
    p.join()

    if ret_ok.value == 0:
        res.error = "Compress failed"
        return res

    comp_sz = os.path.getsize(cmp_path)
    res.ratio = file_size / comp_sz if comp_sz > 0 else 0
    res.comp_speed = res.size_mb / time_taken.value if time_taken.value > 0 else 0
    res.peak_mem_mb = peak_mem.value

    # DECOMPRESSION
    peak_mem_dec = multiprocessing.Value('d', 0.0)
    time_taken_dec = multiprocessing.Value('d', 0.0)
    ret_ok_dec = multiprocessing.Value('i', 0)

    p = multiprocessing.Process(target=py_decompress_worker, args=(algo_name, cmp_path, dec_path, peak_mem_dec, time_taken_dec, ret_ok_dec))
    p.start()
    p.join()

    if ret_ok_dec.value == 0:
        res.error = "Decompress failed"
        return res
        
    res.dec_speed = res.size_mb / time_taken_dec.value if time_taken_dec.value > 0 else 0
    res.peak_mem_mb = max(res.peak_mem_mb, peak_mem_dec.value)
    res.success = True

    if os.path.exists(cmp_path): os.remove(cmp_path)
    if os.path.exists(dec_path): os.remove(dec_path)
    return res

def benchmark_mcx(level, name_label, fpath, file_size):
    res = BenchResult(name_label, file_size / (1024*1024))
    cmp_path = fpath + ".mcx"
    dec_path = fpath + ".mcx.dec"

    # COMPRESSION
    peak_mem_c = 0.0
    t0 = time.time()
    try:
        proc = subprocess.Popen([MCX_BIN, "compress", "-l", str(level), "-o", cmp_path, fpath], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # Poll memory while running
        try:
            pp = psutil.Process(proc.pid)
            while proc.poll() is None:
                mi = pp.memory_info()
                val = mi.peak_wset / (1024*1024)
                if val > peak_mem_c: peak_mem_c = val
                time.sleep(0.01)
        except:
            pass
        proc.communicate()
    except Exception as e:
        res.error = str(e)
        return res
    t1 = time.time()

    if proc.returncode != 0:
        res.error = f"Exit {proc.returncode}"
        return res

    comp_sz = os.path.getsize(cmp_path)
    res.ratio = file_size / comp_sz if comp_sz > 0 else 0
    res.comp_speed = res.size_mb / (t1 - t0) if (t1 - t0) > 0 else 0
    
    # DECOMPRESS
    peak_mem_d = 0.0
    t2 = time.time()
    try:
        proc = subprocess.Popen([MCX_BIN, "decompress", "-o", dec_path, cmp_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            pp = psutil.Process(proc.pid)
            while proc.poll() is None:
                mi = pp.memory_info()
                val = mi.peak_wset / (1024*1024)
                if val > peak_mem_d: peak_mem_d = val
                time.sleep(0.01)
        except:
            pass
        proc.communicate()
    except Exception as e:
        res.error = str(e)
        return res
    t3 = time.time()

    if proc.returncode != 0:
        res.error = f"Dec Exit {proc.returncode}"
        return res

    res.dec_speed = res.size_mb / (t3 - t2) if (t3 - t2) > 0 else 0
    res.peak_mem_mb = max(peak_mem_c, peak_mem_d)
    res.success = True
    
    if os.path.exists(cmp_path): os.remove(cmp_path)
    if os.path.exists(dec_path): os.remove(dec_path)
    
    return res

def generate_datasets():
    print("Generating Benchmark Datasets...")
    datasets = []
    
    # 1. 100MB Random (Incompressible)
    rnd_path = os.path.join(CORPUS_DIR, "random_100MB.bin")
    if not os.path.exists(rnd_path):
        print("  Creating random_100MB.bin...")
        with open(rnd_path, "wb") as f:
            f.write(os.urandom(100 * 1024 * 1024))
    datasets.append(("Incompressible (100MB)", rnd_path))
            
    # 2. 100MB Sparse (All zeros)
    sparse_path = os.path.join(CORPUS_DIR, "sparse_100MB.bin")
    if not os.path.exists(sparse_path):
        print("  Creating sparse_100MB.bin...")
        with open(sparse_path, "wb") as f:
            f.write(b'\x00' * (100 * 1024 * 1024))
    datasets.append(("Sparse Zeros (100MB)", sparse_path))
            
    # 3. 100MB Pattern (Periodic structured)
    tex_path = os.path.join(CORPUS_DIR, "texture_100MB.bin")
    if not os.path.exists(tex_path):
        print("  Creating texture_100MB.bin...")
        pattern = bytearray([x % 256 for x in range(1024)])
        with open(tex_path, "wb") as f:
            for _ in range(100 * 1024):
                f.write(pattern)
    datasets.append(("Structured Pattern (100MB)", tex_path))

    # 4. Silesia Corpus (Real-world large file)
    silesia_tar = os.path.join(CORPUS_DIR, "silesia.tar")
    if not os.path.exists(silesia_tar):
        print("  Downloading Silesia Corpus (~200MB)...")
        # Direct ZIP link (often faster) but historically Canterbury hosts it. We'll use a known mirror or local fallback.
        # To avoid failing the script on networks, we will build a 100MB "Text" mock if download fails.
        try:
            url = "https://sun.aei.polsl.pl//~sdeor/corpus/silesia.tar"
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=30) as response, open(silesia_tar, 'wb') as out_file:
                out_file.write(response.read())
        except Exception as e:
            print(f"  Failed to download Silesia: {e}. Generating large text mock alternative.")
            silesia_tar = os.path.join(CORPUS_DIR, "mock_text_100MB.txt")
            if not os.path.exists(silesia_tar):
                with open(silesia_tar, "wb") as f:
                    phrase = b"The quick brown fox jumps over the lazy dog. " * 50 + b"\n"
                    blocks = (100 * 1024 * 1024) // len(phrase)
                    f.write(phrase * blocks)
    
    datasets.append(("Real-World Text/Mixed", silesia_tar))
    return datasets

def main():
    print(f"===========================================================")
    print(f" MaxCompression Advanced Benchmark Suite")
    print(f"===========================================================\n")
    
    if not os.path.exists(MCX_BIN):
        print(f"ERROR: Cannot find {MCX_BIN}. Did you build in Release mode?")
        return

    datasets = generate_datasets()
    
    for label, fpath in datasets:
        size = os.path.getsize(fpath)
        print(f"\n### Dataset: {label} ({size/(1024*1024):.1f} MB)")
        print(f"| Algorithm | Ratio | Compress (MB/s) | Decompress (MB/s) | Peak RAM (MB) |")
        print(f"|-----------|-------|-----------------|-------------------|---------------|")
        
        algos = [
            ("LZ4 (Fast)", lambda: benchmark_python_algo("lz4", fpath, size)),
            ("Zstd (Fast)", lambda: benchmark_python_algo("zstd", fpath, size)),
            ("Gzip (Legacy)", lambda: benchmark_python_algo("gzip", fpath, size)),
            ("MCX Level 1 (Huffman)", lambda: benchmark_mcx(1, "MCX L1", fpath, size)),
            ("MCX Level 10 (BWT+RLE)", lambda: benchmark_mcx(10, "MCX L10", fpath, size)),
            ("MCX Level 20 (CM-rANS)", lambda: benchmark_mcx(20, "MCX L20", fpath, size))
        ]
        
        for name, func in algos:
            r = func()
            if r.success:
                print(f"| {name:20} | {r.ratio:5.2f}x | {r.comp_speed:7.1f} MB/s | {r.dec_speed:7.1f} MB/s | {r.peak_mem_mb:7.1f} MB |")
            else:
                print(f"| {name:20} | ERROR | {r.error} | - | - |")

if __name__ == '__main__':
    # Required for Windows multiprocessing
    multiprocessing.freeze_support()
    main()
