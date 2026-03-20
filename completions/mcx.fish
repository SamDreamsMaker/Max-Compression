# Fish shell completions for mcx (MaxCompression CLI)

# Disable file completions by default (we add them where needed)
complete -c mcx -f

# Subcommands
complete -c mcx -n '__fish_use_subcommand' -a compress    -d 'Compress files'
complete -c mcx -n '__fish_use_subcommand' -a decompress  -d 'Decompress .mcx files'
complete -c mcx -n '__fish_use_subcommand' -a extract     -d 'Decompress (alias)'
complete -c mcx -n '__fish_use_subcommand' -a info        -d 'Show file information'
complete -c mcx -n '__fish_use_subcommand' -a cat         -d 'Decompress to stdout'
complete -c mcx -n '__fish_use_subcommand' -a stat        -d 'Show file statistics'
complete -c mcx -n '__fish_use_subcommand' -a hash        -d 'Show content hash'
complete -c mcx -n '__fish_use_subcommand' -a checksum    -d 'Show compressed file CRC32'
complete -c mcx -n '__fish_use_subcommand' -a bench       -d 'Benchmark all levels'
complete -c mcx -n '__fish_use_subcommand' -a compare     -d 'Benchmark (alias)'
complete -c mcx -n '__fish_use_subcommand' -a test        -d 'Run self-tests'
complete -c mcx -n '__fish_use_subcommand' -a verify      -d 'Verify file integrity'
complete -c mcx -n '__fish_use_subcommand' -a diff        -d 'Compare two compressed files'
complete -c mcx -n '__fish_use_subcommand' -a ls          -d 'List compressed files'
complete -c mcx -n '__fish_use_subcommand' -a list        -d 'List compressed files (alias)'
complete -c mcx -n '__fish_use_subcommand' -a upgrade     -d 'Re-compress at different level'
complete -c mcx -n '__fish_use_subcommand' -a recompress  -d 'Re-compress (alias)'
complete -c mcx -n '__fish_use_subcommand' -a pipe        -d 'Streaming compress/decompress'
complete -c mcx -n '__fish_use_subcommand' -a version     -d 'Show version info'

# Global flags
complete -c mcx -n '__fish_use_subcommand' -l help    -d 'Show help'
complete -c mcx -n '__fish_use_subcommand' -l version -d 'Show version'

# compress options
complete -c mcx -n '__fish_seen_subcommand_from compress' -s l -l level   -x -a '1 3 6 7 8 9 12 14 20 24 26' -d 'Compression level'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l fast      -d 'Fast compression (L3)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l default   -d 'Default compression (L6)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l best      -d 'Best compression (L20)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l strategy  -x -a 'lz bwt lzrc stride' -d 'Force strategy'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l block-size -x -d 'Override BWT block size'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l verify    -d 'Verify roundtrip after compress'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l dry-run   -d 'Predict strategy without compressing'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l estimate  -d 'Estimate compressed size'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s o -l output  -r -F -d 'Output file'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s q -l quiet   -d 'Quiet mode'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s v -l verbose -d 'Verbose output'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s f -l force   -d 'Force overwrite'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s c -l stdout  -d 'Write to stdout'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s k -l keep    -d 'Keep original (default)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l delete       -d 'Delete source after success'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s r -l recursive -d 'Recurse into directories'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s t -l threads -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s T -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from compress' -F -d 'Input file'

# decompress/extract/x/d options
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s o -l output  -r -F -d 'Output file'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s q -l quiet   -d 'Quiet mode'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s v -l verbose -d 'Verbose output'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s f -l force   -d 'Force overwrite'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s c -l stdout  -d 'Write to stdout'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s k -l keep    -d 'Keep original (default)'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -l delete       -d 'Delete source after success'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -l verify       -d 'Re-compress roundtrip verify'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s r -l recursive -d 'Recurse into directories'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s t -l threads -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s T -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -F -d 'Input .mcx file'

# upgrade/recompress options
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -s l -l level -x -a '1 3 6 7 8 9 12 14 20 24 26' -d 'New compression level'
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -s i -l in-place -d 'Overwrite input file'
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -s o -l output -r -F -d 'Output file'
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -s q -l quiet -d 'Quiet mode'
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -s f -l force -d 'Force recompress'
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -s t -l threads -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -s T -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from upgrade recompress' -F -d 'Input .mcx file'

# pipe options
complete -c mcx -n '__fish_seen_subcommand_from pipe' -s l -l level -x -a '1 3 6 9 12 20' -d 'Compression level'
complete -c mcx -n '__fish_seen_subcommand_from pipe' -s d -l decompress -d 'Decompress mode'

# bench/compare options
complete -c mcx -n '__fish_seen_subcommand_from bench compare' -s l -l level -x -a '1 3 6 7 8 9 12 14 20 24 26' -d 'Specific level'
complete -c mcx -n '__fish_seen_subcommand_from bench compare' -l compare -d 'Include gzip/bzip2/xz'
complete -c mcx -n '__fish_seen_subcommand_from bench compare' -l csv -d 'CSV output format'
complete -c mcx -n '__fish_seen_subcommand_from bench compare' -s t -l threads -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from bench compare' -s T -x -d 'Thread count'
complete -c mcx -n '__fish_seen_subcommand_from bench compare' -F -d 'Input file'

# info, stat — accept files + --json
complete -c mcx -n '__fish_seen_subcommand_from info stat' -l json -d 'JSON output'
complete -c mcx -n '__fish_seen_subcommand_from info stat' -F -d 'Input file'

# ls/list — accept files + --json
complete -c mcx -n '__fish_seen_subcommand_from ls list' -l json -d 'JSON output'
complete -c mcx -n '__fish_seen_subcommand_from ls list' -F -d '.mcx file'

# version -- --build
complete -c mcx -n '__fish_seen_subcommand_from version' -l build -d 'Show build details'

# cat, hash, checksum, verify, diff — accept files
complete -c mcx -n '__fish_seen_subcommand_from cat hash checksum' -F -d 'Input file'
complete -c mcx -n '__fish_seen_subcommand_from verify' -F -d '.mcx file (+ optional original)'
complete -c mcx -n '__fish_seen_subcommand_from diff' -F -d '.mcx file'
