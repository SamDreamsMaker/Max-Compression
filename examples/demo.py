#!/usr/bin/env python3
"""MaxCompression Demo — Quick compression example."""

import sys
import os
import time

# Add bindings to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'bindings', 'python'))

try:
    import maxcomp
except RuntimeError as e:
    print(f"Error: {e}")
    print("Build the shared library first:")
    print("  cmake -S . -B build -DBUILD_SHARED_LIBS=ON")
    print("  cd build && make maxcomp_shared")
    print("  cp build/lib/libmaxcomp.so bindings/python/")
    sys.exit(1)

def demo_compress(filepath, level=20):
    """Compress a file and show results."""
    with open(filepath, 'rb') as f:
        data = f.read()
    
    print(f"File: {filepath}")
    print(f"Size: {len(data):,} bytes")
    print(f"Level: {level}")
    print()
    
    t0 = time.time()
    compressed = maxcomp.compress(data, level=level)
    t1 = time.time()
    
    decompressed = maxcomp.decompress(compressed)
    t2 = time.time()
    
    ratio = len(data) / len(compressed)
    savings = (1 - len(compressed) / len(data)) * 100
    
    print(f"Compressed:   {len(compressed):,} bytes ({ratio:.2f}x, {savings:.1f}% smaller)")
    print(f"Compress time:   {t1-t0:.3f}s ({len(data)/1024/1024/(t1-t0):.1f} MB/s)")
    print(f"Decompress time: {t2-t1:.3f}s ({len(data)/1024/1024/(t2-t1):.1f} MB/s)")
    print(f"Roundtrip: {'✅ OK' if decompressed == data else '❌ FAIL'}")
    print()

def main():
    if len(sys.argv) < 2:
        print("Usage: python demo.py <file> [level]")
        print()
        print("Levels:")
        print("  1-3   Fast (LZ77)")
        print("  4-9   High (LZ77 with hash chains)")
        print("  10-14 Very High (BWT + multi-table rANS)")
        print("  20    Maximum (smart mode: auto-detects best strategy)")
        print()
        print("Example:")
        print("  python demo.py myfile.txt 20")
        return
    
    filepath = sys.argv[1]
    level = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    
    if not os.path.exists(filepath):
        print(f"Error: file not found: {filepath}")
        return
    
    demo_compress(filepath, level)

if __name__ == '__main__':
    main()
