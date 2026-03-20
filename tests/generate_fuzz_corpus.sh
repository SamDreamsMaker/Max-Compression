#!/bin/bash
# Generate initial fuzz corpus for MCX decompression fuzzing.
# Creates small .mcx files from various data patterns for seeding
# libFuzzer / AFL / OSS-Fuzz corpus directories.
#
# Usage: ./generate_fuzz_corpus.sh <mcx_binary> <output_dir>

set -euo pipefail

MCX="${1:-./build/bin/mcx}"
OUTDIR="${2:-./tests/fuzz_corpus}"
TMPDIR=$(mktemp -d)

if [ ! -x "$MCX" ]; then
    echo "Error: mcx binary not found at '$MCX'"
    echo "Usage: $0 <mcx_binary> <output_dir>"
    exit 1
fi

mkdir -p "$OUTDIR"
count=0

compress_seed() {
    local name="$1" level="$2" input="$3"
    "$MCX" compress -l "$level" "$input" -o "$OUTDIR/seed_${name}_L${level}.mcx" -f -q 2>/dev/null && \
        count=$((count + 1)) || true
}

echo "Generating fuzz corpus seeds..."

# ─── Empty file ──────────────────────────────────────────────────────
touch "$TMPDIR/empty"
compress_seed "empty" 1 "$TMPDIR/empty"

# ─── Single byte ─────────────────────────────────────────────────────
printf '\x00' > "$TMPDIR/single_null"
printf '\xff' > "$TMPDIR/single_ff"
compress_seed "single_null" 1 "$TMPDIR/single_null"
compress_seed "single_ff" 1 "$TMPDIR/single_ff"

# ─── All zeros (various sizes) ───────────────────────────────────────
for sz in 16 256 4096 65536; do
    dd if=/dev/zero bs=1 count=$sz of="$TMPDIR/zeros_$sz" 2>/dev/null
    compress_seed "zeros_$sz" 3 "$TMPDIR/zeros_$sz"
    compress_seed "zeros_$sz" 12 "$TMPDIR/zeros_$sz"
done

# ─── All 0xFF ─────────────────────────────────────────────────────────
for sz in 256 4096; do
    head -c $sz /dev/zero | tr '\0' '\377' > "$TMPDIR/ff_$sz"
    compress_seed "ff_$sz" 6 "$TMPDIR/ff_$sz"
done

# ─── Ascending bytes ─────────────────────────────────────────────────
python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)) * 4)" > "$TMPDIR/ascending" 2>/dev/null || \
    perl -e 'print chr($_) for (0..255) x 4;' > "$TMPDIR/ascending"
compress_seed "ascending" 6 "$TMPDIR/ascending"
compress_seed "ascending" 12 "$TMPDIR/ascending"

# ─── Repeated pattern ────────────────────────────────────────────────
printf 'ABCABCABCABC%.0s' {1..100} > "$TMPDIR/repeat_abc"
compress_seed "repeat_abc" 3 "$TMPDIR/repeat_abc"
compress_seed "repeat_abc" 12 "$TMPDIR/repeat_abc"

# ─── English text ────────────────────────────────────────────────────
cat > "$TMPDIR/english.txt" << 'ENDTEXT'
The quick brown fox jumps over the lazy dog. This is a sample text file
used for fuzz corpus seeding. MaxCompression should handle this gracefully
with all compression levels and strategies. Lorem ipsum dolor sit amet,
consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore
et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation
ullamco laboris nisi ut aliquip ex ea commodo consequat.
ENDTEXT
for level in 1 3 6 9 12; do
    compress_seed "english" "$level" "$TMPDIR/english.txt"
done

# ─── Pseudo-random data ──────────────────────────────────────────────
dd if=/dev/urandom bs=1 count=1024 of="$TMPDIR/random_1k" 2>/dev/null
dd if=/dev/urandom bs=1 count=8192 of="$TMPDIR/random_8k" 2>/dev/null
compress_seed "random_1k" 3 "$TMPDIR/random_1k"
compress_seed "random_8k" 6 "$TMPDIR/random_8k"

# ─── Binary-like structured data ────────────────────────────────────
python3 -c "
import struct, sys
buf = b''
for i in range(256):
    buf += struct.pack('<IHBx', i * 1000, i, i & 0xFF)
sys.stdout.buffer.write(buf)
" > "$TMPDIR/structured" 2>/dev/null && {
    compress_seed "structured" 6 "$TMPDIR/structured"
    compress_seed "structured" 12 "$TMPDIR/structured"
}

# ─── Run-heavy data ──────────────────────────────────────────────────
python3 -c "
import sys
buf = b''
for i in range(50):
    buf += bytes([i & 0xFF]) * (i * 10 + 1)
sys.stdout.buffer.write(buf)
" > "$TMPDIR/runs" 2>/dev/null && {
    compress_seed "runs" 6 "$TMPDIR/runs"
    compress_seed "runs" 12 "$TMPDIR/runs"
}

# ─── LZRC levels ─────────────────────────────────────────────────────
compress_seed "english_lzrc" 21 "$TMPDIR/english.txt"
compress_seed "random_lzrc" 21 "$TMPDIR/random_1k"

# ─── Minimal valid MCX frame (raw, for mutation) ─────────────────────
# Copy smallest compressed files as base for mutation
if [ -f "$OUTDIR/seed_single_null_L1.mcx" ]; then
    cp "$OUTDIR/seed_single_null_L1.mcx" "$OUTDIR/seed_minimal_frame.mcx"
    count=$((count + 1))
fi

# Cleanup
rm -rf "$TMPDIR"

echo "Generated $count seed files in $OUTDIR/"
ls -la "$OUTDIR/" | head -30
