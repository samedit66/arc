# Building arc

Run the following commands from the repository root.

The project targets macOS, Linux and Windows through MSYS2 UCRT64. Install
mise, Bash, a C11 compiler, libarchive development files and pkg-config. On
macOS the build uses Homebrew's libarchive. `mise.toml` pins Python and uv for
the development tools; these are not runtime dependencies of arc.

```sh
mise trust
mise install
mise run build
mise run run -- --help
```

Build output stays outside the source tree, in the temporary directory by
default. Set `ARC_BUILD_DIR` to choose another location and `CC` to choose the
compiler. `mise run run` builds the application and forwards its arguments.

For library use, include `arc.h` and link against the arc objects built by
`mise run build`, `filekit/libfilekit.a` from the build directory, and libarchive.
The source and platform-specific object lists are in [scripts/build.sh](../scripts/build.sh).

Filesystem operations live in [filekit](../filekit/README.md), a separate C11
sublibrary with no libarchive dependency.

## Windows

Run the commands inside MSYS2 UCRT64 with its compiler, pkg-config and libarchive
on PATH. `MISE_BASH_PATH` can explicitly select the MSYS2 Bash.

The arc build defines `NOGDI` to exclude Windows GDI declarations: GDI's `Arc()`
function conflicts with arc's public `Arc` type. Applications including both
`arc.h` and Windows headers must also define `NOGDI` before the Windows headers
are included, including indirect inclusion through libarchive headers.

## Development tools

`mise run format` formats the C sources. `mise run check` builds the project,
runs the CLI tests and checks C formatting; both formatting tasks require
clang-format. See [the test guide](../tests/README.md) for focused runs,
sanitizers and independent filekit checks.
