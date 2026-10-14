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
- Respects [NO_COLOR](https://no-color.org/) environment variable

## In a hurry?

```sh
arc pack project.zip ./src ./README.md # pack your files
arc unpack project.zip                 # unpack into ./project
```

Because “unpack” shouldn’t require remembering whether it’s `tar -xzf`, `-xjf`, or a keyboard smash.

## Installation

Installs the latest stable release. Before the first release, select `main` explicitly
with `--version main` (shell) or `-Version main` (PowerShell).

macOS / Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh
```

Windows (PowerShell, with MSYS2 UCRT64 installed):

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.ps1)))
```

For prerequisites, installation options, PATH setup and removal instructions,
see the [installation guide](docs/install.md).

## CLI

Start with the everyday stuff:

```sh
arc unpack project.zip ./restored      # choose a destination

arc list project.zip   # see what’s inside
arc info project.zip   # see sizes and entry counts
arc verify project.zip # check content integrity where supported
```

The output suffix chooses the format when packing; unpacking detects it from
the contents. Existing files and links are never overwritten.

See the [tutorial](docs/tutorial.md) for passwords, selective extraction,
root-directory options and more examples. Run `arc --help` or
`arc COMMAND --help` for command syntax.

`arc` ain’t trying to expose everything `libarchive` can do. If you need fine-grained
control, you’ll probably want to use `libarchive` directly.

## Use it from C

Include [arc.h](arc.h) to create, read and extract archives from C or C++.
With CMake, add arc as a subdirectory and link your target to `arc::arc`;
see [embedding instructions](docs/build.md#embedding-the-library) for source,
installed-package and pkg-config integration.
Operations return an `ArcResult` with an error code and a short diagnostic.

Here’s a complete example that creates an archive with a text file inside:

```c
#include "arc.h"
#include <stdio.h>

int main(void) {
    ArcWriter *writer = NULL;
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

With the [build prerequisites](docs/install.md#prerequisites) installed,
run from the repository root:

```sh
mise trust
mise install
mise run build
mise run run -- --help
```

See [Building arc](docs/build.md) for build settings, Windows setup and linking
the library into your application.

## Development

```sh
mise run test
mise run check
```

The [test guide](tests/README.md) covers focused runs, fixtures and sanitizers.
Filesystem operations live in [filekit](filekit/README.md), a separate C11
sublibrary with its own checks and no libarchive dependency.

The [design document](docs/design.md) describes the current architecture and contracts.

## License

Licensed under [Apache-2.0](LICENSE).
