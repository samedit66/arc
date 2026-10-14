# Shared artifact selection; compiler and linker policy belongs to CMake.
preset=${ARC_CMAKE_PRESET:-dev}
if [[ "${ARC_SANITIZE:-0}" == 1 ]]; then preset=sanitize; fi
build_dir=${ARC_BUILD_DIR:-$PWD/build/$preset}
if command -v cygpath >/dev/null 2>&1; then build_dir=$(cygpath -u "$build_dir"); fi
case "$build_dir" in /*) ;; *) build_dir="$PWD/$build_dir" ;; esac
exe=
case "$(uname -s)" in MINGW*|MSYS*) exe=.exe ;; esac
