#!/bin/bash
# Test multi-file compress/decompress roundtrip
# Validates that mcx correctly handles multiple input files in one command
set -e
MCX="${1:-build/bin/mcx}"
TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT

echo "Testing multi-file compress/decompress roundtrip..."

# Create diverse test files
echo "Hello World! This is file one with some text content." > "$TMP/file1.txt"
dd if=/dev/urandom bs=1024 count=4 of="$TMP/file2.bin" 2>/dev/null
printf 'aaaaaaaaaa%.0s' {1..100} > "$TMP/file3.rle"
seq 1 500 > "$TMP/file4.csv"

echo "  Created 4 test files"

# ── Multi-file compress ──
$MCX compress -q "$TMP/file1.txt" "$TMP/file2.bin" "$TMP/file3.rle" "$TMP/file4.csv"

# Verify all .mcx files were created
for f in file1.txt file2.bin file3.rle file4.csv; do
    if [ ! -f "$TMP/${f}.mcx" ]; then
        echo "FAIL: $TMP/${f}.mcx not created"
        exit 1
    fi
done
echo "  Multi-file compress: OK (4 archives created)"

# ── Multi-file decompress ──
# Remove originals first so decompress can write to the same names
cp "$TMP/file1.txt" "$TMP/file1.txt.orig"
cp "$TMP/file2.bin" "$TMP/file2.bin.orig"
cp "$TMP/file3.rle" "$TMP/file3.rle.orig"
cp "$TMP/file4.csv" "$TMP/file4.csv.orig"
rm "$TMP/file1.txt" "$TMP/file2.bin" "$TMP/file3.rle" "$TMP/file4.csv"

$MCX decompress -q "$TMP/file1.txt.mcx" "$TMP/file2.bin.mcx" "$TMP/file3.rle.mcx" "$TMP/file4.csv.mcx"

echo "  Multi-file decompress: OK"

# ── Verify roundtrip integrity ──
FAIL=0
for f in file1.txt file2.bin file3.rle file4.csv; do
    if ! diff "$TMP/${f}.orig" "$TMP/${f}" > /dev/null 2>&1; then
        echo "FAIL: ${f} content mismatch after roundtrip!"
        FAIL=1
    fi
done

if [ $FAIL -ne 0 ]; then
    echo "FAIL: multi-file roundtrip integrity check failed"
    exit 1
fi
echo "  Roundtrip integrity: OK (all 4 files match)"

# ── Test decompress aliases with multi-file ──
rm "$TMP/file1.txt" "$TMP/file2.bin"
$MCX extract -q "$TMP/file1.txt.mcx" "$TMP/file2.bin.mcx"
diff "$TMP/file1.txt.orig" "$TMP/file1.txt" > /dev/null && echo "  extract alias (multi-file): OK"
diff "$TMP/file2.bin.orig" "$TMP/file2.bin" > /dev/null || { echo "FAIL: extract alias"; exit 1; }

rm "$TMP/file3.rle" "$TMP/file4.csv"
$MCX x -q "$TMP/file3.rle.mcx" "$TMP/file4.csv.mcx"
diff "$TMP/file3.rle.orig" "$TMP/file3.rle" > /dev/null && echo "  x alias (multi-file): OK"
diff "$TMP/file4.csv.orig" "$TMP/file4.csv" > /dev/null || { echo "FAIL: x alias"; exit 1; }

# ── Test multi-file compress with --force ──
$MCX compress -q -f "$TMP/file1.txt" "$TMP/file2.bin"
echo "  Multi-file compress --force (overwrite): OK"

# ── Test verify on multi-file output ──
$MCX verify "$TMP/file1.txt.mcx" "$TMP/file1.txt" > /dev/null && echo "  verify after multi-file: OK"

echo ""
echo "All multi-file tests passed!"
