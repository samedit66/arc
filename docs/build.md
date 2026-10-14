# Building arc

Run the following commands from the repository root.

The project targets macOS, Linux and Windows through MSYS2 UCRT64.
Development requires Git, mise, Bash, a platform C11 compiler, pkg-config and
libarchive development files. See the [platform prerequisites](install.md#select-a-version-or-build-from-source).
Mise pins CMake 4.4.4, Ninja 1.13.2, Python 3.13.15, uv 0.12.24 and clang-format
22.1.8. These versions have been exercised on macOS arm64. Compilers come from
the platform: GCC or Clang on Linux, Apple command-line tools on macOS, and
MSYS2 UCRT64 GCC on Windows. `CC` and `CXX` override the platform defaults (Clang on macOS,
GCC on Linux/Windows). Python and formatting tools are development dependencies.

## Build with mise

```sh
mise trust
mise install
mise run configure
mise run build
mise run test
mise run check
mise run format
mise run run -- --help
mise run run -- list example.zip
```

`configure` initializes the `dev` preset only when needed. `build` depends on
it and uses incremental CMake/Ninja compilation. CMake regenerates changed
CMake files and `VERSION` during a build; the wrapper reapplies changed presets. `test` builds first, runs C API tests
through CTest, then the locked Python CLI suite through uv. Arguments to `test`
are forwarded to pytest. `check` also runs independent filekit checks,
CMake/pkg-config consumers and formatting validation without editing sources.
`format` is the explicit source-modifying task.

`CMakePresets.json` defines Debug `dev`, Debug `sanitize`, Release `release`
(with system libraries), and `release-vcpkg` (for distributable native builds).
All use Ninja, enable the CLI and API tests, and export compile commands.
Compiler flags, sources, platform handling and installation belong to CMake.
Development uses system libarchive; on macOS the helper discovers Homebrew's
libarchive prefix while preserving additional `CMAKE_PREFIX_PATH` entries.

Development output defaults to `build/dev`; sanitizer output to `build/sanitize`.
`ARC_BUILD_DIR` chooses a separate directory. `ARC_CMAKE_PRESET` selects the
preset (`dev` by default); `ARC_SANITIZE=1` selects `sanitize` for compatibility.
`ARC_VERSION` overrides the CLI identification, while package metadata always
uses `VERSION`:

```sh
ARC_VERSION=0.1.0+local mise run build
ARC_SANITIZE=1 CC=clang mise run test
```

Use a separate directory when switching compilers or sanitizer mode. Existing
caches reject a different explicit `CC` rather than silently using the previous
compiler. Changes to other configure inputs (toolchain, prefix, user flags)
require explicit reconfiguration as well:

```sh
CC=clang CXX=clang++ ARC_BUILD_DIR=build/clang mise run test
CC=gcc CXX=g++ ARC_BUILD_DIR=build/gcc mise run test
# Explicitly reset a selected configuration when appropriate:
CC=clang cmake --fresh --preset dev
```

The build/run/test shell helpers remain thin CMake/CTest/process wrappers.
They contain no source compilation or compiler flags. Prefer mise, which
provides the pinned tools; external consumers do not require it.

## Release and distribution

```sh
mise run release
mise run dist
```

`release` prepares the pinned vcpkg checkout and dependencies, builds Release,
and checks the binary version, architecture and dynamic dependencies. `dist`
depends on it, runs CTest and the full Python suite against that binary, then
packages it. Both select the current host; cross-compiling all targets is not
required. Output defaults to `build/release/<target>/{arc,deps,vcpkg,assets}`.
`ARC_RELEASE_BUILD_DIR` overrides the target-specific root; do not share it
between toolchains or targets. `RELEASE_TARGET` selects a native CI target.

Distributions include the executable, README, arc license, dependency/runtime
notices, `VERSION` and `BUILD_INFO` (version, target, dependency triplet).
Dependency versions and the vcpkg checkout are pinned by the existing
`release/vcpkg.json` baseline. The system-library `release` preset is available
for direct CMake users; `mise run release` uses `release-vcpkg` to retain static
release linkage. See [release prerequisites and platform policy](../release/README.md).

## Direct CMake without mise

With CMake 3.21+, Ninja, a C compiler and libarchive development files installed:

```sh
# On macOS, add -DCMAKE_PREFIX_PATH="$(brew --prefix libarchive)" if needed.
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix /your/prefix
```

CTest covers C API tests; the full development suite additionally needs the
mise-managed Python/uv tasks. Presets are conveniences, not an embedding
requirement: ordinary `cmake -S . -B <dir>` and `add_subdirectory` remain supported.

## Publishing a release

Releases are published by `.github/workflows/ci.yml` when a stable version tag
is pushed. Use `vMAJOR.MINOR.PATCH` without leading zeroes, for example `v0.1.0`.
Prerelease and build-metadata tags are not supported by this release workflow.

Commit the intended version to the root `VERSION` file and push the commit to
`main` before pushing its tag. The tag must match `VERSION` in the tagged
commit, and that commit must belong to `main` history. For example, once
`VERSION` contains `0.1.1` and the release commit is on `main`:

```sh
git push origin main
git tag -a v0.1.1 -m "Release v0.1.1"
git push origin v0.1.1
```

The workflow validates the tag and runs the complete Ubuntu, macOS and Windows
checks on the tagged commit. Publication depends on all checks succeeding,
including consumer integration, filekit checks and the configured sanitizers.
Before publishing, it refreshes the remote tag and `main` and validates again
to reject a tag moved to a different commit while checks were running.

The publication job alone receives `contents: write`. It creates a stable
GitHub Release with generated release notes, five tested binary packages and
`SHA256SUMS`. Assets are uploaded into a draft before publication; GitHub also
provides source archives. Installers download and verify a release binary by
default. See [release build details](../release/README.md).
Branch pushes, pull requests and manual workflow runs perform checks without
publishing a release.

If checks fail, the tag remains but no release is published. Rerun the failed
workflow after a transient infrastructure failure. Code fixes require a new
commit and tag. Rerunning a successful release leaves the existing stable
release unchanged. A draft created by this workflow for the same commit can
resume after an interrupted upload; unrelated drafts and prereleases are rejected.
Keep published versions immutable and release corrections under a new version.

The tag must point to a commit containing this workflow and its validation
script. A tag on an older commit cannot use release automation added later.

## Windows

Use the [source build prerequisites](install.md#select-a-version-or-build-from-source), then run the mise
build commands above from the repository root in the UCRT64 terminal.
To launch from PowerShell, include MSYS2's `ucrt64\bin` in `PATH` for runtime DLLs.
MSVC is not currently verified.

## Embedding the library

The public writer type is `ArcWriter`. The header supports C and C++.
Compile the implementation as C11, even when the application uses C++.
Windows support remains MSYS2 UCRT64; MSVC is not currently verified.

### CMake from source

CMake 3.21 or newer and libarchive development files are required. No mise,
Bash or Python is needed by this build path.

```cmake
add_subdirectory(third_party/arc)
target_link_libraries(my_app PRIVATE arc::arc)
```

The CLI defaults to off when embedded. `ARC_BUILD_CLI` selects it explicitly.
C11, large-file support, platform sources and PIC apply to the library;
development warnings are opt-in with `ARC_DEVELOPER_WARNINGS`. The Windows
`-municode` flag applies only to the CLI.
On macOS select Homebrew libarchive with
`-DCMAKE_PREFIX_PATH="$(brew --prefix libarchive)"` when configuring.

### Installed CMake package

```sh
cmake -S . -B build -DARC_BUILD_CLI=OFF -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build
cmake --install build
```

Then configure the application with `-DCMAKE_PREFIX_PATH=/your/prefix`:

```cmake
find_package(arc CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE arc::arc)
```

Installation supplies `arc.h`, one static library containing the private
filekit implementation, and CMake/pkg-config metadata. Libarchive remains an
external dependency; the CMake package resolves it in the consuming build.
CMake and pkg-config report the version from `VERSION`. Installed CMake packages
support version requests with exact compatibility during the 0.x series:
`find_package(arc 0.1.0 EXACT CONFIG REQUIRED)`. LICENSE is installed alongside
the package under `share/licenses/arc` (or the configured datadir).

### Make or direct compiler invocation

Add the installed `lib/pkgconfig` (or configured libdir) to `PKG_CONFIG_PATH`.
The metadata includes the external libarchive dependency:

```sh
cc $(pkg-config --cflags arc) -c app.c -o app.o
cc app.o $(pkg-config --libs arc) -o app
```

The shell examples above assume paths without spaces. For paths containing
spaces, use CMake or parse pkg-config output as shell words rather than using
shell word splitting. For example, with Python available:

```python
import shlex
import subprocess

def flags(*args):
    return shlex.split(subprocess.check_output(["pkg-config", *args, "arc"], text=True))

subprocess.run(["cc", *flags("--cflags"), "app.c", *flags("--libs"), "-o", "app"], check=True)
```

Use the C++ compiler for a C++ consumer and final C++ link. If linking
libarchive statically, use `pkg-config --static --libs arc` to request its
private dependency metadata and select static dependency artifacts separately.
This flag alone does not force a fully static executable. Homebrew's
libarchive metadata may need an additional `-L"$(brew --prefix)/lib"` for its
private compression libraries.
Compatible libarchive shared libraries/DLLs must be available at runtime when
linked dynamically. Match target architecture and Windows CRT across builds.

The mise build also produces `libarc.a` in its build directory. For manual
linking, add the source root to the consumer's include path and link `libarc.a`
followed by libarchive's pkg-config link arguments. Consumers do not need
filekit headers or a separate filekit library.

### Consumer verification

```sh
mise run consumers
```

Requires CMake, C/C++ compilers, pkg-config, libarchive and the mise-managed
Python. Checks source-tree
and relocated installed CMake consumers, plus pkg-config consumers with spaces
in the installation path. Windows
consumers exercise both header orders with GDI enabled.

## Development tools

`mise run format` formats the C sources. `mise run check` builds the project,
runs the CLI tests and checks C formatting; both formatting tasks require
clang-format. See [the test guide](../tests/README.md) for focused runs,
sanitizers and independent filekit checks.

## Migrating the everyday API

Rebuild consumers together with the updated library: `ArcResult` has grown and
`arc_unpack` now takes only archive and destination. Use `arc_unpack_ex` with
`ArcExtractOptions` for merge mode, passwords, selection or root stripping;
`ArcUnpackOptions` has been removed. `arc_pack` and `arc_pack_ex` own the complete
writer lifecycle. See [the tutorial](tutorial.md#use-it-from-c).
