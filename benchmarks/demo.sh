#!/bin/bash
# Demo: MCX vs gzip vs bzip2 on a sample file
# Usage: ./benchmarks/demo.sh [file]
set -e

FILE="${1:-/tmp/cantrbry/alice29.txt}"
NAME=$(basename "$FILE")
SIZE=$(stat -c%s "$FILE" 2>/dev/null || stat -f%z "$FILE")
MCX="${MCX:-mcx}"

echo "╔══════════════════════════════════════════════════╗"
echo "║     MaxCompression vs gzip vs bzip2              ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""
echo "File: $NAME ($SIZE bytes)"
echo ""
printf "%-20s %10s %8s\n" "Compressor" "Size" "Ratio"
echo "─────────────────────────────────────────────"
printf "%-20s %10s %8s\n" "Original" "$SIZE" "1.00x"

# gzip
if command -v gzip &>/dev/null; then
    GZ=$(gzip -9 -c "$FILE" | wc -c)
    RATIO=$(python3 -c "print(f'{$SIZE/$GZ:.2f}x')")
    printf "%-20s %10s %8s\n" "gzip -9" "$GZ" "$RATIO"
fi

# bzip2
if command -v bzip2 &>/dev/null; then
    BZ=$(bzip2 -9 -c "$FILE" | wc -c)
    RATIO=$(python3 -c "print(f'{$SIZE/$BZ:.2f}x')")
    printf "%-20s %10s %8s\n" "bzip2 -9" "$BZ" "$RATIO"
fi

# xz
if command -v xz &>/dev/null; then
    XZ=$(xz -9 -c "$FILE" | wc -c)
    RATIO=$(python3 -c "print(f'{$SIZE/$XZ:.2f}x')")
    printf "%-20s %10s %8s\n" "xz -9" "$XZ" "$RATIO"
fi

# MCX levels
TMP=$(mktemp)
for L in 6 12 20; do
    rm -f "$TMP"
    $MCX compress -l $L -q "$FILE" -o "$TMP"
    SZ=$(stat -c%s "$TMP" 2>/dev/null || stat -f%z "$TMP")
    RATIO=$(python3 -c "print(f'{$SIZE/$SZ:.2f}x')")
    printf "%-20s %10s %8s\n" "mcx -l $L" "$SZ" "$RATIO"
done
rm -f "$TMP"

echo ""
echo "★ MCX L20 = maximum compression (auto-detects best strategy)"
