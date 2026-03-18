#!/bin/bash
# MCX Benchmark Script
# Usage: ./bench.sh [corpus_dir] [levels...]
# Example: ./bench.sh /tmp/silesia 6 9 20
#          ./bench.sh /tmp/cantrbry 1 3 6 9 12 20 26

set -e

CORPUS="${1:-/tmp/silesia}"
shift 2>/dev/null || true
LEVELS="${@:-6 9 20}"
[ -z "$LEVELS" ] && LEVELS="6 9 20"

MCX="${MCX:-$(dirname "$0")/../build/bin/mcx}"
if [ ! -x "$MCX" ]; then
    echo "Error: mcx not found at $MCX"
    echo "Build first: cmake -S . -B build && cmake --build build"
    exit 1
fi

echo "╔══════════════════════════════════════════════╗"
echo "║  MaxCompression Benchmark                    ║"
echo "╚══════════════════════════════════════════════╝"
echo "Binary: $MCX"
echo "Corpus: $CORPUS"
echo "Levels: $LEVELS"
echo ""

for level in $LEVELS; do
    printf "═══ Level %d ═══\n" "$level"
    printf "%-15s %8s → %8s  %7s  %s\n" "File" "Size" "Comp" "Ratio" "Status"
    printf "%-15s %8s   %8s  %7s  %s\n" "────" "────" "────" "─────" "──────"
    
    total_in=0
    total_out=0
    all_ok=1
    
    for f in "$CORPUS"/*; do
        [ -f "$f" ] || continue
        name=$(basename "$f")
        size=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f")
        
        # Compress
        timeout 600 "$MCX" compress -l "$level" "$f" -o "/tmp/_mcx_bench.mcx" 2>/dev/null
        if [ $? -ne 0 ]; then
            printf "%-15s %7dKB   TIMEOUT\n" "$name" $((size/1024))
            continue
        fi
        csize=$(stat -c%s "/tmp/_mcx_bench.mcx" 2>/dev/null || stat -f%z "/tmp/_mcx_bench.mcx")
        
        # Verify roundtrip
        "$MCX" decompress "/tmp/_mcx_bench.mcx" -o "/tmp/_mcx_bench.dec" 2>/dev/null
        if diff "$f" "/tmp/_mcx_bench.dec" > /dev/null 2>&1; then
            status="✓"
        else
            status="FAIL"
            all_ok=0
        fi
        
        ratio=$(python3 -c "print(f'{$size/$csize:.2f}')" 2>/dev/null || echo "N/A")
        printf "%-15s %7dKB → %7dKB  %5s×  %s\n" "$name" $((size/1024)) $((csize/1024)) "$ratio" "$status"
        
        total_in=$((total_in + size))
        total_out=$((total_out + csize))
        rm -f "/tmp/_mcx_bench.mcx" "/tmp/_mcx_bench.dec"
    done
    
    if [ $total_out -gt 0 ]; then
        ratio=$(python3 -c "print(f'{$total_in/$total_out:.2f}')" 2>/dev/null || echo "N/A")
        printf "%-15s %7dKB → %7dKB  %5s×\n" "TOTAL" $((total_in/1024)) $((total_out/1024)) "$ratio"
    fi
    echo ""
done
