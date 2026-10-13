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
mv "$work/prefix" "$work/relocated"
cmake -S tests/consumer -B "$work/installed" -DCMAKE_PREFIX_PATH="$work/relocated" -DARC_EXPECT_VERSION="$(cat VERSION)"
cmake --build "$work/installed"
ctest --test-dir "$work/installed" --output-on-failure
if cmake -S tests/consumer -B "$work/incompatible" -DCMAKE_PREFIX_PATH="$work/relocated" -DARC_EXPECT_VERSION=999.0.0 > "$work/incompatible.log" 2>&1; then
    printf 'Installed package accepted an incompatible version\n' >&2
    exit 1
fi
export PKG_CONFIG_PATH="$work/relocated/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
[[ "$(pkg-config --modversion arc)" == "$(cat VERSION)" ]]
read -r -a compile_flags <<< "$(pkg-config --cflags arc)"
read -r -a libraries <<< "$(pkg-config --libs arc)"
read -r -a static_libraries <<< "$(pkg-config --static --libs arc)"
static_search_flags=()
if [[ "$(uname -s)" == Darwin ]]; then
    # Homebrew libarchive.pc lists private libs without their search directory.
    static_search_flags+=("-L$(brew --prefix)/lib")
fi
for language in c cpp; do
    compiler="${CC:-cc}"
    if [[ "$language" == cpp ]]; then compiler="${CXX:-c++}"; fi
    cp tests/consumer/main.c "$work/main.$language"
    platform_libraries=()
    case "$(uname -s)" in MINGW*|MSYS*) platform_libraries+=(-lgdi32) ;; esac
    for order in windows_first arc_first; do
        "$compiler" "${compile_flags[@]}" -DARC_HEADER_FIRST=$([[ "$order" == arc_first ]] && echo 1 || echo 0) \
            "$work/main.$language" "${libraries[@]}" ${platform_libraries[@]+"${platform_libraries[@]}"} -o "$work/consumer"
        (cd "$work"; ./consumer; rm consumer.tar)
        "$compiler" "${compile_flags[@]}" "$work/main.$language" \
            ${static_search_flags[@]+"${static_search_flags[@]}"} "${static_libraries[@]}" \
            ${platform_libraries[@]+"${platform_libraries[@]}"} -o "$work/consumer"
        (cd "$work"; ./consumer; rm consumer.tar)
    done
done
