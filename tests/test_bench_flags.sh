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

# Test --json: output should be valid JSON with "results" array
JSON_OUT=$("$MCX" bench -l 1 --json "$TMPDIR/input.txt" 2>&1)
if ! echo "$JSON_OUT" | grep -q '"results"'; then
    echo "FAIL: --json should produce JSON with 'results' key"
    echo "$JSON_OUT"
    exit 1
fi
if ! echo "$JSON_OUT" | grep -q '"comp_mbs"'; then
    echo "FAIL: --json should contain comp_mbs field"
    exit 1
fi
echo "OK: --json produces valid JSON output"

# Test --csv: output should have CSV header and data line
CSV_OUT=$("$MCX" bench -l 1 --csv "$TMPDIR/input.txt" 2>&1)
if ! echo "$CSV_OUT" | head -1 | grep -q "file,original_bytes"; then
    echo "FAIL: --csv should have CSV header"
    echo "$CSV_OUT"
    exit 1
fi
CSV_LINES=$(echo "$CSV_OUT" | wc -l)
if [ "$CSV_LINES" -lt 2 ]; then
    echo "FAIL: --csv should have at least header + 1 data line"
    exit 1
fi
echo "OK: --csv produces CSV output"

# Test --decode-only: should still produce output with dec_mbs
DECODE_OUT=$("$MCX" bench -l 1 --decode-only "$TMPDIR/input.txt" 2>&1)
if ! echo "$DECODE_OUT" | grep -q "^L1"; then
    echo "FAIL: --decode-only should produce L1 output"
    echo "$DECODE_OUT"
    exit 1
fi
echo "OK: --decode-only produces output"

# Test --sort ratio: first result should have highest ratio
SORT_OUT=$("$MCX" bench --ratio-only --sort ratio "$TMPDIR/input.txt" 2>&1)
# Extract ratios, first should be >= last
FIRST_RATIO=$(echo "$SORT_OUT" | grep "^L[0-9]" | head -1 | awk '{print $3}' | tr -d 'x')
LAST_RATIO=$(echo "$SORT_OUT" | grep "^L[0-9]" | tail -1 | awk '{print $3}' | tr -d 'x')
if [ "$(echo "$FIRST_RATIO >= $LAST_RATIO" | bc 2>/dev/null || echo 1)" = "1" ]; then
    echo "OK: --sort ratio orders by descending ratio ($FIRST_RATIO >= $LAST_RATIO)"
else
    echo "FAIL: --sort ratio: first ratio $FIRST_RATIO should be >= last $LAST_RATIO"
    exit 1
fi

# Test --top N: with --all-levels --sort ratio --top 3, verify exactly 3 data lines
TOP_OUT=$("$MCX" bench --all-levels --sort ratio --top 3 --ratio-only "$TMPDIR/input.txt" 2>&1)
TOP_LINES=$(echo "$TOP_OUT" | grep "^L[0-9]" | wc -l)
if [ "$TOP_LINES" -ne 3 ]; then
    echo "FAIL: --top 3 should produce exactly 3 data lines, got $TOP_LINES"
    echo "$TOP_OUT"
    exit 1
fi
echo "OK: --top 3 produces exactly 3 lines"

# Test --median with --iterations 3: should produce output (median timing)
MEDIAN_OUT=$("$MCX" bench -l 1 --iterations 3 --median "$TMPDIR/input.txt" 2>&1)
if ! echo "$MEDIAN_OUT" | grep -q "^L1"; then
    echo "FAIL: --median should produce L1 output"
    echo "$MEDIAN_OUT"
    exit 1
fi
echo "OK: --median with --iterations 3 produces output"

# Test --percentile with --iterations 5: should show p5/p50/p95
PCTILE_OUT=$("$MCX" bench -l 1 --iterations 5 --percentile "$TMPDIR/input.txt" 2>&1)
if ! echo "$PCTILE_OUT" | grep -q "p5="; then
    echo "FAIL: --percentile should show p5= in output"
    echo "$PCTILE_OUT"
    exit 1
fi
if ! echo "$PCTILE_OUT" | grep -q "p50="; then
    echo "FAIL: --percentile should show p50= in output"
    exit 1
fi
if ! echo "$PCTILE_OUT" | grep -q "p95="; then
    echo "FAIL: --percentile should show p95= in output"
    exit 1
fi
echo "OK: --percentile with --iterations 5 shows p5/p50/p95"

# Test --histogram: should show block size labels and ratio output
HIST_OUT=$("$MCX" bench -l 6 --histogram "$TMPDIR/input.txt" 2>&1)
if ! echo "$HIST_OUT" | grep -qi "block\|size\|ratio\|KB\|MB"; then
    echo "FAIL: --histogram should show block size / ratio output"
    echo "$HIST_OUT"
    exit 1
fi
echo "OK: --histogram produces block size / ratio output"

