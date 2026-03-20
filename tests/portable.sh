# Portable stat wrappers for Linux + macOS
get_file_size() {
    if stat -c%s "$1" 2>/dev/null; then return; fi
    stat -f%z "$1" 2>/dev/null
}
get_mtime() {
    if stat -c%Y "$1" 2>/dev/null; then return; fi
    stat -f%m "$1" 2>/dev/null
}
