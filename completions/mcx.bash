# Bash completion for mcx (MaxCompression CLI)
_mcx() {
    local cur prev commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    commands="compress decompress extract info cat ls diff stat hash bench test verify version"

    case "${prev}" in
        mcx)
            COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") )
            return 0
            ;;
        compress)
            COMPREPLY=( $(compgen -W "-l -o -q -v -f -r -c -t --level --output --quiet --verbose --force --recursive --stdout --fast --default --best --delete --verify --threads" -- "${cur}") $(compgen -f -- "${cur}") )
            return 0
            ;;
        decompress|extract|x|d)
            COMPREPLY=( $(compgen -W "-o -q -v -f -r -c --output --quiet --verbose --force --recursive --stdout --delete --keep" -- "${cur}") $(compgen -f -X '!*.mcx' -- "${cur}") )
            return 0
            ;;
        bench)
            COMPREPLY=( $(compgen -W "-l --level" -- "${cur}") $(compgen -f -- "${cur}") )
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
        -o|--output)
            COMPREPLY=( $(compgen -f -- "${cur}") )
            return 0
            ;;
        -t|--threads)
            COMPREPLY=( $(compgen -W "1 2 4 8" -- "${cur}") )
            return 0
            ;;
        info|cat|ls|stat|hash|verify)
            COMPREPLY=( $(compgen -f -- "${cur}") )
            return 0
            ;;
    esac

    COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") $(compgen -f -- "${cur}") )
}
complete -F _mcx mcx
