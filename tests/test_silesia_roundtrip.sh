#!/bin/bash
# Integration test: compress/decompress each Silesia corpus file, verify roundtrip
# Tests L1, L6, L12 on all corpus files to ensure lossless compression.
set -e
MCX="${1:-build/bin/mcx}"
CORPUS="corpora/silesia"

if [ ! -d "$CORPUS" ]; then
    echo "SKIP: Silesia corpus not found at $CORPUS"
    exit 0
fi

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT

PASS=0
FAIL=0
LEVELS="1 6 12"

for file in "$CORPUS"/*; do
    [ -f "$file" ] || continue
    name=$(basename "$file")
    # Skip .xz files
    case "$name" in *.xz) continue;; esac
    
    for L in $LEVELS; do
        out="$TMP/${name}.L${L}.mcx"
        dec="$TMP/${name}.L${L}.dec"
        
        if $MCX compress -l "$L" -q "$file" -o "$out" 2>/dev/null; then
            if $MCX decompress -q "$out" -o "$dec" 2>/dev/null; then
                if diff -q "$file" "$dec" >/dev/null 2>&1; then
                    PASS=$((PASS + 1))
                else
                    echo "FAIL: $name L$L — roundtrip mismatch"
                    FAIL=$((FAIL + 1))
                fi
            else
                echo "FAIL: $name L$L — decompress error"
                FAIL=$((FAIL + 1))
            fi
        else
            echo "FAIL: $name L$L — compress error"
            FAIL=$((FAIL + 1))
        fi
        rm -f "$out" "$dec"
    done
done

echo "Silesia roundtrip: $PASS passed, $FAIL failed"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
if [ "$PASS" -eq 0 ]; then
    echo "WARN: no files tested"
    exit 0
fi
echo "All Silesia roundtrip tests passed!"
