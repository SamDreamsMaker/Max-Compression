#!/bin/bash
# Integration test for bench flags: --all-levels, --ratio-only
set -e

MCX="$1"
if [ -z "$MCX" ]; then
    echo "Usage: $0 <path-to-mcx>"
    exit 1
fi

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "=== Bench flags integration test ==="

# Create small test file
echo "The quick brown fox jumps over the lazy dog. Testing bench flags." > "$TMPDIR/input.txt"

# Test --all-levels: should produce 26 level lines
OUTPUT=$("$MCX" bench --all-levels --ratio-only "$TMPDIR/input.txt" 2>&1)
LEVEL_COUNT=$(echo "$OUTPUT" | grep -c "^L[0-9]")
if [ "$LEVEL_COUNT" -ne 26 ]; then
    echo "FAIL: --all-levels should produce 26 level lines, got $LEVEL_COUNT"
    echo "$OUTPUT"
    exit 1
fi
echo "OK: --all-levels produced 26 level lines"

# Verify all levels 1-26 are present
for lvl in $(seq 1 26); do
    if ! echo "$OUTPUT" | grep -q "^L${lvl} "; then
        echo "FAIL: Level $lvl missing from --all-levels output"
        exit 1
    fi
done
echo "OK: All levels 1-26 present in output"

# Test --ratio-only: output should NOT contain "MB/s"
if echo "$OUTPUT" | grep -q "MB/s"; then
    echo "FAIL: --ratio-only should not show speed columns"
    exit 1
fi
echo "OK: --ratio-only hides speed columns"

# Test default levels (without --all-levels): should produce 8 level lines
OUTPUT2=$("$MCX" bench --ratio-only "$TMPDIR/input.txt" 2>&1)
LEVEL_COUNT2=$(echo "$OUTPUT2" | grep -c "^L[0-9]")
if [ "$LEVEL_COUNT2" -ne 8 ]; then
    echo "FAIL: Default bench should produce 8 level lines, got $LEVEL_COUNT2"
    echo "$OUTPUT2"
    exit 1
fi
echo "OK: Default bench produced 8 level lines"

echo "=== All bench flags tests passed ==="
