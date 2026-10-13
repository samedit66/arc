#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Direct invocation uses the same policy as mise tasks.
if [[ "${ARC_BUILD_CONFIG:-}" != 1 ]]; then
    exec mise exec -- bash scripts/build.sh "$@"
fi
source scripts/build-env.sh
bash filekit/build.sh "$build_dir/filekit"
mkdir -p "$build_dir"
compiler="$CC"
read -r -a dependency_cflags <<< "$(pkg-config --cflags libarchive)"
read -r -a dependency_ldflags <<< "$(pkg-config --libs libarchive)"
read -r -a flags <<< "$ARC_CFLAGS"
read -r -a sanitizer_flags <<< "$ARC_SANITIZER_FLAGS"
read -r -a link_flags <<< "$ARC_LDFLAGS"
flags+=("${sanitizer_flags[@]}")
version="${ARC_VERSION:-dev}"
if [[ ! "$version" =~ ^[A-Za-z0-9][A-Za-z0-9.+_-]*$ ]]; then
    printf 'Invalid ARC_VERSION: use letters, digits, dots, plus, underscores or hyphens\n' >&2
    exit 1
fi
flags+=("-DARC_VERSION=\"$version\"")
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
"$compiler" "${flags[@]}" "${link_flags[@]}" main.c "${objects[@]}" "$build_dir/filekit/libfilekit.a" \
    "${dependency_ldflags[@]}" -o "$build_dir/arc$exe"
printf 'Built %s\n' "$build_dir/arc$exe"
