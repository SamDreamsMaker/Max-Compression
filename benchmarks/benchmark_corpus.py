import os
import time
import urllib.request
import tarfile
import subprocess
import gzip
import bz2
import lzma

CORPUS_URL = "http://corpus.canterbury.ac.nz/resources/cantrbry.tar.gz"
CORPUS_DIR = "corpus"
MCX_BIN = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "bin", "Release", "mcx.exe"))

if not os.path.exists(CORPUS_DIR):
    os.makedirs(CORPUS_DIR)
    
tar_path = os.path.join(CORPUS_DIR, "cantrbry.tar.gz")
if not os.path.exists(tar_path):
    print("Downloading Canterbury corpus...")
    req = urllib.request.Request(CORPUS_URL, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as response, open(tar_path, 'wb') as out_file:
        out_file.write(response.read())

with tarfile.open(tar_path, "r:gz") as tar:
    tar.extractall(path=CORPUS_DIR)

files = [f for f in sorted(os.listdir(CORPUS_DIR)) if os.path.isfile(os.path.join(CORPUS_DIR, f)) and f != "cantrbry.tar.gz"]

print(f"| File | Size | GZIP (-9) | BZIP2 (-9) | LZMA | **MCX (-l 12)** | MCX Comp | MCX Dec |")
print(f"|---|---|---|---|---|---|---|---|")

total_orig = 0
total_mcx = 0

for f in files:
    fpath = os.path.abspath(os.path.join(CORPUS_DIR, f))
    orig_size = os.path.getsize(fpath)
    total_orig += orig_size
    
    with open(fpath, "rb") as orig_f:
        data = orig_f.read()
        
    gz_data = gzip.compress(data, compresslevel=9)
    bz2_data = bz2.compress(data, compresslevel=9)
    lzma_data = lzma.compress(data)
    
    gz_ratio = orig_size / len(gz_data)
    bz2_ratio = orig_size / len(bz2_data)
    lzma_ratio = orig_size / len(lzma_data)
    
    mcx_out = fpath + ".mcx"
    t0 = time.time()
    # Using level 12 for benchmark to test block chunking with CM-rANS efficiently
    res = subprocess.run([MCX_BIN, "compress", "-l", "12", "-o", mcx_out, fpath], capture_output=True, text=True)
    t1 = time.time()
    if res.returncode != 0:
        print(f"| {f} | ERROR | - | - | - | - | - | - |")
        print(f"**MCX ERROR**: {res.stderr}")
        continue
    
    comp_speed = (orig_size / (t1 - t0)) / 1e6 if (t1 - t0) > 0 else 0
    mcx_size = os.path.getsize(mcx_out)
    total_mcx += mcx_size
    mcx_ratio = orig_size / mcx_size
    
    mcx_dec = fpath + ".dec"
    t2 = time.time()
    subprocess.run([MCX_BIN, "decompress", "-o", mcx_dec, mcx_out], capture_output=True)
    t3 = time.time()
    dec_speed = (orig_size / (t3 - t2)) / 1e6 if (t3 - t2) > 0 else 0
    
    print(f"| {f:12} | {orig_size:>8,} B | {gz_ratio:5.2f}x | {bz2_ratio:5.2f}x | {lzma_ratio:5.2f}x | **{mcx_ratio:5.2f}x** | {comp_speed:6.1f} MB/s | {dec_speed:6.1f} MB/s |")
    
    os.remove(mcx_out)
    if os.path.exists(mcx_dec):
        os.remove(mcx_dec)

print(f"\n**Total Corpus Ratio**: {(total_orig / total_mcx):.2f}x\n")
