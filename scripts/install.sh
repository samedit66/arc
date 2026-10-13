#!/bin/sh
# Keep execution inside main: a truncated curl response must not start installing.
main() {
    set -eu
    umask 077
    install_dir=${HOME:?HOME must be set}/.local/bin
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --install-dir)
                [ "$#" -ge 2 ] && [ -n "$2" ] || fail '--install-dir requires a directory'
                install_dir=$2
                shift 2 ;;
            --help)
                printf 'Usage: sh install.sh [--install-dir DIRECTORY]\nInstalls the latest main from samedit66/arc using local build dependencies.\n'
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
    destination=$install_dir/arc$exe
    check_destination
    work=
    staged=
    trap cleanup 0
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
    work=$(mktemp -d "${TMPDIR:-/tmp}/arc-install.XXXXXX")
    printf 'Downloading main from samedit66/arc...\n'
    GIT_TERMINAL_PROMPT=0 git clone --depth 1 --branch main --single-branch \
        https://github.com/samedit66/arc.git "$work/source" </dev/null
    commit=$(git -C "$work/source" rev-parse HEAD)
    case "$commit" in ''|*[!0-9a-f]*) fail 'Git returned an invalid commit ID' ;; esac
    [ "${#commit}" -eq 40 ] || fail 'Git returned an invalid commit ID'
    printf 'Building main.%s (requires the local compiler and libarchive)...\n' "$commit"
    (
        cd "$work/source"
        # Always load this snapshot's build policy, even when invoked from mise.
        unset ARC_BUILD_CONFIG ARC_PLATFORM ARC_EXE_SUFFIX ARC_CFLAGS ARC_LDFLAGS
        unset ARC_SANITIZER_FLAGS FILEKIT_SANITIZER_FLAGS
        export ARC_VERSION=main.$commit ARC_BUILD_DIR=$work/build
        export ARC_SANITIZE=0 FILEKIT_SANITIZE=0 MISE_DISABLE_TOOLS=python,uv
        # Installing from main explicitly trusts its build scripts and mise config.
        mise trust </dev/null
        mise exec -- sh -c '
            command -v "$CC" >/dev/null 2>&1 || { echo "Missing C compiler: $CC" >&2; exit 1; }
            pkg-config --exists libarchive || { echo "Missing libarchive development files; see docs/build.md" >&2; exit 1; }
            exec bash scripts/build.sh
        ' </dev/null
    )
    binary=$work/build/arc$exe
    [ -f "$binary" ] || fail 'Build did not produce arc'
    [ "$("$binary" --version)" = "arc main.$commit" ] || fail 'Built arc failed its version check'
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
    printf 'Installed arc main.%s at %s\n' "$commit" "$destination"
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
