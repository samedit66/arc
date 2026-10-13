#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-env.sh
bash filekit/build.sh "$build_dir/filekit"
mkdir -p "$build_dir"
if [[ "$(uname -s)" == Darwin ]]; then
    export PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    compiler="${CC:-clang}"
else
    compiler="${CC:-gcc}"
fi
read -r -a dependency_cflags <<< "$(pkg-config --cflags libarchive)"
read -r -a dependency_ldflags <<< "$(pkg-config --libs libarchive)"
flags=(-std=c11 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wpedantic -Werror)
version="${ARC_VERSION:-dev}"
if [[ ! "$version" =~ ^[A-Za-z0-9][A-Za-z0-9.+_-]*$ ]]; then
    printf 'Invalid ARC_VERSION: use letters, digits, dots, plus, underscores or hyphens\n' >&2
    exit 1
fi
flags+=("-DARC_VERSION=\"$version\"")
if [[ "${ARC_SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
sources=(arc.c internal/writer.c internal/reader.c internal/unpack.c
         internal/libarchive_adapter.c internal/archive_paths.c internal/support.c
         "internal/platform_text_$platform.c")
objects=()
for source in "${sources[@]}"; do
    object="$build_dir/${source%.c}.o"
    mkdir -p "$(dirname "$object")"
    "$compiler" "${flags[@]}" "${dependency_cflags[@]}" -c "$source" -o "$object"
    objects+=("$object")
done
platform_flags=()
if [[ "$platform" == windows ]]; then platform_flags+=(-municode); fi
"$compiler" "${flags[@]}" main.c "${objects[@]}" "$build_dir/filekit/libfilekit.a" \
    "${platform_flags[@]}" "${dependency_ldflags[@]}" -o "$build_dir/arc$exe"
printf 'Built %s\n' "$build_dir/arc$exe"
