#!/bin/sh
# Keep execution inside main: a truncated curl response must not start installing.
main() {
    set -eu
    umask 077
    install_dir=${HOME:?HOME must be set}/.local/bin
    ref=latest
    from_source=0
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --install-dir)
                [ "$#" -ge 2 ] && [ -n "$2" ] || fail '--install-dir requires a directory'
                install_dir=$2
                shift 2 ;;
            --version)
                [ "$#" -ge 2 ] && [ -n "$2" ] || fail '--version requires latest, main or vMAJOR.MINOR.PATCH'
                ref=$2
                shift 2 ;;
            --from-source) from_source=1; shift ;;
            --help)
                printf 'Usage: sh install.sh [--install-dir DIRECTORY] [--version latest|main|vMAJOR.MINOR.PATCH] [--from-source]\nInstalls a verified release binary. main always builds from source.\n'
                return ;;
            *) fail "Unknown argument: $1" ;;
        esac
    done
    case $(uname -s) in
        Darwin) exe=; macos=1 ;;
        Linux) exe=; macos=0 ;;
        MINGW*|MSYS*)
            [ "${MSYSTEM:-}" = UCRT64 ] || fail 'Run inside MSYS2 UCRT64'
            exe=.exe; macos=0
            install_dir=$(cygpath -u "$install_dir") ;;
        *) fail 'Supported systems: macOS, Linux and MSYS2 UCRT64' ;;
    esac
    case "$install_dir" in /*) ;; *) fail '--install-dir must be an absolute path' ;; esac
    command -v mktemp >/dev/null 2>&1 || fail 'Missing dependency: mktemp'
    case "$ref" in
        latest)
            command -v curl >/dev/null 2>&1 || fail 'Missing dependency: curl'
            release_url=$(curl -fsSL -o /dev/null -w '%{url_effective}' https://github.com/samedit66/arc/releases/latest) || fail 'No stable release available; use --version main explicitly'
            ref=${release_url##*/} ;;
    esac
    if [ "$ref" != main ]; then
        printf '%s\n' "$ref" | LC_ALL=C grep -Eq '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' || fail 'Expected main or a release tag vMAJOR.MINOR.PATCH'
    else
        from_source=1
    fi
    destination=$install_dir/arc$exe
    check_destination
    work=
    staged=
    staged_notices=
    trap cleanup 0
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
    work=$(mktemp -d "${TMPDIR:-/tmp}/arc-install.XXXXXX")
    if [ "$from_source" = 1 ]; then
        build_source
    else
        download_binary
    fi
    [ -f "$binary" ] && [ ! -L "$binary" ] || fail 'Installation did not produce a regular arc binary'
    [ "$("$binary" --version)" = "arc $version" ] || fail 'arc failed its version check'
    mkdir -p "$install_dir"
    install_dir=$(CDPATH= cd "$install_dir" && pwd -P)
    destination=$install_dir/arc$exe
    check_destination
    notices_destination=$install_dir/arc-THIRD_PARTY_NOTICES.txt
    [ ! -L "$notices_destination" ] || fail 'Refusing to replace a symbolic link for licenses'
    [ ! -e "$notices_destination" ] || [ -f "$notices_destination" ] || fail 'Refusing to replace a non-regular license file'
    staged=$(mktemp "$install_dir/.arc-install.XXXXXX")
    staged_notices=$(mktemp "$install_dir/.arc-install.XXXXXX")
    cp "$binary" "$staged"
    cp "$notices" "$staged_notices"
    chmod 755 "$staged"
    chmod 644 "$staged_notices"
    mv -f "$staged_notices" "$notices_destination"
    staged_notices=
    mv -f "$staged" "$destination"
    staged=
    printf 'Installed arc %s at %s\n' "$version" "$destination"
    quoted_destination=$(printf '%s' "$destination" | sed "s/'/'\\\\''/g")
    printf "To uninstall, run: rm -- '%s'\n" "$quoted_destination"
    printf 'Bundled licenses: %s\n' "$notices_destination"
    case ":${PATH:-}:" in
        *":$install_dir:"*) ;;
        *) printf 'Add %s to your PATH to run arc.\n' "$install_dir" ;;
    esac
}

