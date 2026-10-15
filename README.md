<div align="center">

# Ꭿrc

A human-centered 🥰 C11 library and CLI for everyday archive tasks.

</div>

`arc` has two goals:

- Provide a library that’s easy to use and easy to wrap (the initial goal)
- Replace `tar`, `zip` and other archive tools in my day-to-day work for good

I originally built it as a backend for another library, then gave it a project of its own.

**I don’t plan to maintain arc regularly**.
As long as it does what I need, I’m happy to leave it as it is. Bug fixes are always welcome.

## Highlights

- Usability first, features second 💅
- Powered by the battle-tested [libarchive](https://www.libarchive.org/) 🛡️
- Unicode and cross-platform support from the start 🌍
- Handles most everyday archive tasks &#128194;
- Easy to embed in your application 😋
- Respects the [NO_COLOR](https://no-color.org/) environment variable ✅

## In a hurry?

```sh
arc pack project.zip ./src ./README.md # pack your files
arc unpack project.zip                 # unpack into ./project
```

Because "unpack" shouldn’t require remembering whether it’s `tar -xzf`, `-xjf`, or a keyboard smash.

## Installation

The commands below install the latest stable release binary. For a development source build,
select `main` explicitly with `--version main` (shell) or `-Version main` (PowerShell).

macOS / Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh
```

Windows x86_64 (PowerShell; no MSYS2 required for release binaries):

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.ps1)))
```

For prerequisites, installation options, PATH setup and removal instructions,
see the [installation guide](docs/install.md).

## CLI

Start with the everyday stuff:

```sh
arc unpack project.zip ./restored  # choose a destination

arc list project.zip         # see what’s inside
arc list --tree project.zip  # see the directory structure
arc unpack --strip-root project.zip ./flat  # remove one common root directory
arc info project.zip         # see sizes and entry counts
arc verify project.zip       # check content integrity where supported
```

The output suffix chooses the format when packing; unpacking detects it from
the contents. **Existing files and links are never overwritten**.

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

Pack a directory, extract an archive or verify its contents with one call:

```c
#include "arc.h"
#include <stdio.h>

int main(void) {
    ArcResult result = arc_pack("backup.tar.gz", "./backup");
    if (!arc_ok(result)) {
        fprintf(stderr, "%s\n", result.message);
        if (*result.cleanup_message)
            fprintf(stderr, "Cleanup: %s\n", result.cleanup_message);
        return 1;
    }
    result = arc_unpack("backup.tar.gz", "./restored");
    if (!arc_ok(result)) {
        fprintf(stderr, "%s\n", result.message);
        return 1;
    }
    return !arc_ok(arc_verify("backup.tar.gz"));
}
```

`arc_unpack` requires a new destination. Use `arc_unpack_ex` for an existing
directory, selected entries, a password or root stripping. Existing files and
links are never overwritten. `ArcPackOptions`, `ArcUnpackOptions` and `ArcReadOptions` accept optional synchronous
progress observers for phases, entry counts and processed content bytes. The CLI
uses these counters on terminals without a background animation thread.

Writer/reader APIs remain available for generated
text, binary chunks and incremental reading.

See the [tutorial](docs/tutorial.md#use-it-from-c) for more examples and the
header for full API contracts.

## Benchmark

To my surprise, arc actually outruns some of the tools it wants to replace.
I was aiming for fewer flags, but I’ll take fewer milliseconds too.

On a local macOS benchmark using Rust’s source code, arc packed TAR archives
about 8× faster than the system tar, and extracted TAR and TAR.GZ archives
in about 25–32% less time than [ouch](https://github.com/ouch-org/ouch).

Ouch still won at packing and ZIP extraction. Fair enough — you can’t
win every race in your first pair of sneakers.

See the [benchmarks](docs/benchmark.md) for charts, archive sizes,
memory usage and reproducible runs.

## Build

With the [build prerequisites](docs/install.md#prerequisites) installed,
run from the repository root:

```sh
git clone https://github.com/samedit66/arc
cd arc
mise trust
mise install
mise run build
mise run test
mise run run -- --help
```

Mise runs CMake/Ninja; development outputs are in `build/dev`.
`mise run release` builds a native Release binary with pinned vcpkg dependencies;
`mise run dist` tests it and creates a distributable archive.

See [Building arc](docs/build.md) for build settings, Windows setup and linking
the library into your application.

## Development

```sh
mise run test
mise run check
```

The [test guide](tests/README.md) covers focused runs, fixtures and sanitizers.
Filesystem operations live in the internal [fs module](docs/design.md), a C11
module with no libarchive dependency. It’s tested as part of the common suite.

The [design document](docs/design.md) describes the current architecture and contracts.

## License

Licensed under [Apache-2.0](LICENSE).
