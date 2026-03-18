#!/bin/bash
# Compare MCX against gzip, bzip2, and xz on a file
# Usage: ./compare.sh <file> [mcx_path]

set -e

FILE="$1"
MCX="${2:-mcx}"

if [ -z "$FILE" ] || [ ! -f "$FILE" ]; then
    echo "Usage: $0 <file> [mcx_path]"
    exit 1
fi

SIZE=$(stat -c%s "$FILE" 2>/dev/null || stat -f%z "$FILE")
NAME=$(basename "$FILE")

printf "\n%-12s  %10s  %8s  %8s\n" "Compressor" "Compressed" "Ratio" "Saving"
printf "──────────────────────────────────────────────\n"
printf "%-12s  %10d  %8s  %8s\n" "Original" "$SIZE" "1.00x" "0.0%"

# gzip
if command -v gzip &>/dev/null; then
    gzip -9 -c "$FILE" > /tmp/_cmp_gzip.gz 2>/dev/null
    GZ=$(stat -c%s /tmp/_cmp_gzip.gz 2>/dev/null || stat -f%z /tmp/_cmp_gzip.gz)
    printf "%-12s  %10d  %8.2fx  %5.1f%%\n" "gzip -9" "$GZ" "$(echo "$SIZE $GZ" | awk '{printf "%.2f", $1/$2}')" "$(echo "$SIZE $GZ" | awk '{printf "%.1f", 100*(1-$2/$1)}')"
    rm -f /tmp/_cmp_gzip.gz
fi

# bzip2
if command -v bzip2 &>/dev/null; then
    bzip2 -9 -c "$FILE" > /tmp/_cmp_bzip2.bz2 2>/dev/null
    BZ=$(stat -c%s /tmp/_cmp_bzip2.bz2 2>/dev/null || stat -f%z /tmp/_cmp_bzip2.bz2)
    printf "%-12s  %10d  %8.2fx  %5.1f%%\n" "bzip2 -9" "$BZ" "$(echo "$SIZE $BZ" | awk '{printf "%.2f", $1/$2}')" "$(echo "$SIZE $BZ" | awk '{printf "%.1f", 100*(1-$2/$1)}')"
    rm -f /tmp/_cmp_bzip2.bz2
fi

# xz
if command -v xz &>/dev/null; then
    xz -9 -c "$FILE" > /tmp/_cmp_xz.xz 2>/dev/null
    XZ=$(stat -c%s /tmp/_cmp_xz.xz 2>/dev/null || stat -f%z /tmp/_cmp_xz.xz)
    printf "%-12s  %10d  %8.2fx  %5.1f%%\n" "xz -9" "$XZ" "$(echo "$SIZE $XZ" | awk '{printf "%.2f", $1/$2}')" "$(echo "$SIZE $XZ" | awk '{printf "%.1f", 100*(1-$2/$1)}')"
    rm -f /tmp/_cmp_xz.xz
fi

# MCX levels
for L in 6 12 20 26; do
    "$MCX" compress -l $L "$FILE" -o /tmp/_cmp_mcx.mcx 2>/dev/null
    MX=$(stat -c%s /tmp/_cmp_mcx.mcx 2>/dev/null || stat -f%z /tmp/_cmp_mcx.mcx)
    printf "%-12s  %10d  %8.2fx  %5.1f%%\n" "mcx -l$L" "$MX" "$(echo "$SIZE $MX" | awk '{printf "%.2f", $1/$2}')" "$(echo "$SIZE $MX" | awk '{printf "%.1f", 100*(1-$2/$1)}')"
    rm -f /tmp/_cmp_mcx.mcx
done

echo ""
