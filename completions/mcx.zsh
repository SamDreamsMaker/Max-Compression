#compdef mcx

_mcx() {
    local -a commands
    commands=(
        'compress:Compress files'
        'decompress:Decompress .mcx files'
        'extract:Decompress (alias)'
        'info:Show file information'
        'cat:Decompress to stdout'
        'stat:Show file statistics (entropy, bytes)'
        'hash:Show CRC32/FNV hash of content'
        'checksum:Show CRC32 of compressed file'
        'bench:Benchmark all levels'
        'compare:Benchmark (alias)'
        'test:Run self-tests'
        'verify:Verify file integrity'
        'diff:Compare two compressed files'
        'ls:List compressed files'
        'list:List compressed files (alias)'
        'upgrade:Re-compress at different level'
        'recompress:Re-compress (alias)'
        'pipe:Streaming compress/decompress'
        'version:Show version info'
    )

    local -a compress_opts
    compress_opts=(
        '-l[Compression level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '--level[Compression level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '--fast[Fast compression (L3)]'
        '--default[Default compression (L6)]'
        '--best[Alias for --level-scan]'
        '--strategy[Force strategy]:strategy:(lz bwt lzrc stride)'
        '--block-size[Override BWT block size]:size:'
        '--verify[Verify roundtrip after compress]'
        '--dry-run[Predict strategy without compressing]'
        '--estimate[Estimate compressed size]'
        '--level-scan[Try L1-L20, pick best ratio]'
        '--level-range[Try a range of levels, pick best]:range:'
        '--no-trials[Skip multi-strategy trial at L20+]'
        '--fast-decode[Prefer faster-decoding entropy coders]'
        '--filter[Force preprocessing filter]:filter:(delta nibble none)'
        '--split[Split output into chunks]:size:'
        '--preserve-mtime[Set output mtime to match input]'
        '--min-ratio[Only write if ratio >= N]:ratio:'
        '--atomic[Write to temp file, rename on success]'
        '--keep-broken[Keep partial output on error]'
        '--exclude[Skip files matching glob with -r]:pattern:'
        '--decompress-check[Decompress in-memory after compress and verify CRC]'
        '--priority[Optimization goal]:priority:(speed ratio balanced)'
        '--adaptive-level[Auto-select level based on file entropy]'
        '--memory-limit[Cap memory usage]:size:'
        '-o[Output file]:output:_files'
        '--output[Output file]:output:_files'
        '-q[Quiet mode]'
        '--quiet[Quiet mode]'
        '-v[Verbose output]'
        '--verbose[Verbose output]'
        '-f[Force overwrite]'
        '--force[Force overwrite]'
        '-c[Write to stdout]'
        '--stdout[Write to stdout]'
        '--delete[Delete source after success]'
        '-r[Recurse into directories]'
        '--recursive[Recurse into directories]'
        '-t[Thread count]:threads:'
        '--threads[Thread count]:threads:'
        '-T[Thread count]:threads:'
        '*:input:_files'
    )

    local -a decompress_opts
    decompress_opts=(
        '-o[Output file]:output:_files'
        '--output[Output file]:output:_files'
        '-q[Quiet mode]'
        '--quiet[Quiet mode]'
        '-v[Verbose output]'
        '--verbose[Verbose output]'
        '-f[Force overwrite]'
        '--force[Force overwrite]'
        '-c[Write to stdout]'
        '--stdout[Write to stdout]'
        '--delete[Delete source after success]'
        '--verify[Re-compress roundtrip verify]'
        '--preserve-mtime[Set output mtime to match input]'
        '-r[Recurse into directories]'
        '--recursive[Recurse into directories]'
        '*:input:_files -g "*.mcx"'
    )

    local -a upgrade_opts
    upgrade_opts=(
        '-l[New compression level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '--level[New compression level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '-i[Overwrite input file]'
        '--in-place[Overwrite input file]'
        '-o[Output file]:output:_files'
        '--output[Output file]:output:_files'
        '-q[Quiet mode]'
        '-f[Force recompress]'
        '-t[Thread count]:threads:'
        '-T[Thread count]:threads:'
        '*:input:_files -g "*.mcx"'
    )

    local -a pipe_opts
    pipe_opts=(
        '-l[Compression level]:level:(1 3 6 9 12 20)'
        '--level[Compression level]:level:(1 3 6 9 12 20)'
        '-d[Decompress mode]'
        '--decompress[Decompress mode]'
    )

    local -a bench_opts
    bench_opts=(
        '-l[Specific level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '--level[Specific level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '--compare[Include gzip/bzip2/xz]'
        '--csv[CSV output format]'
        '--json[JSON output format]'
        '--warmup[Run warmup iteration before timing]'
        '--warmup-iterations[Number of warmup iterations]:count:'
        '--cold[Flush filesystem cache between iterations]'
        '--output[Write results to file (append mode)]:file:_files'
        '--compare-self[Compare against reference .mcx file]:ref:_files'
        '--delta[Compare against baseline file]:baseline:_files'
        '--save-baseline[Save current results as baseline]:file:_files'
        '--baseline-dir[Per-file baselines in directory]:dir:_directories'
        '--diff[Compare ratio and speed against baseline]:baseline:_files'
        '--profile[Show per-phase time breakdown]'
        '--chart[ASCII bar chart of compression ratios]'
        '--suite[Predefined benchmark suite]:suite:(quick standard full)'
        '--decode-only[Benchmark decompression only]'
        '--iterations[Number of iterations]:iterations:'
        '--size[Benchmark on truncated prefix]:size:'
        '--memory[Report peak RSS memory usage]'
        '--all-levels[Benchmark every level 1-26]'
        '--ratio-only[Measure ratio only, skip timing]'
        '--sort[Sort output]:mode:(ratio speed level)'
        '--top[Show top N results by ratio]:count:'
        '--median[Report median instead of mean]'
        '--percentile[Report p5/p50/p95 speed distribution]'
        '--histogram[Show compressed size distribution across block sizes]'
        '--brief[Compact one-line-per-level output]'
        '--worst[Show worst N results by ratio]:count:'
        '--filter[Force preprocessing filter]:filter:(delta nibble none)'
        '--format[Output format]:format:(table csv json markdown)'
        '--exclude[Skip files matching glob (directory input)]:pattern:'
        '--aggregate[Show aggregate totals for directory benchmarks]'
        '--no-header[Suppress column headers in output]'
        '--repeat[Run benchmark N times, show min/max/avg]:count:'
        '-t[Thread count]:threads:'
        '-T[Thread count]:threads:'
        '*:input:_files'
    )

    if (( CURRENT == 2 )); then
        _describe 'command' commands
    else
        case "${words[2]}" in
            compress)  _arguments $compress_opts ;;
            decompress|extract|x|d) _arguments $decompress_opts ;;
            upgrade|recompress) _arguments $upgrade_opts ;;
            pipe) _arguments $pipe_opts ;;
            bench|compare) _arguments $bench_opts ;;
            info) _arguments '--json[JSON output]' '--blocks[Show per-block details]' '--entropy[Show Shannon entropy of original data]' '*:input:_files' ;;
            stat) _arguments '--json[JSON output]' '--compare[Compare two files]' '*:input:_files' ;;
            ls|list) _arguments '--json[JSON output]' '*:input:_files -g "*.mcx"' ;;
            version) _arguments '--build[Show build details]' ;;
            cat) _files ;;
            verify) _files -g "*.mcx" ;;
            hash|checksum) _files ;;
            diff) _files -g "*.mcx" ;;
            *) ;;
        esac
    fi
}

_mcx "$@"
