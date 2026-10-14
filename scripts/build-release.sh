#!/usr/bin/env bash
set -euo pipefail
repo=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$repo"
[[ $# == 1 ]] || { echo 'Usage: bash scripts/build-release.sh TARGET' >&2; exit 1; }
target=$1
case "$target" in
    linux-x86_64) triplet=x64-linux-musl; exe=; link_flags=-static ;;
    linux-arm64) triplet=arm64-linux-musl; exe=; link_flags=-static ;;
    darwin-x86_64) triplet=x64-osx; exe=; link_flags= ;;
    darwin-arm64) triplet=arm64-osx; exe=; link_flags= ;;
    windows-x86_64) triplet=x64-mingw-static; exe=.exe; link_flags='-static -static-libgcc' ;;
    *) echo "Unsupported release target: $target" >&2; exit 1 ;;
esac
work=${ARC_RELEASE_BUILD_DIR:?Set ARC_RELEASE_BUILD_DIR to an absolute temporary directory}
case "$work" in /*|[A-Za-z]:/*) ;; *) echo 'ARC_RELEASE_BUILD_DIR must be absolute' >&2; exit 1 ;; esac
mkdir -p "$work"
pin=$(sed -n 's/.*"builtin-baseline": "\([0-9a-f]*\)".*/\1/p' release/vcpkg.json)
vcpkg_root=$work/vcpkg
if [[ ! -d "$vcpkg_root/.git" ]]; then
    git init "$vcpkg_root"
    git -C "$vcpkg_root" remote add origin https://github.com/microsoft/vcpkg.git
fi
git -C "$vcpkg_root" fetch --depth 1 origin "$pin"
git -C "$vcpkg_root" checkout --detach "$pin"
if [[ "$target" == windows-* ]]; then
    export ARC_VCPKG_BOOTSTRAP
    ARC_VCPKG_BOOTSTRAP=$(cygpath -w "$vcpkg_root/bootstrap-vcpkg.bat")
    powershell.exe -NoProfile -Command '& $env:ARC_VCPKG_BOOTSTRAP -disableMetrics; exit $LASTEXITCODE'
    export VCPKG_DEFAULT_HOST_TRIPLET=x64-mingw-static
else
    bash "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
fi
export VCPKG_DISABLE_METRICS=1
"$vcpkg_root/vcpkg" install --triplet "$triplet" --overlay-triplets "$repo/release/triplets" \
    --x-manifest-root "$repo/release" --x-install-root "$work/deps"
toolchain_flags=()
if [[ "$target" == linux-* ]]; then
    toolchain_flags+=("-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$repo/release/triplets/musl-toolchain.cmake")
fi
cmake -S . -B "$work/arc" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$vcpkg_root/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="$triplet" -DVCPKG_INSTALLED_DIR="$work/deps" \
    -DVCPKG_MANIFEST_DIR="$repo/release" -DVCPKG_MANIFEST_INSTALL=OFF \
    -DVCPKG_OVERLAY_TRIPLETS="$repo/release/triplets" \
    -DCMAKE_BUILD_TYPE=Release -DARC_BUILD_CLI=ON -DARC_DEVELOPER_WARNINGS=ON -DARC_BUILD_API_TESTS=ON \
    -DCMAKE_EXE_LINKER_FLAGS="$link_flags" ${toolchain_flags[@]+"${toolchain_flags[@]}"}
cmake --build "$work/arc" --parallel 4
binary=$work/arc/arc$exe
[[ "$("$binary" --version)" == "arc $(cat VERSION)" ]]
bash scripts/check-release-binary.sh "$target" "$binary"
