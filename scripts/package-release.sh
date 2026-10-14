#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# -le 1 ]] || { echo 'Usage: bash scripts/package-release.sh [TARGET]' >&2; exit 1; }
source scripts/release-env.sh
binary=$work/arc/arc$exe
bash scripts/check-release-binary.sh "$target" "$binary"
mkdir -p "$work/package"
cp "$binary" "$work/package/arc$exe"
cp LICENSE "$work/package/LICENSE"
cp VERSION "$work/package/VERSION"
cp README.md "$work/package/README.md"
printf 'version=%s\ntarget=%s\ntriplet=%s\n' "$(cat VERSION)" "$target" "$triplet" > "$work/package/BUILD_INFO"
[[ -f "$work/deps/$triplet/share/libarchive/copyright" ]] || { echo 'Missing vcpkg dependency licenses; rebuild release.' >&2; exit 1; }
{
    printf 'Arc and bundled dependency licenses\n\n'
    cat LICENSE
    for copyright in "$work/deps/$triplet"/share/*/copyright; do
        [[ -f "$copyright" ]] || continue
        printf '\n\n===== %s =====\n' "$(basename "$(dirname "$copyright")")"
        cat "$copyright"
    done
    for copyright in release/licenses/*; do
        printf '\n\n===== %s =====\n' "$(basename "$copyright")"
        cat "$copyright"
    done
} > "$work/package/THIRD_PARTY_NOTICES.txt"
mkdir -p "$work/assets"
name="arc-v$(cat VERSION)-$asset_target"
if [[ "$target" == windows-* ]]; then
    rm -f "$work/assets/$name.zip"
    (cd "$work/package"; zip -9 "$work/assets/$name.zip" arc.exe LICENSE VERSION README.md BUILD_INFO THIRD_PARTY_NOTICES.txt)
else
    tar -czf "$work/assets/$name.tar.gz" -C "$work/package" arc LICENSE VERSION README.md BUILD_INFO THIRD_PARTY_NOTICES.txt
fi
