#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-env.sh
source scripts/cmake-cache.sh
command -v cmake >/dev/null || { echo 'Missing CMake; run mise install.' >&2; exit 1; }
command -v ninja >/dev/null || { echo 'Missing Ninja; run mise install.' >&2; exit 1; }
cache=$build_dir/CMakeCache.txt
version=${ARC_VERSION:-$(cat VERSION)}
fresh_flags=()
# CMake regenerates changed source/configuration files during the build.
# Reconfigure here only for first use or a changed CLI version override.
if [[ -f "$cache" ]]; then
    if cmake_cache_moved "$PWD" "$build_dir"; then
        echo 'Build directory moved; regenerating the CMake cache.'
        fresh_flags=(--fresh)
    fi
    if [[ -n "${CC:-}" ]]; then
        selected=$(command -v "$CC") || { echo "Missing C compiler: $CC" >&2; exit 1; }
        if command -v cygpath >/dev/null 2>&1; then selected=$(cygpath -m "$selected"); fi
        cached=$(sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p; s/^CMAKE_C_COMPILER:STRING=//p' "$cache")
        [[ "${selected%.exe}" == "${cached%.exe}" ]] || {
            echo 'CC differs from the cached compiler. Select a new ARC_BUILD_DIR or configure with cmake --fresh.' >&2
            exit 1
        }
    fi
    sanitize=OFF
    [[ "$preset" != sanitize ]] || sanitize=ON
    cached_sanitize=$(sed -n 's/^ARC_SANITIZE:BOOL=//p' "$cache")
    [[ "${cached_sanitize:-OFF}" == "$sanitize" ]] || {
        echo 'Sanitizer mode changed. Select a separate ARC_BUILD_DIR or configure with cmake --fresh.' >&2; exit 1;
    }
    cached_version=$(sed -n 's/^ARC_VERSION:UNINITIALIZED=//p; s/^ARC_VERSION:STRING=//p' "$cache")
    if [[ ${#fresh_flags[@]} == 0 && "$version" == "$cached_version" && ! CMakePresets.json -nt "$cache" ]]; then exit 0; fi
fi
if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null 2>&1; then
    export CMAKE_PREFIX_PATH="$(brew --prefix libarchive)${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi
cmake ${fresh_flags[@]+"${fresh_flags[@]}"} --preset "$preset" -B "$build_dir" -DARC_VERSION="$version"
