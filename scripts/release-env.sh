# Native release target and the existing dependency triplet.
target=${1:-${RELEASE_TARGET:-}}
if [[ -z "$target" ]]; then
    case "$(uname -m)" in x86_64|amd64) architecture=x86_64 ;; arm64|aarch64) architecture=arm64 ;; *) echo 'Unsupported host architecture' >&2; exit 1 ;; esac
    case "$(uname -s)" in Linux) target=linux-$architecture ;; Darwin) target=macos-$architecture ;; MINGW*|MSYS*) target=windows-$architecture ;; *) echo 'Unsupported host OS' >&2; exit 1 ;; esac
fi
case "$target" in darwin-*) target=macos-${target#darwin-} ;; esac
exe=
case "$target" in
    linux-x86_64) triplet=x64-linux-musl ;;
    linux-arm64) triplet=arm64-linux-musl ;;
    macos-x86_64) triplet=x64-osx ;;
    macos-arm64) triplet=arm64-osx ;;
    windows-x86_64) triplet=x64-mingw-static; exe=.exe ;;
    *) echo "Unsupported release target: $target" >&2; exit 1 ;;
esac
# Published asset names are kept compatible with both installers.
asset_target=$target
case "$target" in macos-*) asset_target=darwin-${target#macos-} ;; esac
work=${ARC_RELEASE_BUILD_DIR:-$PWD/build/release/$target}
if command -v cygpath >/dev/null 2>&1; then work=$(cygpath -m "$work"); fi
case "$work" in /*|[A-Za-z]:/*) ;; *) work="$PWD/$work" ;; esac
export ARC_RELEASE_BUILD_DIR=$work ARC_VCPKG_TRIPLET=$triplet
