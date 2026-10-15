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
CMake files and `VERSION` during a build; the wrapper reapplies changed presets. `test` builds first, runs C API and fs module tests
through CTest, then the locked Python CLI suite through uv. Arguments to `test`
are forwarded to pytest. `check` also runs
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

## Cleaning and moving the checkout

```sh
mise run clean -- --help
mise run clean
```

`clean` removes the checkout's `build/` tree (including downloaded vcpkg and
installed release dependencies), `tests/.venv`, pytest/Python caches and legacy
root binaries (`arc`, `arc.exe`, `arc.o`, `libarc.a`). Sources, checked-in fixtures,
documentation and `.git` remain intact. External directories selected by
`ARC_BUILD_DIR`, `ARC_RELEASE_BUILD_DIR` or `UV_PROJECT_ENVIRONMENT`, and global
mise/uv/vcpkg caches are not removed. Do not run clean concurrently with builds.
The next build or test recreates its local outputs and locked dependencies.

After renaming or moving a checkout, the development and release wrappers detect
outdated source/build paths and reconfigure CMake with `--fresh`. Existing release
dependencies are retained. Recreate a moved Python environment with `mise run clean`
or remove only `tests/.venv` before running tests: its launchers contain absolute paths.

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
including consumer integration, fs checks and the configured sanitizers.
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
fs implementation, and CMake/pkg-config metadata. Libarchive remains an
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
fs headers or a separate fs library.

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

Run `mise run bench` to build a native Release binary and compare Arc with
`tar` for TAR/TAR.GZ and `zip`/`unzip` for ZIP. Requires these tools on PATH;
the Python script uses only the standard library. This native build uses the
local libarchive, unlike the pinned static distribution from `mise run release`.

The benchmark generates 2,000 small text files, a 16 MiB text file and a
16 MiB seeded pseudorandom binary file in a temporary directory, then removes
all fixtures, archives and extracted files automatically. Nothing large is
stored in the repository. To run a smaller experiment:

```sh
mise run bench -- --size-mib 1 --small-files 100 --runs 2
```

Options include `--seed` (default 1729), `--size-mib`, `--small-files`, `--runs`,
`--output` and optional `--ouch /path/to/ouch`. Ouch is not downloaded or
installed by the benchmark. To benchmark an existing binary without building, use
`python scripts/bench.py --arc /absolute/path/to/arc`.

Each format and dataset gets one warm-up per tool and five measured runs,
with rotating tool order. The table reports median wall-clock milliseconds
for packing and unpacking and archive bytes. Fresh output paths are used each
time; SHA-256 content checks run outside the timed commands. Raw samples go
to `build/bench/results.csv`; the adjacent JSON records fixture parameters,
content hashes, platform, executable paths and tool versions.

This measures CLI defaults with warm filesystem caches, including process
startup and filesystem work. Compression settings can differ between tools;
compare archive size alongside time. Extraction safety checks also differ.
Times do not include an explicit disk sync and are not cold-cache or durable
write measurements. Seed and fixture version reproduce file contents, not
identical timing or archive bytes across different tools and platforms.
See [benchmark results and methodology comparison](benchmark.md) for a recorded
run including ouch and the limitations of comparing these tools.

`mise run bench-ouch -- --ouch /path/to/ouch --hyperfine /path/to/hyperfine`
builds Arc in Release and runs ouch's Rust 1.65.0 workloads with Arc added.
It downloads the pinned Rust checkout into a temporary directory, removes
`.git` from the benchmark copy and uses the compiler subtree for compressed
formats. Four warm-ups and ten measured runs per command use Hyperfine;
all readers extract the same ouch-created archive. Payloads are removed on
exit. `--source /path/to/rust` reuses an existing checkout at the pinned
revision, without changing it. JSON, Markdown, sizes and raw timing CSV reports
go to `build/bench/ouch-workload`. Requires git and the same archive tools.

The optional `scripts/plot-bench.py` renders committed CSV snapshots as SVG
and PNG figures in `docs/assets/benchmark`. It needs matplotlib only for
plotting, not for building Arc or running either benchmark:

```sh
mise exec -- uv run --no-project --python 3.13.15 --with matplotlib==3.11.2 python scripts/plot-bench.py
```

