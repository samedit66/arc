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
