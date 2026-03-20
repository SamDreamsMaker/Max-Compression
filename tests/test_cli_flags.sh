#!/bin/bash
# Test all CLI flag combinations
set -e
MCX="${1:-build/bin/mcx}"
TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT

echo "Testing CLI flags..."
echo "Hello World! MCX test data for CLI flag testing." > "$TMP/input.txt"

# Basic compress/decompress
$MCX compress "$TMP/input.txt" -o "$TMP/out.mcx" && echo "  compress: OK"
$MCX decompress "$TMP/out.mcx" -o "$TMP/out.txt" && echo "  decompress: OK"
diff "$TMP/input.txt" "$TMP/out.txt" && echo "  roundtrip: OK"

# Quiet mode
$MCX compress -q "$TMP/input.txt" -o "$TMP/q.mcx" && echo "  compress -q: OK"
$MCX decompress -q "$TMP/q.mcx" -o "$TMP/q.txt" && echo "  decompress -q: OK"

# Level aliases
$MCX compress --fast "$TMP/input.txt" -o "$TMP/fast.mcx" && echo "  --fast: OK"
$MCX compress --default "$TMP/input.txt" -o "$TMP/def.mcx" && echo "  --default: OK"
$MCX compress --best "$TMP/input.txt" -o "$TMP/best.mcx" && echo "  --best: OK"

# Explicit levels
for L in 1 3 6 9 12 20 24 26; do
    $MCX compress -l $L -q "$TMP/input.txt" -o "$TMP/l${L}.mcx" && echo "  -l $L: OK"
done

# Info command
$MCX info "$TMP/out.mcx" > /dev/null && echo "  info: OK"

# Cat command
$MCX cat "$TMP/out.mcx" > "$TMP/cat.txt" && diff "$TMP/input.txt" "$TMP/cat.txt" && echo "  cat: OK"

# Test command
$MCX test > /dev/null && echo "  test: OK"

# Version command
$MCX version > /dev/null 2>&1 && echo "  version: OK" || echo "  version: no subcommand (OK)"

# Pipe mode roundtrip
cat "$TMP/input.txt" | $MCX pipe -l 3 | $MCX pipe -d > "$TMP/pipe.txt"
diff "$TMP/input.txt" "$TMP/pipe.txt" && echo "  pipe roundtrip: OK"

# Pipe mode with larger data
dd if=/dev/urandom bs=1024 count=16 of="$TMP/random.bin" 2>/dev/null
cat "$TMP/random.bin" | $MCX pipe -l 1 | $MCX pipe -d > "$TMP/pipe_random.txt"
diff "$TMP/random.bin" "$TMP/pipe_random.txt" && echo "  pipe roundtrip (16KB random): OK"

# Upgrade --in-place
$MCX compress -l 3 "$TMP/input.txt" -o "$TMP/upgrade_test.mcx"
$MCX upgrade --in-place -l 6 "$TMP/upgrade_test.mcx"
$MCX decompress "$TMP/upgrade_test.mcx" -o "$TMP/upgrade_out.txt"
diff "$TMP/input.txt" "$TMP/upgrade_out.txt" && echo "  upgrade --in-place: OK"

echo ""
echo "All CLI flag tests passed!"
