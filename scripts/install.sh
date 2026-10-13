#!/bin/sh
# Keep execution inside main: a truncated curl response must not start installing.
main() {
    set -eu
    umask 077
    install_dir=${HOME:?HOME must be set}/.local/bin
    ref=latest
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
            --help)
                printf 'Usage: sh install.sh [--install-dir DIRECTORY] [--version latest|main|vMAJOR.MINOR.PATCH]\nInstalls the latest stable release from samedit66/arc using local build dependencies.\n'
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
    for tool in git bash mise pkg-config ar mktemp; do
        command -v "$tool" >/dev/null 2>&1 || fail "Missing dependency: $tool. See docs/build.md"
    done
    if [ "$macos" = 1 ]; then
        command -v brew >/dev/null 2>&1 || fail 'macOS requires Homebrew and its libarchive package'
        brew --prefix libarchive >/dev/null || fail 'Install libarchive with: brew install libarchive pkgconf'
    fi
    case "$ref" in
        latest)
            command -v curl >/dev/null 2>&1 || fail 'Missing dependency: curl'
            release_url=$(curl -fsSL -o /dev/null -w '%{url_effective}' https://github.com/samedit66/arc/releases/latest) || fail 'No stable release available; use --version main explicitly'
            ref=${release_url##*/} ;;
    esac
    if [ "$ref" != main ]; then
        printf '%s\n' "$ref" | LC_ALL=C grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' || fail 'Expected main or a release tag vMAJOR.MINOR.PATCH'
    fi
    destination=$install_dir/arc$exe
    check_destination
    work=
    staged=
    trap cleanup 0
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
    work=$(mktemp -d "${TMPDIR:-/tmp}/arc-install.XXXXXX")
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
        export ARC_SANITIZE=0 FILEKIT_SANITIZE=0 MISE_DISABLE_TOOLS=python,uv
        # Installing explicitly trusts its build scripts and mise config.
        mise trust </dev/null
        mise exec -- sh -c '
            command -v "$CC" >/dev/null 2>&1 || { echo "Missing C compiler: $CC" >&2; exit 1; }
            pkg-config --exists libarchive || { echo "Missing libarchive development files; see docs/build.md" >&2; exit 1; }
            exec bash scripts/build.sh
        ' </dev/null
    )
    binary=$work/build/arc$exe
    [ -f "$binary" ] || fail 'Build did not produce arc'
    [ "$("$binary" --version)" = "arc $version" ] || fail 'Built arc failed its version check'
    mkdir -p "$install_dir"
    install_dir=$(CDPATH= cd "$install_dir" && pwd -P)
    destination=$install_dir/arc$exe
    check_destination
    # Stage in the destination filesystem so rename cannot expose a partial copy.
    staged=$(mktemp "$install_dir/.arc-install.XXXXXX")
    cp "$binary" "$staged"
    chmod 755 "$staged"
    mv -f "$staged" "$destination"
    staged=
    printf 'Installed arc %s at %s\n' "$version" "$destination"
    # Single-quote the path for a command users can paste, including apostrophes.
    quoted_destination=$(printf '%s' "$destination" | sed "s/'/'\\\\''/g")
    printf "To uninstall, run: rm -- '%s'\n" "$quoted_destination"
    case ":${PATH:-}:" in
        *":$install_dir:"*) ;;
        *) printf 'Add %s to your PATH to run arc.\n' "$install_dir" ;;
    esac
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
    [ -z "$work" ] || rm -rf "$work"
    :
}
main "$@"
