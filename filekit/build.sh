#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
output=${1:-${FILEKIT_BUILD_DIR:-$PWD/build}}
if command -v cygpath >/dev/null 2>&1; then output=$(cygpath -u "$output"); fi
sanitize=OFF
[[ "${ARC_SANITIZE:-${FILEKIT_SANITIZE:-0}}" != 1 ]] || sanitize=ON
options=()
if [[ -n "${AR:-}" ]]; then options+=("-DCMAKE_AR=$(command -v "$AR")"); fi
cache=$output/CMakeCache.txt
if [[ ! -f "$cache" ]]; then
    cmake -S . -B "$output" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFILEKIT_SANITIZE="$sanitize" ${options[@]+"${options[@]}"}
else
    cached_sanitize=$(sed -n 's/^FILEKIT_SANITIZE:BOOL=//p' "$cache")
    [[ "$cached_sanitize" == "$sanitize" ]] || { echo 'Use a separate FILEKIT_BUILD_DIR when changing sanitizer mode.' >&2; exit 1; }
    if [[ -n "${CC:-}" ]]; then
        compiler=$(command -v "$CC") || { echo "Missing C compiler: $CC" >&2; exit 1; }
        if command -v cygpath >/dev/null 2>&1; then compiler=$(cygpath -m "$compiler"); fi
        cached_compiler=$(sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p; s/^CMAKE_C_COMPILER:STRING=//p' "$cache")
        [[ "${compiler%.exe}" == "${cached_compiler%.exe}" ]] || { echo 'Use a separate FILEKIT_BUILD_DIR when changing compilers.' >&2; exit 1; }
    fi
fi
cmake --build "$output"
