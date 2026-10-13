# Sourced by build/test/run scripts so all three select the same artifact.
case "$(uname -s)" in
    MINGW*|MSYS*) platform=windows; exe=.exe; temporary_root="${TEMP:-/tmp}" ;;
    *) platform=unix; exe=; temporary_root="${TMPDIR:-/tmp}" ;;
esac
build_dir="${ARC_BUILD_DIR:-$temporary_root/arc-build}"
if command -v cygpath >/dev/null 2>&1; then
    build_dir="$(cygpath -u "$build_dir")"
fi
case "$build_dir" in /*) ;; *) build_dir="$PWD/$build_dir" ;; esac
