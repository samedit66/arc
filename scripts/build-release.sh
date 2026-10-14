#!/usr/bin/env bash
set -euo pipefail
repo=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$repo"
[[ $# -le 1 ]] || { echo 'Usage: bash scripts/build-release.sh [TARGET]' >&2; exit 1; }
source scripts/release-env.sh
mkdir -p "$work"
pin=$(sed -n 's/.*"builtin-baseline": "\([0-9a-f]*\)".*/\1/p' release/vcpkg.json)
vcpkg_root=$work/vcpkg
if [[ ! -d "$vcpkg_root/.git" ]]; then
    git init "$vcpkg_root"
    git -C "$vcpkg_root" remote add origin https://github.com/microsoft/vcpkg.git
fi
if [[ "$(git -C "$vcpkg_root" rev-parse HEAD 2>/dev/null || true)" != "$pin" ]]; then
    git -C "$vcpkg_root" fetch --depth 1 origin "$pin"
    git -C "$vcpkg_root" checkout --detach "$pin"
fi
if [[ "$target" == windows-* ]]; then
    export ARC_VCPKG_BOOTSTRAP
    ARC_VCPKG_BOOTSTRAP=$(cygpath -w "$vcpkg_root/bootstrap-vcpkg.bat")
    [[ -f "$vcpkg_root/vcpkg.exe" ]] || powershell.exe -NoProfile -Command '& $env:ARC_VCPKG_BOOTSTRAP -disableMetrics; exit $LASTEXITCODE'
    export VCPKG_DEFAULT_HOST_TRIPLET=x64-mingw-static
else
    [[ -f "$vcpkg_root/vcpkg" ]] || bash "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
fi
export VCPKG_DISABLE_METRICS=1
if [[ "$target" == linux-* ]]; then
    # Autotools and CMake dependencies must both use musl.
    export CC=musl-gcc
    export ARC_MUSL_KERNEL_HEADERS="$work/kernel-headers"
    if [[ ! -d "$ARC_MUSL_KERNEL_HEADERS/linux" || ! -d "$ARC_MUSL_KERNEL_HEADERS/asm" || ! -d "$ARC_MUSL_KERNEL_HEADERS/asm-generic" ]]; then
        mkdir -p "$ARC_MUSL_KERNEL_HEADERS"
        # Expose kernel UAPI headers without exposing glibc's libc headers.
        kernel_triplet=$(gcc -dumpmachine)
        cp -R /usr/include/linux /usr/include/asm-generic \
            "/usr/include/$kernel_triplet/asm" "$ARC_MUSL_KERNEL_HEADERS/"
    fi
fi
"$vcpkg_root/vcpkg" install --triplet "$triplet" --overlay-triplets "$repo/release/triplets" \
    --x-manifest-root "$repo/release" --x-install-root "$work/deps"
toolchain_flags=()
if [[ "$target" == linux-* ]]; then
    toolchain_flags+=("-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$repo/release/triplets/musl-toolchain.cmake")
fi
cache=$work/arc/CMakeCache.txt
if [[ ! -f "$cache" ]]; then
    cmake --preset release-vcpkg ${toolchain_flags[@]+"${toolchain_flags[@]}"}
else
    cached_triplet=$(sed -n 's/^VCPKG_TARGET_TRIPLET:STRING=//p; s/^VCPKG_TARGET_TRIPLET:UNINITIALIZED=//p' "$cache")
    [[ "$cached_triplet" == "$triplet" ]] || { echo 'Release target changed; use a separate ARC_RELEASE_BUILD_DIR.' >&2; exit 1; }
    if [[ -n "${CC:-}" ]]; then
        compiler=$(command -v "$CC") || { echo "Missing C compiler: $CC" >&2; exit 1; }
        if command -v cygpath >/dev/null 2>&1; then compiler=$(cygpath -m "$compiler"); fi
        cached_compiler=$(sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p; s/^CMAKE_C_COMPILER:STRING=//p' "$cache")
        [[ "${compiler%.exe}" == "${cached_compiler%.exe}" ]] || { echo 'Release compiler changed; use a separate ARC_RELEASE_BUILD_DIR.' >&2; exit 1; }
    fi
    if [[ CMakePresets.json -nt "$cache" ]]; then
        cmake --preset release-vcpkg ${toolchain_flags[@]+"${toolchain_flags[@]}"}
    fi
fi
cmake --build --preset release-vcpkg
binary=$work/arc/arc$exe
[[ "$("$binary" --version)" == "arc $(cat VERSION)" ]]
bash scripts/check-release-binary.sh "$target" "$binary"
