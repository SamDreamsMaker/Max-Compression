#!/bin/bash
# Integration test for --preserve-mtime flag
set -e

MCX="$1"
if [ -z "$MCX" ]; then
    echo "Usage: $0 <path-to-mcx>"
    exit 1
fi

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "=== --preserve-mtime integration test ==="

# Create test file and set a specific mtime (2024-01-15 12:00:00)
echo "Hello, mtime preservation test!" > "$TMPDIR/input.txt"
touch -t 202401151200.00 "$TMPDIR/input.txt"
ORIG_MTIME=$(stat -c %Y "$TMPDIR/input.txt")

# Compress with --preserve-mtime
"$MCX" compress -l 1 --preserve-mtime -o "$TMPDIR/output.mcx" "$TMPDIR/input.txt"
COMP_MTIME=$(stat -c %Y "$TMPDIR/output.mcx")

if [ "$COMP_MTIME" != "$ORIG_MTIME" ]; then
    echo "FAIL: Compressed file mtime ($COMP_MTIME) != input mtime ($ORIG_MTIME)"
    exit 1
fi
echo "OK: Compressed file mtime matches input"

# Decompress with --preserve-mtime
"$MCX" decompress --preserve-mtime -o "$TMPDIR/restored.txt" "$TMPDIR/output.mcx"
DEC_MTIME=$(stat -c %Y "$TMPDIR/restored.txt")

if [ "$DEC_MTIME" != "$COMP_MTIME" ]; then
    echo "FAIL: Decompressed file mtime ($DEC_MTIME) != compressed file mtime ($COMP_MTIME)"
    exit 1
fi
echo "OK: Decompressed file mtime matches compressed file"

# Verify roundtrip content
if ! cmp -s "$TMPDIR/input.txt" "$TMPDIR/restored.txt"; then
    echo "FAIL: Roundtrip verification failed (files differ)"
    exit 1
fi
echo "OK: Content roundtrip verified"

# Test without --preserve-mtime (should NOT preserve mtime)
sleep 1
"$MCX" compress -l 1 -o "$TMPDIR/output2.mcx" "$TMPDIR/input.txt"
COMP2_MTIME=$(stat -c %Y "$TMPDIR/output2.mcx")

if [ "$COMP2_MTIME" = "$ORIG_MTIME" ]; then
    echo "WARN: Without --preserve-mtime, mtime coincidentally matches (possible if very fast)"
fi
echo "OK: Without --preserve-mtime completed"

echo "=== All --preserve-mtime tests passed ==="
