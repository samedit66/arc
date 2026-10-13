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
# Bash 3.2 treats empty arrays as unset under nounset.
flags+=(${sanitizer_flags[@]+"${sanitizer_flags[@]}"})
release_version=$(cat VERSION)
if [[ ! "$release_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'VERSION must contain one MAJOR.MINOR.PATCH version\n' >&2
    exit 1
fi
version="${ARC_VERSION:-$release_version}"
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
    "$compiler" "${flags[@]}" ${dependency_cflags[@]+"${dependency_cflags[@]}"} -c "$source" -o "$object"
    objects+=("$object")
done
filekit_objects=("$build_dir/filekit/support.o" "$build_dir/filekit/file_system_$platform.o")
if [[ "$platform" == windows ]]; then filekit_objects+=("$build_dir/filekit/windows_text.o"); fi
rm -f "$build_dir/libarc.a"
"$AR" rcs "$build_dir/libarc.a" "${objects[@]}" "${filekit_objects[@]}"
"$compiler" "${flags[@]}" ${link_flags[@]+"${link_flags[@]}"} main.c "$build_dir/libarc.a" \
    ${dependency_ldflags[@]+"${dependency_ldflags[@]}"} -o "$build_dir/arc$exe"
printf 'Built %s\n' "$build_dir/arc$exe"