build_source() {
    for tool in git bash mise pkg-config ar; do
        command -v "$tool" >/dev/null 2>&1 || fail "Missing dependency: $tool. See docs/build.md"
    done
    if [ "$macos" = 1 ]; then
        command -v brew >/dev/null 2>&1 || fail 'macOS source builds require Homebrew and libarchive'
        brew --prefix libarchive >/dev/null || fail 'Install libarchive with: brew install libarchive pkgconf'
    fi
    printf 'Downloading %s from samedit66/arc...\n' "$ref"
    GIT_TERMINAL_PROMPT=0 git clone --depth 1 --branch "$ref" --single-branch \
        https://github.com/samedit66/arc.git "$work/source" </dev/null
    commit=$(git -C "$work/source" rev-parse HEAD)
    case "$commit" in ''|*[!0-9a-f]*) fail 'Git returned an invalid commit ID' ;; esac
    [ "${#commit}" -eq 40 ] || fail 'Git returned an invalid commit ID'
    release_version=$(cat "$work/source/VERSION")
    printf '%s\n' "$release_version" | LC_ALL=C grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || fail 'Invalid VERSION in downloaded source'
    version=$release_version
    if [ "$ref" = main ]; then version=$release_version+main.$commit
    else [ "$ref" = "v$release_version" ] || fail 'Release tag disagrees with VERSION'; fi
    printf 'Building %s (requires the local compiler and libarchive)...\n' "$version"
    (
        cd "$work/source"
        # Always load this snapshot's build policy, even when invoked from mise.
        unset ARC_BUILD_CONFIG ARC_PLATFORM ARC_EXE_SUFFIX ARC_CFLAGS ARC_LDFLAGS
        unset ARC_SANITIZER_FLAGS FILEKIT_SANITIZER_FLAGS
        export ARC_VERSION=$version ARC_BUILD_DIR=$work/build
        export ARC_SANITIZE=0 FILEKIT_SANITIZE=0 MISE_DISABLE_TOOLS=python,uv,pipx:clang-format
        # Installing explicitly trusts its build scripts and mise config.
        mise trust </dev/null
        mise install </dev/null
        mise exec -- sh -c '
            compiler=${CC:-cc}
            command -v "$compiler" >/dev/null 2>&1 || { echo "Missing C compiler: $compiler" >&2; exit 1; }
            pkg-config --exists libarchive || { echo "Missing libarchive development files; see docs/build.md" >&2; exit 1; }
            exec bash scripts/build.sh
        ' </dev/null
    )
    binary=$work/build/arc$exe
    [ -f "$binary" ] || fail 'Build did not produce arc'
    [ "$("$binary" --version)" = "arc $version" ] || fail 'Built arc failed its version check'
    notices=$work/source/LICENSE
}

download_binary() {
    command -v curl >/dev/null 2>&1 || fail 'Missing dependency: curl'
    case $(uname -m) in
        x86_64|amd64) arch=x86_64 ;;
        aarch64|arm64) arch=arm64 ;;
        *) fail 'No release binary for this architecture; use --from-source' ;;
    esac
    case $(uname -s) in
        Darwin) target=darwin-$arch; extension=tar.gz ;;
        Linux) target=linux-$arch; extension=tar.gz ;;
        *) [ "$arch" = x86_64 ] || fail 'Windows releases require x86_64'; target=windows-x86_64; extension=zip ;;
    esac
    asset=arc-$ref-$target.$extension
    base=https://github.com/samedit66/arc/releases/download/$ref
    printf 'Downloading %s...\n' "$asset"
    curl -fsSL --proto '=https' --tlsv1.2 "$base/SHA256SUMS" -o "$work/SHA256SUMS" || fail 'Release checksums unavailable; older releases need --from-source'
    expected=$(awk -v asset="$asset" '$2 == asset { if (NF != 2 || found++) exit 1; print $1 } END { if (found != 1) exit 1 }' "$work/SHA256SUMS") || fail 'Missing or duplicate asset checksum'
    [ "${#expected}" = 64 ] || fail 'Invalid asset checksum'
    case "$expected" in *[!0-9a-f]*) fail 'Invalid asset checksum' ;; esac
    curl -fsSL --proto '=https' --tlsv1.2 "$base/$asset" -o "$work/asset" || fail 'Release binary download failed'
    if command -v sha256sum >/dev/null 2>&1; then actual=$(sha256sum "$work/asset")
    elif command -v shasum >/dev/null 2>&1; then actual=$(shasum -a 256 "$work/asset")
    else fail 'Need sha256sum or shasum to verify the binary'; fi
    [ "${actual%% *}" = "$expected" ] || fail 'Release binary checksum mismatch'
    if [ "$extension" = zip ]; then
        command -v unzip >/dev/null 2>&1 || fail 'Missing dependency: unzip'
        unzip -p "$work/asset" arc.exe > "$work/arc.exe"
        unzip -p "$work/asset" THIRD_PARTY_NOTICES.txt > "$work/THIRD_PARTY_NOTICES.txt"
        unzip -p "$work/asset" VERSION > "$work/VERSION"
    else
        tar -xzf "$work/asset" -C "$work" arc THIRD_PARTY_NOTICES.txt VERSION
    fi
    version=${ref#v}
    [ "$(cat "$work/VERSION")" = "$version" ] || fail 'Release asset disagrees with its tag'
    binary=$work/arc$exe
    notices=$work/THIRD_PARTY_NOTICES.txt
    [ ! -L "$binary" ] && [ ! -L "$notices" ] && [ -s "$notices" ] || fail 'Invalid release package'
    chmod 755 "$binary"
}

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
check_destination() {
    [ ! -L "$destination" ] || fail "Refusing to replace a symbolic link: $destination"
    if [ -e "$destination" ] && [ ! -f "$destination" ]; then
        fail "Refusing to replace a non-regular file: $destination"
    fi
}
cleanup() {
    [ -z "$staged" ] || rm -f "$staged"
    [ -z "$staged_notices" ] || rm -f "$staged_notices"
    [ -z "$work" ] || rm -rf "$work"
    :
}
main "$@"
