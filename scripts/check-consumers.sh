#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
if [[ "$(uname -s)" == Darwin ]]; then
    export CMAKE_PREFIX_PATH="$(brew --prefix libarchive)${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/arc-consumers.XXXXXX")
trap 'rm -rf "$work"' EXIT
cmake -S tests/consumer -B "$work/source" -DARC_SOURCE_DIR="$root" -DARC_BUILD_CLI=OFF
cmake --build "$work/source"
ctest --test-dir "$work/source" --output-on-failure
cmake -S . -B "$work/library" -DARC_BUILD_CLI=ON -DARC_DEVELOPER_WARNINGS=ON -DCMAKE_INSTALL_PREFIX="$work/prefix" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$work/library"
"$work/library/arc" --help
[[ "$("$work/library/arc" --version)" == "arc $(cat VERSION)" ]]
cmake --install "$work/library"
[[ -f "$work/prefix/share/licenses/arc/LICENSE" ]]
mv "$work/prefix" "$work/relocated with spaces"
cmake -S tests/consumer -B "$work/installed" -DCMAKE_PREFIX_PATH="$work/relocated with spaces" -DARC_EXPECT_VERSION="$(cat VERSION)"
cmake --build "$work/installed"
ctest --test-dir "$work/installed" --output-on-failure
if cmake -S tests/consumer -B "$work/incompatible" -DCMAKE_PREFIX_PATH="$work/relocated with spaces" -DARC_EXPECT_VERSION=999.0.0 > "$work/incompatible.log" 2>&1; then
    printf 'Installed package accepted an incompatible version\n' >&2
    exit 1
fi
export PKG_CONFIG_PATH="$work/relocated with spaces/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
case "$(uname -s)" in
    MINGW*|MSYS*)
        # Native pkg-config needs Windows paths and a semicolon-separated list.
        export PKG_CONFIG_PATH="$(cygpath -m -p "$PKG_CONFIG_PATH")"
        ;;
esac
[[ "$(pkg-config --modversion arc)" == "$(cat VERSION)" ]]
python tests/consumer/check_pkg_config.py "$work" "$root"
