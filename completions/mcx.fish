# Fish shell completions for mcx (MaxCompression CLI)

# Disable file completions by default (we add them where needed)
complete -c mcx -f

# Subcommands
complete -c mcx -n '__fish_use_subcommand' -a compress    -d 'Compress files'
complete -c mcx -n '__fish_use_subcommand' -a decompress  -d 'Decompress .mcx files'
complete -c mcx -n '__fish_use_subcommand' -a extract     -d 'Decompress (alias)'
complete -c mcx -n '__fish_use_subcommand' -a info        -d 'Show file information'
complete -c mcx -n '__fish_use_subcommand' -a cat         -d 'Decompress to stdout'
complete -c mcx -n '__fish_use_subcommand' -a bench       -d 'Benchmark all levels'
complete -c mcx -n '__fish_use_subcommand' -a test        -d 'Run self-tests'
complete -c mcx -n '__fish_use_subcommand' -a verify      -d 'Verify file integrity'
complete -c mcx -n '__fish_use_subcommand' -a diff        -d 'Compare two compressed files'
complete -c mcx -n '__fish_use_subcommand' -a ls          -d 'List compressed files'
complete -c mcx -n '__fish_use_subcommand' -a version     -d 'Show version info'

# Global flags
complete -c mcx -n '__fish_use_subcommand' -l help    -d 'Show help'
complete -c mcx -n '__fish_use_subcommand' -l version -d 'Show version'

# compress options
complete -c mcx -n '__fish_seen_subcommand_from compress' -s l -l level   -x -a '1 3 6 7 8 9 12 14 20 24 26' -d 'Compression level'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l fast      -d 'Fast compression (L3)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l default   -d 'Default compression (L6)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l best      -d 'Best compression (L20)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s o -l output  -r -F -d 'Output file'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s q -l quiet   -d 'Quiet mode'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s f -l force   -d 'Force overwrite'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s c -l stdout  -d 'Write to stdout'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s k -l keep    -d 'Keep original (default)'
complete -c mcx -n '__fish_seen_subcommand_from compress' -l delete       -d 'Delete source after success'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s r -l recursive -d 'Recurse into directories'
complete -c mcx -n '__fish_seen_subcommand_from compress' -s t -l threads -x -d 'Thread count'
complete -c mcx -n "__fish_seen_subcommand_from compress" -s T -x -d "Thread count"
complete -c mcx -n '__fish_seen_subcommand_from compress' -F -d 'Input file'

# decompress/extract/x/d options
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s o -l output  -r -F -d 'Output file'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s q -l quiet   -d 'Quiet mode'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s f -l force   -d 'Force overwrite'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s c -l stdout  -d 'Write to stdout'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s k -l keep    -d 'Keep original (default)'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -l delete       -d 'Delete source after success'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -s r -l recursive -d 'Recurse into directories'
complete -c mcx -n '__fish_seen_subcommand_from decompress extract x d' -F -d 'Input .mcx file'

# info, cat, bench — accept files
complete -c mcx -n '__fish_seen_subcommand_from info cat bench' -F -d 'Input file'

# verify — accept files
complete -c mcx -n '__fish_seen_subcommand_from verify' -F -d '.mcx file (+ optional original)'

# diff — accept .mcx files
complete -c mcx -n '__fish_seen_subcommand_from diff' -F -d '.mcx file'

# ls — accept .mcx files
complete -c mcx -n '__fish_seen_subcommand_from ls' -F -d '.mcx file'
complete -c mcx -n "__fish_seen_subcommand_from decompress extract" -s t -l threads -x -d "Thread count"
complete -c mcx -n "__fish_seen_subcommand_from decompress extract" -s T -x -d "Thread count"
complete -c mcx -n "__fish_seen_subcommand_from bench" -s t -l threads -x -d "Thread count"
complete -c mcx -n "__fish_seen_subcommand_from bench" -s T -x -d "Thread count"
complete -c mcx -n '__fish_use_subcommand' -a compare    -d 'Alias for bench'
