# Sourced by build/test/run scripts so all three select the same artifact.
if [[ "${ARC_BUILD_CONFIG:-}" != 1 ]]; then
    printf 'Run through mise to load the build configuration\n' >&2
    exit 1
fi
platform="$ARC_PLATFORM"
exe="$ARC_EXE_SUFFIX"
if [[ "$platform" == windows ]]; then
    temporary_root="${TEMP:-/tmp}"
else
    temporary_root="${TMPDIR:-/tmp}"
fi
build_dir="${ARC_BUILD_DIR:-$temporary_root/arc-build}"
if command -v cygpath >/dev/null 2>&1; then
    build_dir="$(cygpath -u "$build_dir")"
fi
case "$build_dir" in /*) ;; *) build_dir="$PWD/$build_dir" ;; esac
