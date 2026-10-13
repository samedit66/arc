#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-env.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/arc-api.XXXXXX")
trap 'rm -rf "$work"' EXIT
read -r -a flags <<< "$ARC_CFLAGS $ARC_SANITIZER_FLAGS"
read -r -a cflags <<< "$(pkg-config --cflags libarchive)"
read -r -a libs <<< "$(pkg-config --libs libarchive)"
"$CC" "${flags[@]}" ${cflags[@]+"${cflags[@]}"} -I. tests/api.c "$build_dir/libarc.a" "${libs[@]}" -o "$work/api$exe"
(cd "$work"; ./api)

"$CC" "${flags[@]}" ${cflags[@]+"${cflags[@]}"} -I. tests/api_formats.c "$build_dir/libarc.a" "${libs[@]}" -o "$work/formats$exe"
(cd "$work"; ./formats)
if [[ "$platform" == windows ]]; then
    "$CC" "${flags[@]}" ${cflags[@]+"${cflags[@]}"} -I. tests/api_windows_cleanup.c "$build_dir/libarc.a" "${libs[@]}" -o "$work/windows-cleanup$exe"
    (cd "$work"; ./windows-cleanup)
fi
