#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
case "$(uname -s)" in
    MINGW*|MSYS*) platform=windows; temporary_root="${TEMP:-/tmp}" ;;
    *) platform=unix; temporary_root="${TMPDIR:-/tmp}" ;;
esac
output="${1:-${FILEKIT_BUILD_DIR:-$temporary_root/filekit-build}}"
if command -v cygpath >/dev/null 2>&1; then output="$(cygpath -u "$output")"; fi
mkdir -p "$output"
compiler="${CC:-cc}"
flags=(-std=c11 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wpedantic -Werror -Wstrict-prototypes -Wmissing-prototypes)
if [[ "${ARC_SANITIZE:-${FILEKIT_SANITIZE:-0}}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
sources=(support.c "file_system_$platform.c")
if [[ "$platform" == windows ]]; then sources+=(windows_text.c); fi
objects=()
for source in "${sources[@]}"; do
    object="$output/${source%.c}.o"
    "$compiler" "${flags[@]}" -c "$source" -o "$object"
    objects+=("$object")
done
rm -f "$output/libfilekit.a"
"${AR:-ar}" rcs "$output/libfilekit.a" "${objects[@]}"
printf 'Built %s/libfilekit.a\n' "$output"