For ZIP backend comparisons, `scripts/bench-zip.py` accepts multiple Arc builds
and an optional libarchive-only probe, checks generated payloads and compares
native versus explicit UTC environments on the same extraction input.
See the [combined benchmark report](benchmark.md#optimization-experiments)
for results. JSON metadata records `TZ` for timezone-sensitive comparisons:

```sh
mise exec -- python scripts/bench-zip.py --ouch /path/to/ouch --compare-utc
mise exec -- python scripts/bench-tar.py --ouch /path/to/ouch --runs 5
python scripts/plot-bench.py --zip-experiment docs/benchmark-zip-results.csv
python scripts/plot-bench.py --tar-experiment docs/benchmark-tar-results.csv
```

Both scripts accept `--arc-before`; ZIP also accepts `--arc-experimental` and
`--libarchive`, while TAR accepts `--arc-metadata` and `--source`. Build each
selected Arc in native Release with the same backend. TAR's recorded baseline
is `28545fb`; its intermediate build adds only timestamp cleanup to that
revision. Generated TAR fixtures and optional source copies are temporary.

For buffer/traversal comparisons, `bench-io.py` accepts named builds; the first
build creates the shared extraction input and validates each packed archive.
Every warm-up and measured output is checked outside the timer. It generates
temporary fixtures and writes CSV/JSON with parameters and binary hashes:

```sh
mise exec -- python scripts/bench-io.py --binary before=/path/to/old/arc --binary after=/path/to/new/arc --size-mib 128 --datasets text binary --runs 15
mise exec -- python scripts/bench-io.py --binary before=/path/to/old/arc --binary after=/path/to/new/arc --size-mib 128 --datasets text binary --operations unpack --pause-ms 500 --runs 15
mise exec -- python scripts/bench-io.py --binary before=/path/to/old/arc --binary after=/path/to/new/arc --size-mib 1 --small-files 10000 --datasets small --formats tar tar.gz zip --runs 7
python scripts/plot-bench.py --io-experiment docs/benchmark-io-results.csv
```

Use native Release builds with the same compiler and backend; the I/O baseline
is `ea5a458`. The retained build changes only the extraction buffer to 256 KiB
and reuses POSIX traversal metadata. Candidate measurements additionally tried
256 KiB/1 MiB buffers in both directions and a temporary block-reader packing
prototype with zero filling for sparse gaps; those prototypes are not shipped.

The POSIX diagnostic probe uses the same libarchive as Arc:

```sh
export PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -p build/bench
cc -std=c11 -O3 scripts/bench-libarchive.c -o build/bench/libarchive-probe $(pkg-config --cflags --libs libarchive)
mise exec -- python scripts/bench-zip.py --ouch /path/to/ouch --libarchive build/bench/libarchive-probe --compare-utc
```

The optional zlib-ng prototype uses `ZLIB_COMPAT=ON`, `BUILD_SHARED_LIBS=ON`
and `CMAKE_BUILD_TYPE=Release`, with an isolated libarchive linked to it.
The recorded Mac experiment copied the libarchive dylib, redirected its zlib
load path, renewed its signature and linked only the experimental executable
to that copy. Its level-9 variant sets ZIP `compression-level=9` in an isolated
adapter. These are diagnostics, not a supported distribution configuration.

For compression curves, `scripts/bench-compression.c` is an experimental
adapter replacement, compiled separately and linked before the native Release
static library. It reads `ARC_BENCH_LEVEL` only in the probe executable;
production Arc has no such switch. Use the compiler from the CMake cache:

```sh
ARC_CMAKE_PRESET=release mise run build
# macOS Homebrew; omit this export when pkg-config already finds libarchive:
export PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -p build/bench/compression
bench_cc=$(sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p' build/release/CMakeCache.txt)
"$bench_cc" -std=c11 -O3 -DNDEBUG -fPIC -D_FILE_OFFSET_BITS=64 -I. -Isrc -Isrc/internal $(pkg-config --cflags libarchive) -c scripts/bench-compression.c -o build/bench/compression/adapter.o
"$bench_cc" build/release/CMakeFiles/arc_cli.dir/src/cli/*.o build/bench/compression/adapter.o build/release/libarc.a $(pkg-config --libs libarchive) -lpthread -o build/bench/compression/arc-zlib
mise exec -- python scripts/bench-compression.py --backend zlib=build/bench/compression/arc-zlib --ouch /path/to/ouch
```

To include zlib-ng, relink the same objects against an isolated libarchive
linked to zlib-ng, using the prototype setup above, and add
`--backend ng=/path/to/arc-ng`. Verify its actual dependencies with `otool -L`
on macOS or `ldd` on Linux. No probe or backend is added to production builds.
Useful runner options:

- `--datasets`, `--sizes-kib`, `--levels`, `--formats`, `--runs`, `--output`.
- `--ouch-threads 1 --ouch-levels 1 3 6 9` for one-worker curves; ouch ZIP
  ignores levels and is sampled at default only.
- `--compare-ouch-threads --formats tar.gz --levels 0 --sizes-kib 16384 --runs 7`
  interleaves ouch's default workers and one worker on identical bytes.
- `--datasets structured mixed front-random front-text buried-text --sizes-kib 4096 --seed 271828 --levels 0 3 6 9 --evaluate-policy`
  evaluates the illustrative offline rule and deliberate sampling counterexamples.
  This requires backends named `zlib` and `ng`; results go to JSON. It is not a
  runtime policy, and its sample cost is measured separately.

CSV/JSON records levels, versions, binary hashes, fixture manifest hashes and
sampling features/costs. Payloads and archives are temporary. The committed
report combines cohorts using an `experiment` CSV column (`sweep`,
`single-thread`, `threads`, `holdout`); retain each run's JSON parameters.
Render its large-input, one-worker curves with:

```sh
python scripts/plot-bench.py --compression-experiment docs/benchmark-compression-results.csv
```

`mise run format` formats the C sources. `mise run check` builds the project,
runs the CLI tests and checks C formatting; both formatting tasks require
clang-format. See [the test guide](../tests/README.md) for focused runs,
sanitizers and fs module checks.

## Migrating the everyday API

Rebuild consumers together with the updated library: `ArcResult` has grown and
`arc_unpack` now takes only archive and destination. Use `arc_unpack_ex` with
`ArcUnpackOptions` for merge mode, passwords, selection or root stripping;
`ArcExtractOptions` has been renamed to `ArcUnpackOptions`. `arc_pack` and `arc_pack_ex` own the complete
writer lifecycle. See [the tutorial](tutorial.md#use-it-from-c).

### Migrating progress and pack options

Recompile the library and all consumers together: public option layouts have changed.
Rename `ArcExtractOptions` to `ArcUnpackOptions`. In `ArcPackOptions`, replace the
`configuration` pointer with direct `format`, `compression` and `password` fields.
When copying an old explicit configuration, copy all three fields to retain its
encoding. Omitted compression with an explicit format now means `"none"`; with an
inferred format it follows the suffix. Zero-initialize all option structures.

Pack, unpack and read options now accept an `ArcObserver`. Its context is borrowed
until the operation returns, or until reader close for sequential readers. NULL
unpack options use the same safe defaults as a zero initializer. See the tutorial
for progress counters, phases and callback lifetime rules.
