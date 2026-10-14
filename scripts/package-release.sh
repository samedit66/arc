#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 1 ]] || { echo 'Usage: bash scripts/package-release.sh TARGET' >&2; exit 1; }
target=$1
case "$target" in
    linux-x86_64) triplet=x64-linux-musl; exe= ;;
    linux-arm64) triplet=arm64-linux-musl; exe= ;;
    darwin-x86_64) triplet=x64-osx; exe= ;;
    darwin-arm64) triplet=arm64-osx; exe= ;;
    windows-x86_64) triplet=x64-mingw-static; exe=.exe ;;
    *) echo 'Unknown release target' >&2; exit 1 ;;
esac
work=${ARC_RELEASE_BUILD_DIR:?Set ARC_RELEASE_BUILD_DIR}
binary=$work/arc/arc$exe
bash scripts/check-release-binary.sh "$target" "$binary"
mkdir -p "$work/package"
cp "$binary" "$work/package/arc$exe"
cp LICENSE "$work/package/LICENSE"
cp VERSION "$work/package/VERSION"
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
name="arc-v$(cat VERSION)-$target"
if [[ "$target" == windows-* ]]; then
    rm -f "$work/assets/$name.zip"
    (cd "$work/package"; zip -9 "$work/assets/$name.zip" arc.exe LICENSE VERSION THIRD_PARTY_NOTICES.txt)
else
    tar -czf "$work/assets/$name.tar.gz" -C "$work/package" arc LICENSE VERSION THIRD_PARTY_NOTICES.txt
fi
