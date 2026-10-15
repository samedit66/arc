# Sourced by configure/release helpers. CMake caches contain absolute paths.
cmake_cache_moved() {
    local source_path build_path cached_source cached_build
    source_path=$(cd "$1" && pwd -P)
    build_path=$(cd "$2" && pwd -P)
    cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$2/CMakeCache.txt")
    cached_build=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$2/CMakeCache.txt")
    # Existing aliases such as /tmp and /private/tmp are the same location.
    if [[ -d "$cached_source" ]]; then cached_source=$(cd "$cached_source" && pwd -P); fi
    if [[ -d "$cached_build" ]]; then cached_build=$(cd "$cached_build" && pwd -P); fi
    if command -v cygpath >/dev/null 2>&1; then
        source_path=$(cygpath -m "$source_path")
        build_path=$(cygpath -m "$build_path")
        if [[ -n "$cached_source" ]]; then cached_source=$(cygpath -m "$cached_source"); fi
        if [[ -n "$cached_build" ]]; then cached_build=$(cygpath -m "$cached_build"); fi
    fi
    [[ -n "$cached_source" && "$cached_source" != "$source_path" ]] ||
        [[ -n "$cached_build" && "$cached_build" != "$build_path" ]]
}
