<div align="center">

# Ꭿrc

The friendly C11 archive library and CLI you’ve always wanted.

</div>

I originally built it as a backend for another library, then gave it a project
of its own. That made the API easier to maintain—and gave me a tool of my own.

## Highlights

- Usability first, features second 💅
- Powered by the battle-tested [libarchive](https://www.libarchive.org/) 🛡️
- Unicode and cross-platform support from the start 🌍
- Handles most everyday archive tasks
- Easy to embed in your application

## In a hurry?

```sh
arc pack project.zip ./src ./README.md # pack your files
arc unpack project.zip                 # unpack into ./project
```

## Installation

> [!WARNING]
> This installs the development version from `main`, not a release; it may not
> have passed CI. The installer downloads and runs repository build scripts and
> calls `mise trust` to allow execution of its mise configuration. It replaces an
> existing regular `arc` binary in the installation directory. System dependencies
> must be installed beforehand and remain available to run arc.

macOS / Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh
```

Windows (PowerShell, with MSYS2 UCRT64 installed):

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.ps1)))
```

Requires Git, mise, Bash, a C compiler supporting C11, `ar`, pkg-config and
libarchive development files (Homebrew's libarchive on macOS). See
[Building arc](docs/build.md) for setup; the current shell installer also works
inside MSYS2 UCRT64.

Installs to `~/.local/bin` by default; pass `--install-dir /absolute/path` via
`sh -s --` to choose another directory. Add that directory to your `PATH` if needed.
PowerShell installs to `%USERPROFILE%\.local\bin`; use `-InstallDir` to change it
and `-Msys2Root` if MSYS2 is outside `C:\msys64`. To run from PowerShell, also
add MSYS2's `ucrt64\bin` to `PATH` for libarchive's runtime DLLs.

To uninstall, run `rm -- "$HOME/.local/bin/arc"` for the default installation,
or remove `arc` from your custom installation directory; on Windows, remove `arc.exe`.

## CLI

Start with the everyday stuff:

```sh
arc pack project.zip ./src ./README.md # pack a few files or directories
arc unpack project.zip                 # unpack into a new ./project directory
arc unpack project.zip ./restored      # choose a destination

arc list project.zip   # see what’s inside
arc info project.zip   # see sizes and entry counts
arc verify project.zip # check content integrity where supported
```

The output suffix chooses the format when packing; unpacking detects it from
the contents. Existing files and links are never overwritten.

A few more tricks when you need them:

```sh
arc pack project.tar.gz ./project             # pack the directory’s contents
arc pack --keep-root project.tar.gz ./project # keep project/ itself
arc pack project.zip                          # use ./project as the source directory
arc pack --format tar backup ./src            # choose the format explicitly

# You can specify --password for any command; encrypted output requires ZIP.
arc pack --password secret private.zip ./docs
arc unpack --password secret private.zip ./private

# Just need a few things? Select a path and its subtree; repeat --entry as needed.
arc unpack --entry docs --entry README.md project.zip ./selected

arc pack -- -backup.zip ./src # use -- before paths beginning with '-'
arc --help                    # overview
arc --version                 # print the build version
arc unpack --help             # help for a specific command
```

Selected paths keep their archive layout. ZIP encryption leaves names and
metadata visible; command-line passwords can appear in shell history and process
arguments. See the [tutorial](docs/tutorial.md) for details and more examples.

`arc` ain’t trying to expose everything `libarchive` can do. If you need fine-grained
control, you’ll probably want to use `libarchive` directly.

## Use it from C

Include [arc.h](arc.h) to create, read and extract archives from your application.
Operations return an `ArcResult` with an error code and a short diagnostic.

Here’s a complete example that creates an archive with a text file inside:

```c
#include "arc.h"
#include <stdio.h>

int main(void) {
    Arc *writer = NULL;
    ArcResult result = arc_create("hello.tar.gz", &writer);
    if (result.code != ARC_OK) {
        fprintf(stderr, "%s\n", result.message);
        return 1;
    }

    result = arc_add_text(writer, "hello.txt", "Hello!\n");
    if (result.code != ARC_OK) {
        fprintf(stderr, "%s\n", result.message);
        ArcResult cleanup = arc_discard(&writer);
        if (cleanup.code != ARC_OK)
            fprintf(stderr, "%s\n", cleanup.message);
        return 1;
    }

    result = arc_close(&writer);
    if (result.code != ARC_OK)
        fprintf(stderr, "%s\n", result.message);
    return result.code != ARC_OK;
}
```

See the [tutorial](docs/tutorial.md#use-it-from-c) for more examples and the
header for full API contracts.

## Build

The project targets macOS, Linux and Windows through MSYS2 UCRT64. You’ll need
mise, Bash, a C11 compiler, libarchive development files and pkg-config.
On macOS, the build uses Homebrew’s libarchive.

```sh
mise trust
mise install
mise run build
mise run run -- --help
```

See [Building arc](docs/build.md) for build settings, Windows setup and linking
the library into your application.

Local builds report `arc dev`. To embed a version when building:

```sh
ARC_VERSION=0.1.0 mise run build
ARC_VERSION=0.1.0 mise run run -- --version
```

## Development

```sh
mise run test
mise run check
```

The [test guide](tests/README.md) covers focused runs, fixtures and sanitizers.
Filesystem operations live in [filekit](filekit/README.md), a separate C11
sublibrary with its own checks and no libarchive dependency.
