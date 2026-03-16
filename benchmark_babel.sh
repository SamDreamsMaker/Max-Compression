#!/bin/bash
# Benchmark MCX Babel (L20) vs BWT (L12) vs LZ77 (L3) on real corpora
set -e

MCX="LD_LIBRARY_PATH=build/bin build/bin/mcx"
RESULTS="babel_benchmark_results.txt"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "=== MCX Babel Transform Real-World Benchmark ===" > "$RESULTS"
echo "Date: $(date)" >> "$RESULTS"
echo "" >> "$RESULTS"

benchmark_file() {
    local file="$1"
    local name="$2"
    local orig_size=$(stat -c%s "$file")
    
    echo "Testing: $name ($orig_size bytes)"
    echo "### $name ($orig_size bytes)" >> "$RESULTS"
    
    for level in 3 12 20; do
        local label=""
        case $level in
            3)  label="L3-LZ77" ;;
            12) label="L12-BWT" ;;
            20) label="L20-Babel" ;;
        esac
        
        local outfile="$TMPDIR/out.mcx"
        local decfile="$TMPDIR/dec"
        
        # Compress
        local start=$(date +%s%N)
        eval $MCX compress -l $level "$file" -o "$outfile" 2>/dev/null
        local end=$(date +%s%N)
        local comp_ms=$(( (end - start) / 1000000 ))
        
        if [ -f "$outfile" ]; then
            local comp_size=$(stat -c%s "$outfile")
            local ratio=$(python3 -c "print(f'{$orig_size/$comp_size:.3f}')")
            local bpb=$(python3 -c "print(f'{$comp_size*8/$orig_size:.3f}')")
            
            # Decompress and verify
            eval $MCX decompress "$outfile" -o "$decfile" 2>/dev/null
            local valid="FAIL"
            if [ -f "$decfile" ]; then
                if diff -q "$file" "$decfile" > /dev/null 2>&1; then
                    valid="OK"
                fi
                rm -f "$decfile"
            fi
            
            echo "  $label: ${comp_size}B ratio=${ratio}x bpb=${bpb} time=${comp_ms}ms roundtrip=$valid"
            echo "  $label: ${comp_size}B ratio=${ratio}x bpb=${bpb} time=${comp_ms}ms roundtrip=$valid" >> "$RESULTS"
        else
            echo "  $label: FAILED"
            echo "  $label: FAILED" >> "$RESULTS"
        fi
        rm -f "$outfile"
    done
    echo "" >> "$RESULTS"
}

echo "=== Canterbury Corpus ==="
echo "## Canterbury Corpus" >> "$RESULTS"
for f in corpora/alice29.txt corpora/asyoulik.txt corpora/cp.html corpora/fields.c corpora/grammar.lsp corpora/kennedy.xls corpora/lcet10.txt corpora/plrabn12.txt corpora/ptt5 corpora/sum corpora/xargs.1; do
    if [ -f "$f" ]; then
        benchmark_file "$f" "$(basename $f)"
    fi
done

echo "=== Silesia Corpus ==="
echo "## Silesia Corpus" >> "$RESULTS"
for f in corpora/silesia/*; do
    if [ -f "$f" ]; then
        benchmark_file "$f" "silesia/$(basename $f)"
    fi
done

echo "=== Project Source Files ==="
echo "## Project Source Files" >> "$RESULTS"
for f in lib/core.c lib/babel/babel_transform.c tests/test_babel.c README.md; do
    if [ -f "$f" ]; then
        benchmark_file "$f" "src/$(basename $f)"
    fi
done

echo ""
echo "Results saved to $RESULTS"
cat "$RESULTS"
