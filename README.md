<div align="center">

# Ꭿrc

A human-centered 🥰 Eiffel library and CLI for everyday archive tasks.

</div>

`arc` has two goals:

- Provide a library that feels at home in Eiffel
- Replace `tar`, `zip` and other archive tools in my day-to-day work for good

I originally built [arc in C](https://github.com/samedit66/arc) as a backend for
another library, then gave it a project of its own. This is its Eiffel sibling.
Same idea, a few more contracts. We haven't caught up with everything yet.

## Highlights

- Usability first, features second 💅
- Powered by the battle-tested [libarchive](https://www.libarchive.org/) 🛡️
- Void-safe Eiffel, with EiffelStudio and Gobo support
- Pack, unpack, list, inspect and verify archives 📂
- Unicode paths, preserved symbolic links and optional passwords
- Native dependencies travel with the library package 😋

macOS ARM64 has been built and tested. Linux and Windows have dependency profiles
and platform code, but still need native verification. I’d like those runs before
calling the port ready on all three.

## In a hurry?

```sh
arc pack project.zip ./src ./README.md # pack your files
arc unpack project.zip                 # unpack into ./project
```

Because "unpack" shouldn’t require remembering whether it’s `tar -xzf`, `-xjf`,
or a keyboard smash.

## Installation

Build from this checkout with a configured Eiffel and C toolchain:

```sh
mise trust
mise install
mise run prepare
```

Then compile the CLI with one of the supported Eiffel compilers:

```sh
# Gobo: configure GOBO and PATH first.
GOBO_EIFFEL=ge gec --gelint --finalize --target=cli arc.ecf
./arc --help
```

```sh
# EiffelStudio: configure ISE_EIFFEL, ISE_LIBRARY, ISE_PLATFORM and PATH first.
GOBO_EIFFEL=ise ec -batch -config arc.ecf -target cli -finalize -c_compile
./EIFGENs/cli/F_code/arc --help
```

For prerequisites, Windows commands and native ABI requirements, see
[Building and packaging](docs/build.md). The original Arc installer installs the
C version; this checkout currently uses the source and library-package workflow.

## CLI

Start with the everyday stuff:

```sh
arc list project.zip         # see what’s inside
arc list --tree project.zip  # see the directory structure
arc info project.zip         # see metadata and entry counts
arc verify project.zip       # check content integrity where supported

arc pack --keep-root project.tar.gz ./src
arc pack --password secret project.zip ./src
arc unpack --password secret project.zip
```

The output suffix chooses the format when packing; unpacking detects it from
the contents. **Existing files and links are never overwritten**.

A single directory contributes its contents; `--keep-root` retains its name.
Without a source, `arc pack docs.zip` uses `./docs`. Unpacking creates a new
directory beside the archive: `/backups/docs.zip` goes into `/backups/docs`.

ZIP, TAR and 7z creation are available, including gzip, bzip2, XZ and Zstandard
wrappers for TAR. Read support comes from the linked libarchive build.

Destination overrides, selective extraction and `--strip-root` haven't made the
trip from the C version yet. The [parity audit](docs/parity.md) keeps the full
list honest. See the [CLI guide](docs/cli.md) or run `arc COMMAND --help` for
syntax and behavior.

`arc` ain’t trying to expose everything `libarchive` can do. There's still a fair
bit of the original Arc API to bring over, though.

## Use it from Eiffel

Add the library to your ECF:

```xml
<library name="arc" location="vendor/arc/arc.ecf"/>
```

Create an archive, extract it or verify its contents:

```eiffel
local
    writer: ARCHIVE_WRITER
    reader: ARCHIVE_READER
do
    create writer.make_with_name ("backup.tar.gz")
    writer.add_directory_contents_string ("backup")
    writer.pack

    if writer.is_successful then
        create reader.make_with_name ("backup.tar.gz")
        reader.verify
        if reader.is_successful then
            reader.unpack
        end
    end
end
```

Ordinary failures are available through `has_completed_operation`, `is_successful`
and `last_error`. A saved error is an immutable snapshot, including separate
cleanup errors. Contract violations and unexpected Eiffel exceptions propagate.

`PACK_OPTIONS` and `UNPACK_OPTIONS` accept passwords and synchronous progress
observers. `reader.new_stream` returns an unopened `ARCHIVE_READ_STREAM`;
its `visit` command scopes immutable entry visits with automatic cleanup.
`verify` and extraction independently validate declared file sizes, including sparse
payloads. Public identifiers are inherited from `ARCHIVE_CONSTANTS`. Reading entry
bytes into Eiffel and writing generated text or chunks are still missing.

See [Using the Eiffel library](docs/library.md) for contracts, examples, observer
lifetime and stream cleanup. The `arc` library target stays independent of the
optional CLI.

## Package it

```sh
mise run package
```

This prepares native dependencies and creates a target-specific ZIP under
`build/package`. Unpack its `arc` directory into your application's `vendor`
directory and reference `vendor/arc/arc.ecf`. Consumers need a compatible Eiffel
and C toolchain; they don't need Conan or mise.

See [the packaging guide](docs/build.md#package-and-consume-without-conan) for
profiles, archive contents and relocation requirements.

## Development

```sh
mise run test
python3 tests/check_cli.py ./arc ./EIFGENs/cli/F_code/arc
python3 tests/check_read_cli.py ./arc ./EIFGENs/cli/F_code/arc
```

`mise run test` checks packaging contracts. Compile both CLI executables before
running their integration checks. The [test guide](docs/testing.md) covers the
library probes, native fault injection and compiler matrix.

The [implementation notes](docs/design.md) describe resource ownership, path
policy and the Eiffel/C boundary. The [documentation index](docs/README.md)
links the rest.

The original project's benchmark numbers belong to its C implementation.
This port hasn't earned its own charts yet.