# Test --format markdown: should show markdown table (pipe characters)
FMT_OUT=$("$MCX" bench -l 1 --format markdown "$TMPDIR/input.txt" 2>&1)
if ! echo "$FMT_OUT" | grep -q "^|"; then
    echo "FAIL: --format markdown should produce markdown table rows"
    echo "$FMT_OUT"
    exit 1
fi
if ! echo "$FMT_OUT" | grep -q "|.*L1.*|"; then
    echo "FAIL: --format markdown should have L1 row in table"
    echo "$FMT_OUT"
    exit 1
fi
echo "OK: --format markdown produces markdown table"

# Test --format csv: should produce CSV output
FMT_CSV=$("$MCX" bench -l 1 --format csv "$TMPDIR/input.txt" 2>&1)
if ! echo "$FMT_CSV" | grep -q "^file,original_bytes"; then
    echo "FAIL: --format csv should produce CSV header"
    echo "$FMT_CSV"
    exit 1
fi
echo "OK: --format csv produces CSV output"

# Test --brief: should produce compact one-line output
BRIEF_OUT=$("$MCX" bench -l 6 --brief "$TMPDIR/input.txt" 2>&1)
if ! echo "$BRIEF_OUT" | grep -q "^L6:"; then
    echo "FAIL: --brief should produce 'L6:' compact output"
    echo "$BRIEF_OUT"
    exit 1
fi
# Should NOT contain "Benchmarking" header
if echo "$BRIEF_OUT" | grep -q "Benchmarking"; then
    echo "FAIL: --brief should not show Benchmarking header"
    exit 1
fi
echo "OK: --brief produces compact one-line output"

# Test L2 vs L3 differentiation: L3 should produce <= L2 output
# Create a larger test file (text-like) where lazy depth 2 can help
python3 -c "import random; random.seed(42); print(''.join(random.choice('abcdefghij ') for _ in range(10000)))" > "$TMPDIR/l2l3test.txt"
L2_SIZE=$("$MCX" compress -l 2 -c "$TMPDIR/l2l3test.txt" | wc -c)
L3_SIZE=$("$MCX" compress -l 3 -c "$TMPDIR/l2l3test.txt" | wc -c)
if [ "$L3_SIZE" -gt "$L2_SIZE" ]; then
    echo "FAIL: L3 ($L3_SIZE) should be <= L2 ($L2_SIZE)"
    exit 1
fi
echo "OK: L3 ($L3_SIZE) <= L2 ($L2_SIZE) — lazy depth 2 works"

# Test --level-range: compress with range, verify roundtrip
"$MCX" compress --level-range 1-6 "$TMPDIR/input.txt" -o "$TMPDIR/range.mcx"
"$MCX" decompress "$TMPDIR/range.mcx" -o "$TMPDIR/range_out.txt"
if ! diff -q "$TMPDIR/input.txt" "$TMPDIR/range_out.txt" > /dev/null 2>&1; then
    echo "FAIL: --level-range roundtrip failed"
    exit 1
fi
echo "OK: --level-range 1-6 roundtrip verified"

# Test --level-range single level (L6-L6)
"$MCX" compress --level-range 6-6 "$TMPDIR/input.txt" -o "$TMPDIR/range_single.mcx"
"$MCX" decompress "$TMPDIR/range_single.mcx" -o "$TMPDIR/range_single_out.txt"
if ! diff -q "$TMPDIR/input.txt" "$TMPDIR/range_single_out.txt" > /dev/null 2>&1; then
    echo "FAIL: --level-range 6-6 single-level roundtrip failed"
    exit 1
fi
echo "OK: --level-range 6-6 single-level roundtrip verified"

# Test --level-range full range (L1-L26 would be slow, test L1-L3)
"$MCX" compress --level-range 1-3 "$TMPDIR/input.txt" -o "$TMPDIR/range_full.mcx"
"$MCX" decompress "$TMPDIR/range_full.mcx" -o "$TMPDIR/range_full_out.txt"
if ! diff -q "$TMPDIR/input.txt" "$TMPDIR/range_full_out.txt" > /dev/null 2>&1; then
    echo "FAIL: --level-range 1-3 roundtrip failed"
    exit 1
fi
echo "OK: --level-range 1-3 roundtrip verified"

# Test --worst: show worst N results
WORST_OUT=$("$MCX" bench --all-levels --worst 3 "$TMPDIR/input.txt" 2>&1)
WORST_LINES=$(echo "$WORST_OUT" | grep "^L[0-9]" | wc -l)
if [ "$WORST_LINES" -ne 3 ]; then
    echo "FAIL: --worst 3 should show exactly 3 result lines, got $WORST_LINES"
    exit 1
fi
echo "OK: --worst 3 shows exactly 3 result lines"

echo "=== All bench flags tests passed ==="
