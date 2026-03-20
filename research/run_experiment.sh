#!/bin/bash
# MCX Autoresearch — Run one experiment and measure results
set -e

NAME="${1:-unnamed}"
MCX="$(dirname "$0")/../build/bin/mcx"
RESULTS="$(dirname "$0")/results.tsv"
CORPUS_FILES=("/tmp/cantrbry/alice29.txt" "/tmp/silesia/ooffice" "/tmp/silesia/nci")
CORPUS_NAMES=("alice29" "ooffice" "nci")
CORPUS_SIZES=(152089 6152192 33553445)

# Build
echo "=== Building... ==="
cd "$(dirname "$0")/.."
cmake --build build --target mcx maxcomp_static -j2 2>&1 | tail -3
if [ $? -ne 0 ]; then
    echo "FAIL: Build failed"
    echo -e "$NAME\tBUILD_FAIL\t-\t-\t-\t$(date -Iseconds)" >> "$RESULTS"
    exit 1
fi

# Roundtrip test
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

$MCX compress -l 20 -q "${CORPUS_FILES[0]}" -o "$TMPDIR/test.mcx"
$MCX decompress -q "$TMPDIR/test.mcx" -o "$TMPDIR/test.dec"
if ! diff -q "${CORPUS_FILES[0]}" "$TMPDIR/test.dec" > /dev/null 2>&1; then
    echo "FAIL: Roundtrip test failed!"
    echo -e "$NAME\tROUNDTRIP_FAIL\t-\t-\t-\t$(date -Iseconds)" >> "$RESULTS"
    exit 1
fi

# Measure
TOTAL_ORIG=0
TOTAL_COMP=0
DETAILS=""

for i in 0 1 2; do
    rm -f "$TMPDIR/out.mcx"
    $MCX compress -l 20 -q "${CORPUS_FILES[$i]}" -o "$TMPDIR/out.mcx"
    COMP_SIZE=$(stat -c%s "$TMPDIR/out.mcx")
    ORIG_SIZE=${CORPUS_SIZES[$i]}
    INFO=$(python3 -c "print(f'{$ORIG_SIZE/$COMP_SIZE:.3f}x {$COMP_SIZE*8/$ORIG_SIZE:.4f}bpb')")
    DETAILS="${DETAILS}${CORPUS_NAMES[$i]}=${COMP_SIZE}(${INFO}) "
    TOTAL_ORIG=$((TOTAL_ORIG + ORIG_SIZE))
    TOTAL_COMP=$((TOTAL_COMP + COMP_SIZE))
done

COMPOSITE=$(python3 -c "print(f'{$TOTAL_COMP*8/$TOTAL_ORIG:.4f}')")
RATIO=$(python3 -c "print(f'{$TOTAL_ORIG/$TOTAL_COMP:.3f}')")

echo "=== Results: $NAME ==="
echo "$DETAILS"
echo "Composite: ${COMPOSITE} bpb (${RATIO}x)"

echo -e "$NAME\tOK\t$COMPOSITE\t$RATIO\t$DETAILS\t$(date -Iseconds)" >> "$RESULTS"
echo "=== Done ==="
