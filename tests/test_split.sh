#!/bin/bash
# Integration test for --split flag: compress with split, cat chunks, decompress, verify roundtrip
set -e

MCX="$1"
if [ -z "$MCX" ]; then
    echo "Usage: $0 <path-to-mcx>"
    exit 1
fi

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "=== --split integration test ==="

# Create test file (128KB of mixed data)
dd if=/dev/urandom bs=1024 count=128 of="$TMPDIR/input.bin" 2>/dev/null

# Compress with --split 32K (should produce multiple chunks)
"$MCX" compress -l 1 --split 32K -o "$TMPDIR/output.mcx" "$TMPDIR/input.bin"

# Verify chunks were created
CHUNKS=$(ls "$TMPDIR"/output.mcx.* 2>/dev/null | wc -l)
if [ "$CHUNKS" -lt 2 ]; then
    echo "FAIL: Expected multiple chunks, got $CHUNKS"
    exit 1
fi
echo "OK: Got $CHUNKS chunks"

# Verify main output file was removed (replaced by chunks)
if [ -f "$TMPDIR/output.mcx" ]; then
    echo "FAIL: Main output file should not exist when chunks are created"
    exit 1
fi
echo "OK: Main output file correctly removed"

# Cat chunks back together
cat "$TMPDIR"/output.mcx.* > "$TMPDIR/reassembled.mcx"

# Decompress the reassembled file
"$MCX" decompress -o "$TMPDIR/restored.bin" "$TMPDIR/reassembled.mcx"

# Verify roundtrip
if ! cmp -s "$TMPDIR/input.bin" "$TMPDIR/restored.bin"; then
    echo "FAIL: Roundtrip verification failed (files differ)"
    exit 1
fi
echo "OK: Roundtrip verified"

# Test with a file smaller than split size (should NOT create chunks)
dd if=/dev/urandom bs=1024 count=4 of="$TMPDIR/small.bin" 2>/dev/null
"$MCX" compress -l 1 --split 1M -o "$TMPDIR/small.mcx" "$TMPDIR/small.bin"

if ls "$TMPDIR"/small.mcx.* >/dev/null 2>&1; then
    echo "FAIL: Small file should not produce chunks"
    exit 1
fi

if [ ! -f "$TMPDIR/small.mcx" ]; then
    echo "FAIL: Small file should produce single output"
    exit 1
fi

# Verify small file roundtrip
"$MCX" decompress -o "$TMPDIR/small_restored.bin" "$TMPDIR/small.mcx"
if ! cmp -s "$TMPDIR/small.bin" "$TMPDIR/small_restored.bin"; then
    echo "FAIL: Small file roundtrip verification failed"
    exit 1
fi
echo "OK: Small file (no split needed) roundtrip verified"

echo "=== All --split tests passed ==="
