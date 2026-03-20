#!/bin/bash
# Test --decompress-check, --priority, and --repeat flags
set -e
MCX="$1"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Create test data
dd if=/dev/urandom bs=1024 count=64 of="$TMPDIR/random.bin" 2>/dev/null
echo "The quick brown fox jumps over the lazy dog." > "$TMPDIR/text.txt"
for i in $(seq 1 1000); do echo "Line $i: The quick brown fox jumps over the lazy dog." >> "$TMPDIR/text.txt"; done

# Test 1: --decompress-check with L1
echo "Test 1: --decompress-check L1"
$MCX compress -l 1 --decompress-check "$TMPDIR/text.txt" -o "$TMPDIR/text_l1.mcx"
echo "  PASS"

# Test 2: --decompress-check with L12
echo "Test 2: --decompress-check L12"
$MCX compress -l 12 --decompress-check "$TMPDIR/text.txt" -o "$TMPDIR/text_l12.mcx"
echo "  PASS"

# Test 3: --decompress-check with random data L6
echo "Test 3: --decompress-check L6 random"
$MCX compress -l 6 --decompress-check "$TMPDIR/random.bin" -o "$TMPDIR/random_l6.mcx"
echo "  PASS"

# Test 4: --priority speed
echo "Test 4: --priority speed"
$MCX compress -l 12 --priority speed "$TMPDIR/text.txt" -o "$TMPDIR/text_speed.mcx"
$MCX decompress "$TMPDIR/text_speed.mcx" -o "$TMPDIR/text_speed_dec.txt"
diff "$TMPDIR/text.txt" "$TMPDIR/text_speed_dec.txt"
echo "  PASS"

# Test 5: --priority ratio
echo "Test 5: --priority ratio"
$MCX compress -l 12 --priority ratio "$TMPDIR/text.txt" -o "$TMPDIR/text_ratio.mcx"
$MCX decompress "$TMPDIR/text_ratio.mcx" -o "$TMPDIR/text_ratio_dec.txt"
diff "$TMPDIR/text.txt" "$TMPDIR/text_ratio_dec.txt"
echo "  PASS"

# Test 6: --priority balanced
echo "Test 6: --priority balanced"
$MCX compress -l 12 --priority balanced "$TMPDIR/text.txt" -o "$TMPDIR/text_bal.mcx"
$MCX decompress "$TMPDIR/text_bal.mcx" -o "$TMPDIR/text_bal_dec.txt"
diff "$TMPDIR/text.txt" "$TMPDIR/text_bal_dec.txt"
echo "  PASS"

# Test 7: --repeat with bench
echo "Test 7: bench --repeat 2"
OUTPUT=$($MCX bench -l 1 --repeat 2 "$TMPDIR/text.txt" 2>&1)
echo "$OUTPUT" | grep -q "Run 1/2" || { echo "FAIL: missing Run 1/2 header"; exit 1; }
echo "$OUTPUT" | grep -q "Run 2/2" || { echo "FAIL: missing Run 2/2 header"; exit 1; }
echo "$OUTPUT" | grep -q "Repeat summary" || { echo "FAIL: missing repeat summary"; exit 1; }
echo "  PASS"

echo ""
echo "All tests passed!"
