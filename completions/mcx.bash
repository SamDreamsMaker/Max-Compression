# Bash completion for mcx (MaxCompression CLI)
_mcx() {
    local cur prev commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    commands="compress decompress extract info cat ls list diff stat hash checksum bench compare test verify version upgrade recompress pipe"

    case "${prev}" in
        mcx)
            COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") )
            return 0
            ;;
        compress)
            COMPREPLY=( $(compgen -W "-l -o -q -v -f -r -c -t --level -T --output --quiet --verbose --force --recursive --stdout --fast --default --best --delete --verify --threads --strategy --block-size --dry-run --estimate" -- "${cur}") $(compgen -f -- "${cur}") )
            return 0
            ;;
        decompress|extract|x|d)
            COMPREPLY=( $(compgen -W "-o -q -v -f -r -c --output --quiet --verbose --force --recursive --stdout --delete --keep --verify" -- "${cur}") $(compgen -f -X '!*.mcx' -- "${cur}") )
            return 0
            ;;
        upgrade|recompress)
            COMPREPLY=( $(compgen -W "-l -o -q -f -i --level --output --quiet --force --in-place --threads -t -T" -- "${cur}") $(compgen -f -X '!*.mcx' -- "${cur}") )
            return 0
            ;;
        pipe)
            COMPREPLY=( $(compgen -W "-l -d --level --decompress" -- "${cur}") )
            return 0
            ;;
        bench|compare)
            COMPREPLY=( $(compgen -W "-l --level --compare --csv -t -T --threads" -- "${cur}") $(compgen -f -- "${cur}") )
            return 0
            ;;
        diff)
            COMPREPLY=( $(compgen -f -X '!*.mcx' -- "${cur}") )
            return 0
            ;;
        -l|--level)
            COMPREPLY=( $(compgen -W "1 3 6 7 8 9 12 14 20 24 26" -- "${cur}") )
            return 0
            ;;
        --strategy)
            COMPREPLY=( $(compgen -W "lz bwt lzrc stride" -- "${cur}") )
            return 0
            ;;
        -o|--output)
            COMPREPLY=( $(compgen -f -- "${cur}") )
            return 0
            ;;
        -t|-T|--threads)
            COMPREPLY=( $(compgen -W "1 2 4 8" -- "${cur}") )
            return 0
            ;;
        info|stat|ls|list)
            COMPREPLY=( $(compgen -W "--json" -- "${cur}") $(compgen -f -- "${cur}") )
            return 0
            ;;
        cat|hash|checksum|verify)
            COMPREPLY=( $(compgen -f -- "${cur}") )
            return 0
            ;;
        version)
            COMPREPLY=( $(compgen -W "--build" -- "${cur}") )
            return 0
            ;;
    esac

    COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") $(compgen -f -- "${cur}") )
}
complete -F _mcx mcx
