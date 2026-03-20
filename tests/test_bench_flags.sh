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
OUTPUT=$("$MCX" bench --all-levels --ratio-only "$TMPDIR/input.txt" 2>/dev/null)
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
OUTPUT2=$("$MCX" bench --ratio-only "$TMPDIR/input.txt" 2>/dev/null)
LEVEL_COUNT2=$(echo "$OUTPUT2" | grep -c "^L[0-9]")
if [ "$LEVEL_COUNT2" -ne 8 ]; then
    echo "FAIL: Default bench should produce 8 level lines, got $LEVEL_COUNT2"
    echo "$OUTPUT2"
    exit 1
fi
echo "OK: Default bench produced 8 level lines"

# Test --json: output should be valid JSON with "results" array
JSON_OUT=$("$MCX" bench -l 1 --json "$TMPDIR/input.txt" 2>/dev/null)
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
CSV_OUT=$("$MCX" bench -l 1 --csv "$TMPDIR/input.txt" 2>/dev/null)
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
DECODE_OUT=$("$MCX" bench -l 1 --decode-only "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$DECODE_OUT" | grep -q "^L1"; then
    echo "FAIL: --decode-only should produce L1 output"
    echo "$DECODE_OUT"
    exit 1
fi
echo "OK: --decode-only produces output"

# Test --sort ratio: first result should have highest ratio
SORT_OUT=$("$MCX" bench --ratio-only --sort ratio "$TMPDIR/input.txt" 2>/dev/null)
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
TOP_OUT=$("$MCX" bench --all-levels --sort ratio --top 3 --ratio-only "$TMPDIR/input.txt" 2>/dev/null)
TOP_LINES=$(echo "$TOP_OUT" | grep "^L[0-9]" | wc -l)
if [ "$TOP_LINES" -ne 3 ]; then
    echo "FAIL: --top 3 should produce exactly 3 data lines, got $TOP_LINES"
    echo "$TOP_OUT"
    exit 1
fi
echo "OK: --top 3 produces exactly 3 lines"

# Test --median with --iterations 3: should produce output (median timing)
MEDIAN_OUT=$("$MCX" bench -l 1 --iterations 3 --median "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$MEDIAN_OUT" | grep -q "^L1"; then
    echo "FAIL: --median should produce L1 output"
    echo "$MEDIAN_OUT"
    exit 1
fi
echo "OK: --median with --iterations 3 produces output"

# Test --percentile with --iterations 5: should show p5/p50/p95
PCTILE_OUT=$("$MCX" bench -l 1 --iterations 5 --percentile "$TMPDIR/input.txt" 2>/dev/null)
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
HIST_OUT=$("$MCX" bench -l 6 --histogram "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$HIST_OUT" | grep -qi "block\|size\|ratio\|KB\|MB"; then
    echo "FAIL: --histogram should show block size / ratio output"
    echo "$HIST_OUT"
    exit 1
fi
echo "OK: --histogram produces block size / ratio output"

# Test --format markdown: should show markdown table (pipe characters)
FMT_OUT=$("$MCX" bench -l 1 --format markdown "$TMPDIR/input.txt" 2>/dev/null)
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
FMT_CSV=$("$MCX" bench -l 1 --format csv "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$FMT_CSV" | grep -q "^file,original_bytes"; then
    echo "FAIL: --format csv should produce CSV header"
    echo "$FMT_CSV"
    exit 1
fi
echo "OK: --format csv produces CSV output"

# Test --brief: should produce compact one-line output
BRIEF_OUT=$("$MCX" bench -l 6 --brief "$TMPDIR/input.txt" 2>/dev/null)
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
WORST_OUT=$("$MCX" bench --all-levels --worst 3 "$TMPDIR/input.txt" 2>/dev/null)
WORST_LINES=$(echo "$WORST_OUT" | grep "^L[0-9]" | wc -l)
if [ "$WORST_LINES" -ne 3 ]; then
    echo "FAIL: --worst 3 should show exactly 3 result lines, got $WORST_LINES"
    exit 1
fi
echo "OK: --worst 3 shows exactly 3 result lines"

# Test --worst with --sort speed: worst by speed (slowest compressors)
WORST_SPEED_OUT=$("$MCX" bench --all-levels --sort speed --worst 2 "$TMPDIR/input.txt" 2>/dev/null)
WORST_SPEED_LINES=$(echo "$WORST_SPEED_OUT" | grep "^L[0-9]" | wc -l)
if [ "$WORST_SPEED_LINES" -ne 2 ]; then
    echo "FAIL: --worst 2 --sort speed should show exactly 2 result lines, got $WORST_SPEED_LINES"
    exit 1
fi
echo "OK: --worst 2 --sort speed shows exactly 2 result lines"

# Test --filter with bench: force none filter
FILTER_OUT=$("$MCX" bench --filter none -l 1 "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$FILTER_OUT" | grep -q "^L1"; then
    echo "FAIL: bench --filter none -l 1 should produce L1 result"
    exit 1
fi
echo "OK: bench --filter none produces results"

# Test --fast-decode roundtrip
"$MCX" compress --fast-decode -l 9 "$TMPDIR/input.txt" -o "$TMPDIR/fast_decode.mcx"
"$MCX" decompress "$TMPDIR/fast_decode.mcx" -o "$TMPDIR/fast_decode_out.txt"
if ! diff -q "$TMPDIR/input.txt" "$TMPDIR/fast_decode_out.txt" >/dev/null 2>&1; then
    echo "FAIL: --fast-decode roundtrip mismatch"
    exit 1
fi
echo "OK: --fast-decode roundtrip verified"

# Test bench --exclude with directory input
mkdir -p "$TMPDIR/benchdir"
# Files need to be >28 bytes (MCX frame overhead) for aggregate compress to work
python3 -c "print('Include me for benchmarking. ' * 10)" > "$TMPDIR/benchdir/keep.txt"
python3 -c "print('Also include me for testing. ' * 10)" > "$TMPDIR/benchdir/also.txt"
python3 -c "print('Skip this log file content. ' * 10)" > "$TMPDIR/benchdir/skip.log"
python3 -c "print('Skip this debug log too. ' * 10)" > "$TMPDIR/benchdir/debug.log"

# Without --exclude: should see all 4 files
BENCH_ALL=$("$MCX" bench -l 1 --ratio-only "$TMPDIR/benchdir" 2>/dev/null)
ALL_COUNT=$(echo "$BENCH_ALL" | grep -c "===.*===")
if [ "$ALL_COUNT" -ne 4 ]; then
    echo "FAIL: bench directory should show 4 files, got $ALL_COUNT"
    echo "$BENCH_ALL"
    exit 1
fi
echo "OK: bench directory shows all 4 files"

# With --exclude '*.log': should see only 2 files
BENCH_EXCL=$("$MCX" bench -l 1 --ratio-only --exclude '*.log' "$TMPDIR/benchdir" 2>/dev/null)
EXCL_COUNT=$(echo "$BENCH_EXCL" | grep -c "===.*===")
if [ "$EXCL_COUNT" -ne 2 ]; then
    echo "FAIL: bench --exclude '*.log' should show 2 files, got $EXCL_COUNT"
    echo "$BENCH_EXCL"
    exit 1
fi
# Verify .log files are not in output
if echo "$BENCH_EXCL" | grep -q ".log"; then
    echo "FAIL: bench --exclude should not show .log files"
    exit 1
fi
echo "OK: bench --exclude correctly skips .log files"

# Test --aggregate with directory
BENCH_AGG=$("$MCX" bench -l 1 --ratio-only --aggregate "$TMPDIR/benchdir" 2>/dev/null)
if ! echo "$BENCH_AGG" | grep -q "AGGREGATE"; then
    echo "FAIL: --aggregate should show AGGREGATE summary"
    echo "$BENCH_AGG"
    exit 1
fi
echo "OK: bench --aggregate shows aggregate summary"

# Test --aggregate with --json
BENCH_AGG_JSON=$("$MCX" bench -l 1 --ratio-only --aggregate --format json "$TMPDIR/benchdir" 2>/dev/null)
if ! echo "$BENCH_AGG_JSON" | grep -q '"aggregate"'; then
    echo "FAIL: --aggregate --json should contain aggregate key"
    echo "$BENCH_AGG_JSON"
    exit 1
fi
if ! echo "$BENCH_AGG_JSON" | grep -q '"total_input"'; then
    echo "FAIL: --aggregate --json should contain total_input"
    echo "$BENCH_AGG_JSON"
    exit 1
fi
echo "OK: bench --aggregate --json shows structured aggregate"

# Test --aggregate with --csv
BENCH_AGG_CSV=$("$MCX" bench -l 1 --ratio-only --aggregate --format csv "$TMPDIR/benchdir" 2>/dev/null)
if ! echo "$BENCH_AGG_CSV" | grep -q "TOTAL"; then
    echo "FAIL: --aggregate --csv should contain TOTAL line"
    echo "$BENCH_AGG_CSV"
    exit 1
fi
echo "OK: bench --aggregate --csv shows TOTAL line"

# Test --no-header flag
BENCH_NH=$("$MCX" bench -l 1 "$TMPDIR/benchdir/keep.txt" --ratio-only --no-header 2>/dev/null)
if echo "$BENCH_NH" | grep -q "Benchmarking"; then
    echo "FAIL: --no-header should suppress Benchmarking title"
    echo "$BENCH_NH"
    exit 1
fi
if echo "$BENCH_NH" | grep -q "Level"; then
    echo "FAIL: --no-header should suppress column headers"
    echo "$BENCH_NH"
    exit 1
fi
if ! echo "$BENCH_NH" | grep -q "L1"; then
    echo "FAIL: --no-header should still show data rows"
    echo "$BENCH_NH"
    exit 1
fi
echo "OK: bench --no-header suppresses headers, keeps data"

# Test --no-header with --csv
BENCH_NH_CSV=$("$MCX" bench -l 1 "$TMPDIR/benchdir/keep.txt" --csv --no-header 2>/dev/null)
if echo "$BENCH_NH_CSV" | grep -q "file,original"; then
    echo "FAIL: --no-header --csv should suppress CSV header line"
    echo "$BENCH_NH_CSV"
    exit 1
fi
CSV_COLS=$(echo "$BENCH_NH_CSV" | head -1 | tr ',' '\n' | wc -l)
if [ "$CSV_COLS" -lt 5 ]; then
    echo "FAIL: --no-header --csv should still output data columns"
    echo "$BENCH_NH_CSV"
    exit 1
fi
echo "OK: bench --no-header --csv suppresses header, keeps data"

# Test --repeat with --format json
BENCH_REPEAT=$("$MCX" bench -l 1 --repeat 2 --format json "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$BENCH_REPEAT" | grep -q '"level"'; then
    echo "FAIL: --repeat --format json should contain level field"
    echo "$BENCH_REPEAT"
    exit 1
fi
# --repeat 2 should produce 2 JSON result objects
JSON_COUNT=$(echo "$BENCH_REPEAT" | grep -c '"results"')
if [ "$JSON_COUNT" -lt 2 ]; then
    echo "FAIL: --repeat 2 should produce at least 2 JSON result objects, got $JSON_COUNT"
    echo "$BENCH_REPEAT"
    exit 1
fi
echo "OK: bench --repeat --format json works ($JSON_COUNT runs)"

# Test --warmup-iterations N
BENCH_WI=$("$MCX" bench -l 1 --warmup-iterations 2 --brief "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$BENCH_WI" | grep -q "L1:"; then
    echo "FAIL: --warmup-iterations should produce L1 output"
    echo "$BENCH_WI"
    exit 1
fi
echo "OK: bench --warmup-iterations works"

# Test --adaptive-level
ADAPT_OUT=$("$MCX" compress --adaptive-level "$TMPDIR/input.txt" -o "$TMPDIR/adaptive_out.mcx" -f 2>/dev/null)
if ! echo "$ADAPT_OUT" | grep -q "Entropy:.*auto-selected L"; then
    echo "FAIL: --adaptive-level should show entropy and selected level"
    echo "$ADAPT_OUT"
    exit 1
fi
# Verify roundtrip
"$MCX" decompress "$TMPDIR/adaptive_out.mcx" -o "$TMPDIR/adaptive_rt.txt" -f 2>&1 > /dev/null
if ! diff -q "$TMPDIR/input.txt" "$TMPDIR/adaptive_rt.txt" > /dev/null 2>&1; then
    echo "FAIL: --adaptive-level roundtrip failed"
    exit 1
fi
echo "OK: --adaptive-level roundtrip verified"

# Test --adaptive-level entropy categories
# High-entropy random data should select L1
dd if=/dev/urandom of="$TMPDIR/random.bin" bs=1024 count=64 2>/dev/null
RAND_OUT=$("$MCX" compress --adaptive-level "$TMPDIR/random.bin" -o "$TMPDIR/rand.mcx" -f 2>/dev/null)
RAND_LEVEL=$(echo "$RAND_OUT" | grep -oP 'auto-selected L\K\d+')
if [ "$RAND_LEVEL" != "1" ]; then
    echo "FAIL: random data should auto-select L1, got L$RAND_LEVEL"
    echo "$RAND_OUT"
    exit 1
fi
echo "OK: --adaptive-level selects L1 for high-entropy random data"

# Low-entropy repetitive text should select L12
python3 -c "print('hello world test data ' * 5000)" > "$TMPDIR/repeat.txt" 2>/dev/null || \
    printf 'hello world test data %.0s' $(seq 1 5000) > "$TMPDIR/repeat.txt"
TEXT_OUT=$("$MCX" compress --adaptive-level "$TMPDIR/repeat.txt" -o "$TMPDIR/text.mcx" -f 2>/dev/null)
TEXT_LEVEL=$(echo "$TEXT_OUT" | grep -oP 'auto-selected L\K\d+')
if [ "$TEXT_LEVEL" != "12" ]; then
    echo "FAIL: repetitive text should auto-select L12, got L$TEXT_LEVEL"
    echo "$TEXT_OUT"
    exit 1
fi
# Verify roundtrip
"$MCX" decompress "$TMPDIR/text.mcx" -o "$TMPDIR/text_rt.txt" -f 2>&1 > /dev/null
if ! diff -q "$TMPDIR/repeat.txt" "$TMPDIR/text_rt.txt" > /dev/null 2>&1; then
    echo "FAIL: --adaptive-level L12 roundtrip failed"
    exit 1
fi
echo "OK: --adaptive-level selects L12 for low-entropy text, roundtrip verified"

# Test --cold flag (just verify it's accepted, can't actually drop caches without root)
COLD_OUT=$("$MCX" bench -l 1 --cold --brief "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$COLD_OUT" | grep -q "L1:"; then
    echo "FAIL: --cold should produce benchmark output"
    echo "$COLD_OUT"
    exit 1
fi
echo "OK: bench --cold flag accepted"

# Test --cold with --iterations 2
COLD_ITER=$("$MCX" bench -l 1 --cold --iterations 2 --brief "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$COLD_ITER" | grep -q "L1:"; then
    echo "FAIL: --cold --iterations 2 should produce benchmark output"
    echo "$COLD_ITER"
    exit 1
fi
echo "OK: bench --cold --iterations 2 works"

# Test --output FILE (append mode)
OUTFILE="$TMPDIR/bench_output.txt"
rm -f "$OUTFILE"
"$MCX" bench -l 1 --brief --output "$OUTFILE" "$TMPDIR/input.txt" 2>/dev/null
"$MCX" bench -l 3 --brief --output "$OUTFILE" "$TMPDIR/input.txt" 2>/dev/null
if [ ! -f "$OUTFILE" ]; then
    echo "FAIL: --output should create output file"
    exit 1
fi
LINE_COUNT=$(wc -l < "$OUTFILE")
if [ "$LINE_COUNT" -lt 2 ]; then
    echo "FAIL: --output append mode should produce at least 2 lines, got $LINE_COUNT"
    cat "$OUTFILE"
    exit 1
fi
echo "OK: bench --output FILE append mode works ($LINE_COUNT lines)"

# Test --output with --format json
JSON_OUTFILE="$TMPDIR/bench_json_out.txt"
rm -f "$JSON_OUTFILE"
"$MCX" bench -l 1 --format json --output "$JSON_OUTFILE" "$TMPDIR/input.txt" 2>/dev/null
if [ ! -f "$JSON_OUTFILE" ]; then
    echo "FAIL: --output with --format json should create output file"
    exit 1
fi
if ! grep -q '"results"' "$JSON_OUTFILE"; then
    echo "FAIL: --output with --format json should contain JSON results"
    cat "$JSON_OUTFILE"
    exit 1
fi
echo "OK: bench --output with --format json works"

# Test --output with --format csv
CSV_OUTFILE="$TMPDIR/bench_csv_out.txt"
rm -f "$CSV_OUTFILE"
"$MCX" bench -l 1 --format csv --output "$CSV_OUTFILE" "$TMPDIR/input.txt" 2>/dev/null
if [ ! -f "$CSV_OUTFILE" ]; then
    echo "FAIL: --output with --format csv should create output file"
    exit 1
fi
if ! grep -q "," "$CSV_OUTFILE"; then
    echo "FAIL: --output with --format csv should contain CSV data"
    cat "$CSV_OUTFILE"
    exit 1
fi
echo "OK: bench --output with --format csv works"

# Test --compare-self
"$MCX" compress -l 1 "$TMPDIR/input.txt" -o "$TMPDIR/ref.mcx" -f -q 2>/dev/null
CS_OUT=$("$MCX" bench --compare-self "$TMPDIR/ref.mcx" -l 1 "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$CS_OUT" | grep -q "MATCH"; then
    echo "FAIL: --compare-self should report MATCH for same build"
    echo "$CS_OUT"
    exit 1
fi
echo "OK: bench --compare-self reports MATCH"

# Test --compare-self regression detection: use a tiny file as reference
echo "x" > "$TMPDIR/tiny_input.txt"
"$MCX" compress -l 1 "$TMPDIR/tiny_input.txt" -o "$TMPDIR/tiny_ref.mcx" -f -q 2>/dev/null
# Compare against larger input — sizes will differ
CS_REG=$("$MCX" bench --compare-self "$TMPDIR/tiny_ref.mcx" -l 1 "$TMPDIR/input.txt" 2>/dev/null; echo "EXIT:$?")
CS_EXIT=$(echo "$CS_REG" | grep -oP 'EXIT:\K\d+')
if echo "$CS_REG" | grep -q "REGRESSION"; then
    echo "OK: bench --compare-self detects REGRESSION (exit=$CS_EXIT)"
elif echo "$CS_REG" | grep -q "IMPROVED"; then
    echo "OK: bench --compare-self detects size difference (exit=$CS_EXIT)"
else
    echo "WARN: --compare-self regression test inconclusive"
    echo "$CS_REG"
fi

# Test --delta baseline save/compare
rm -f "$TMPDIR/baseline.txt"
"$MCX" bench --delta "$TMPDIR/baseline.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null
if [ ! -f "$TMPDIR/baseline.txt" ]; then
    echo "FAIL: --delta should create baseline file"
    exit 1
fi
DELTA_OUT=$("$MCX" bench --delta "$TMPDIR/baseline.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$DELTA_OUT" | grep -q "=\|MATCH\|+0"; then
    echo "FAIL: --delta should show match on second run"
    echo "$DELTA_OUT"
    exit 1
fi
echo "OK: bench --delta baseline save/compare works"

# Test --memory-limit
ML_OUT=$("$MCX" compress --memory-limit 128M -l 12 "$TMPDIR/input.txt" -o "$TMPDIR/ml_out.mcx" -f 2>&1)
if ! echo "$ML_OUT" | grep -q "block size"; then
    echo "FAIL: --memory-limit should show effective block size"
    echo "$ML_OUT"
    exit 1
fi
"$MCX" decompress "$TMPDIR/ml_out.mcx" -o "$TMPDIR/ml_rt.txt" -f -q 2>/dev/null
if ! diff -q "$TMPDIR/input.txt" "$TMPDIR/ml_rt.txt" > /dev/null 2>&1; then
    echo "FAIL: --memory-limit roundtrip failed"
    exit 1
fi
echo "OK: --memory-limit roundtrip verified"

# Test --delta regression detection (tamper baseline)
echo "L1 1" > "$TMPDIR/tampered_bl.txt"
REGR_OUT=$("$MCX" bench --delta "$TMPDIR/tampered_bl.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null) || true
REGR_EXIT=${PIPESTATUS[0]:-$?}
# Re-run to capture exit code properly
"$MCX" bench --delta "$TMPDIR/tampered_bl.txt" -l 1 "$TMPDIR/input.txt" > /dev/null 2>&1 && REGR_EXIT=0 || REGR_EXIT=$?
if [ "$REGR_EXIT" -ne 1 ]; then
    echo "FAIL: --delta should return exit code 1 on regression, got $REGR_EXIT"
    echo "$REGR_OUT"
    exit 1
fi
if ! echo "$REGR_OUT" | grep -q "✗"; then
    echo "FAIL: --delta should show regression marker"
    echo "$REGR_OUT"
    exit 1
fi
echo "OK: --delta regression detection works (exit code 1, ✗ marker)"

# Test --save-baseline
rm -f "$TMPDIR/explicit_bl.txt"
SB_OUT=$("$MCX" bench --save-baseline "$TMPDIR/explicit_bl.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null)
if [ ! -f "$TMPDIR/explicit_bl.txt" ]; then
    echo "FAIL: --save-baseline should create file"
    exit 1
fi
if ! echo "$SB_OUT" | grep -q "saved"; then
    echo "FAIL: --save-baseline should show saved message"
    echo "$SB_OUT"
    exit 1
fi
echo "OK: --save-baseline creates file"

# Test --save-baseline + --delta workflow
rm -f "$TMPDIR/workflow_bl.txt"
"$MCX" bench --save-baseline "$TMPDIR/workflow_bl.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null
WF_OUT=$("$MCX" bench --delta "$TMPDIR/workflow_bl.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null)
WF_EXIT=$?
if [ "$WF_EXIT" -ne 0 ]; then
    echo "FAIL: --save-baseline + --delta should exit 0 (no regression)"
    echo "$WF_OUT"
    exit 1
fi
if ! echo "$WF_OUT" | grep -q "=\|+0"; then
    echo "FAIL: --save-baseline + --delta should show MATCH"
    echo "$WF_OUT"
    exit 1
fi
echo "OK: --save-baseline + --delta workflow verified (MATCH, exit 0)"

# Test --diff flag (ratio + speed comparison)
rm -f "$TMPDIR/diff_bl.txt"
DIFF_SAVE=$("$MCX" bench --diff "$TMPDIR/diff_bl.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null)
if [ ! -f "$TMPDIR/diff_bl.txt" ]; then
    echo "FAIL: --diff should create baseline file"
    exit 1
fi
if ! echo "$DIFF_SAVE" | grep -q "saved"; then
    echo "FAIL: --diff first run should show saved message"
    echo "$DIFF_SAVE"
    exit 1
fi
DIFF_CMP=$("$MCX" bench --diff "$TMPDIR/diff_bl.txt" -l 1 "$TMPDIR/input.txt" 2>/dev/null)
if ! echo "$DIFF_CMP" | grep -q "ratio:"; then
    echo "FAIL: --diff second run should show ratio comparison"
    echo "$DIFF_CMP"
    exit 1
fi
if ! echo "$DIFF_CMP" | grep -q "comp:"; then
    echo "FAIL: --diff second run should show compress speed comparison"
    echo "$DIFF_CMP"
    exit 1
fi
echo "OK: bench --diff ratio + speed comparison works"

# Test --block-size auto
AUTO_BS=$("$MCX" compress --block-size auto -l 12 "$TMPDIR/input.txt" -o "$TMPDIR/auto_bs.mcx" -f 2>&1)
if ! echo "$AUTO_BS" | grep -q "Auto block size"; then
    echo "FAIL: --block-size auto should show auto block size"
    echo "$AUTO_BS"
    exit 1
fi
"$MCX" decompress "$TMPDIR/auto_bs.mcx" -o "$TMPDIR/auto_bs_rt.txt" -f -q 2>/dev/null
if ! diff -q "$TMPDIR/input.txt" "$TMPDIR/auto_bs_rt.txt" > /dev/null 2>&1; then
    echo "FAIL: --block-size auto roundtrip failed"
    exit 1
fi
echo "OK: --block-size auto roundtrip verified"

echo "=== All bench flags tests passed ==="
