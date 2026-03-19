# Bash completion for mcx
_mcx() {
    local cur prev commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    
    commands="compress decompress info cat bench test version"
    
    case "${prev}" in
        mcx)
            COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") )
            return 0
            ;;
        compress)
            COMPREPLY=( $(compgen -W "-l -o -q -f --fast --default --best --delete --quiet --force --level --output" -- "${cur}") $(compgen -f -- "${cur}") )
            return 0
            ;;
        decompress)
            COMPREPLY=( $(compgen -W "-o -q -f --delete --quiet --force --output" -- "${cur}") $(compgen -f -X '!*.mcx' -- "${cur}") )
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
        info|cat|bench)
            COMPREPLY=( $(compgen -f -- "${cur}") )
            return 0
            ;;
    esac
    
    COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") $(compgen -f -- "${cur}") )
}
complete -F _mcx mcx
