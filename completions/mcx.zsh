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
        'bench:Benchmark all levels'
        'test:Run self-tests'
        'verify:Verify file integrity'
        'diff:Compare two compressed files'
        'ls:List compressed files'
        'version:Show version info'
    )

    local -a compress_opts
    compress_opts=(
        '-l[Compression level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '--level[Compression level]:level:(1 3 6 7 8 9 12 14 20 24 26)'
        '--fast[Fast compression (L3)]'
        '--default[Default compression (L6)]'
        '--best[Best compression (L20)]'
        '-o[Output file]:output:_files'
        '--output[Output file]:output:_files'
        '-q[Quiet mode]'
        '--quiet[Quiet mode]'
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
        '-f[Force overwrite]'
        '--force[Force overwrite]'
        '-c[Write to stdout]'
        '--stdout[Write to stdout]'
        '--delete[Delete source after success]'
        '-r[Recurse into directories]'
        '--recursive[Recurse into directories]'
        '*:input:_files -g "*.mcx"'
    )

    if (( CURRENT == 2 )); then
        _describe 'command' commands
    else
        case "${words[2]}" in
            compress)  _arguments $compress_opts ;;
            decompress|extract|x|d) _arguments $decompress_opts ;;
            info|cat|bench|stat) _files ;;
            verify) _files -g "*.mcx" ;;
            hash|ls) _files -g "*.mcx" ;;
            diff) _files -g "*.mcx" ;;
            *) ;;
        esac
    fi
}

_mcx "$@"
