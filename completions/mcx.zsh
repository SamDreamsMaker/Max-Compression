#compdef mcx

_mcx() {
    local -a commands
    commands=(
        'compress:Compress files'
        'decompress:Decompress .mcx files'
        'info:Show file information'
        'cat:Decompress to stdout'
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
        '-t[Thread count]:threads:'
        '--threads[Thread count]:threads:'
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
        '*:input:_files -g "*.mcx"'
    )

    if (( CURRENT == 2 )); then
        _describe 'command' commands
    else
        case "${words[2]}" in
            compress)  _arguments $compress_opts ;;
            decompress) _arguments $decompress_opts ;;
            info|cat|bench|verify|ls) _files ;;
            diff) _files -g "*.mcx" ;;
            *) ;;
        esac
    fi
}

_mcx "$@"
