# Building arc

Run the following commands from the repository root.

The project targets macOS, Linux and Windows through MSYS2 UCRT64.
The mise build requires Git, mise, Bash, a C11 compiler, `ar`, pkg-config and
libarchive development files. For platform setup, see the
[installation prerequisites](install.md#prerequisites).

For embedding with CMake, see [Embedding the library](#embedding-the-library);
that build path does not require mise or its development tools.

## Build with mise

```sh
mise trust
mise install
mise run build
mise run run -- --help
```

Build output stays outside the source tree, in the temporary directory by
default. Set `ARC_BUILD_DIR` to choose another location and `CC` to choose the
compiler; `AR` selects the static-library archiver. `ARC_VERSION` sets the
CLI version override (default: the root `VERSION` file), and `ARC_SANITIZE=1` enables ASan/UBSan on macOS or
Linux. Direct calls to `scripts/build.sh`, `scripts/run.sh` and
`scripts/test.sh` load the configuration through `mise exec` when necessary.
Standalone `filekit/build.sh` retains its own defaults and does not require mise.
`mise run run` builds the application and forwards its arguments.

The root `VERSION` file is the release version source for shell builds, CMake
and package metadata. Local builds report that version. To override CLI build
identification without changing the package version:

```sh
ARC_VERSION=0.1.0 mise run build
ARC_VERSION=0.1.0 mise run run -- --version
```

The root `mise.toml` declares compiler defaults (Clang on macOS, GCC on Linux
and MSYS2 Windows), C11 warnings, platform selection and sanitizer flags.
Arc and its embedded filekit use the same compiler. `pkg-config` supplies
libarchive's include and link arguments. On macOS, Homebrew's libarchive path
is prepended to existing `PKG_CONFIG_PATH` entries.
Python and uv are pinned for development tools; they are not runtime dependencies.

## Windows

Use the [MSYS2 UCRT64 setup](install.md#windows-msys2-ucrt64), then run the mise
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
mise exec -- bash scripts/check-consumers.sh
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
