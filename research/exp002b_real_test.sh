#!/bin/bash
# Test Babel Dictionary with actual MCX compression
# Compare: MCX(original) vs MCX(dict_output) + dict_header
set -e

MCX="$(dirname "$0")/../build/bin/mcx"
BABEL="$(dirname "$0")/exp002_babel_dict"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

FILE="${1:-/tmp/cantrbry/alice29.txt}"
LEVEL="${2:-20}"
BASENAME=$(basename "$FILE")

echo "=== Real MCX Test: Babel Dictionary ==="
echo "File: $FILE"
echo "Level: $LEVEL"
echo ""

# Step 1: Compress original
$MCX compress -l "$LEVEL" -q "$FILE" -o "$TMPDIR/original.mcx"
ORIG_MCX=$(stat -c%s "$TMPDIR/original.mcx")
ORIG_SIZE=$(stat -c%s "$FILE")
echo "Original → MCX:     $ORIG_MCX bytes ($(python3 -c "print(f'{$ORIG_SIZE/$ORIG_MCX:.3f}x')"))"

# Step 2: Apply Babel dictionary (extract transformed data)
# For now, write transformed data to file and compress that
# TODO: integrate into MCX pipeline properly
$BABEL "$FILE" > "$TMPDIR/babel_log.txt" 2>&1

# Extract the transformed output size from log
DICT_OUTPUT_SIZE=$(grep "Output size:" "$TMPDIR/babel_log.txt" | grep -oP '\d+(?= bytes)')
DICT_HEADER=$(grep "Dictionary header cost:" "$TMPDIR/babel_log.txt" | grep -oP '\d+(?= bytes)')

# We need the actual transformed file. Let's modify the experiment to write it.
echo ""
echo "=== From Babel analysis ==="
echo "Dict output: $DICT_OUTPUT_SIZE bytes"
echo "Dict header: $DICT_HEADER bytes"
echo ""
echo "=== Theoretical comparison ==="
echo "MCX(original): $ORIG_MCX bytes"
THEORETICAL=$(python3 -c "
# Rough estimate: MCX compresses dict output about as well as original
# but on fewer bytes. The entropy × size ratio predicts the gain.
orig_mcx = $ORIG_MCX
orig_size = $ORIG_SIZE
dict_out = $DICT_OUTPUT_SIZE  
dict_hdr = $DICT_HEADER
# Compression ratio stays roughly similar
ratio = orig_size / orig_mcx
dict_compressed = dict_out / ratio + dict_hdr
print(f'MCX(babel) estimate: {dict_compressed:.0f} bytes ({orig_mcx/dict_compressed:.3f}x vs original MCX)')
print(f'Estimated gain: {(1 - dict_compressed/orig_mcx)*100:.1f}%')
")
echo "$THEORETICAL"

echo ""
echo "NOTE: This is a rough estimate. Real test needs the babel"
echo "transform integrated into MCX to get actual compressed output."
