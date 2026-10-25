# CLI

The five commands are here. Some of the original flags are still on the way;
see the [parity audit](parity.md) before swapping binaries in a script.

Build instructions live in [Building and packaging](build.md#compile-and-run-the-cli).

The CLI accepts:

```text
arc --help
arc --version
arc COMMAND --help
arc pack [--format FORMAT] [--keep-root] [--password VALUE] ARCHIVE [SOURCE...]
arc unpack [--password VALUE] ARCHIVE
arc list [--tree] [--password VALUE] ARCHIVE
arc info [--password VALUE] ARCHIVE
arc verify [--password VALUE] ARCHIVE
```

With no arguments, Arc shows general help. `arc --help` (or `arc -h`) shows
the command overview; `arc --version` prints the application and libarchive
versions. Use `arc pack --help` (or `arc pack -h`) for command help.
Root flags cannot
be combined with an archive command; `arc --version --help` shows help.
Command help stops parsing and skips required arguments; earlier errors still fail.
Options accept `--name value` and `--name=value`, before or after positional
arguments. Values starting with `-` must use the inline form, such as
`--password=-secret`. `--` ends option recognition; use it before paths starting with `-`.
Options cannot repeat. Empty passwords mean no password. Diagnostics may
include input values. Invalid calls print to stderr and exit with status 2.

`arc unpack project.zip` extracts all entries into the adjacent `project` directory,
using the library's destination naming and extraction rules. Success is silent
and exits with status 0; extraction failures print diagnostics to stderr and exit
with status 1. An existing destination is not overwritten; a failed extraction may
leave partial output. `--password` supplies a password without an interactive prompt.
Progress output, destination overrides, `--strip-root`, and `--entry` are not yet
supported; unsupported arguments are rejected with status 2 before extraction.
`arc list project.zip` prints stored pathnames in archive order, one escaped name
per line, without extracting files. It streams output: a late failure can leave a
partial listing, with exit status 1. `arc list --tree project.zip` builds a pathname
tree in first-seen order and prints it only after successful traversal and cleanup.
Implied parent nodes are shown; each actual entry has a separate typed line with
its original pathname, preserving duplicates and file/directory conflicts. Literal
empty path components are labelled `<empty component>`; a directory's final slash
is represented by its directory entry. Both views escape backslashes, terminal
control characters and Unicode format controls. Listing does not verify all data.

`arc verify project.zip` reads and discards all decoded entry data. Success is
silent with status 0; detected reading, integrity, password or cleanup failures
produce stderr diagnostics and status 1. Its guarantees and limitations are those
of `ARCHIVE_READER.verify` above. `list` and `verify` accept `--password`; exposed
names can often be listed without decrypting file contents. A successful list
therefore does not establish password correctness. Neither command creates an
extraction directory. Invalid calls, including empty archive paths, exit with 2.

`arc info project.tar.gz` traverses headers and prints a summary only after
successful traversal and cleanup. It reports the absolute archive path, detected
format, outer filters, total entry count, counts of files/directories/symbolic
links/hardlinks/other entries, declared ordinary-file bytes, the number of
ordinary files with unknown size, and encryption detection state. Each stored
entry is counted, including duplicates; implied parent directories are not.
Sizes exclude links and other entry types and do not estimate extraction disk
usage. Missing sizes are excluded from the sum and counted separately; a negative
file size or a total exceeding NATURAL_64 is an operation error. Encryption is
printed as `yes`, `none detected`, `unknown`, or `unsupported` according to the
stream's detection state. Format names are libarchive descriptions and may vary
with format details or library versions.

For example, a TAR.GZ archive may produce:

```text
Archive: /work/project.tar.gz
Format: POSIX pax interchange format
Filters: gzip
Entries: 3
Files: 2
Directories: 1
Symbolic links: 0
Hard links: 0
Other: 0
Declared file bytes: 4096
Files with unknown size: 0
Encryption: unsupported
```

Info uses content detection, accepts `--password`, and escapes paths and native
text for terminal output. Like listing, it skips entry contents instead of
explicitly consuming them for verification. Some formats still require reading
and decompressing intervening data to reach later headers. A successful summary
does not establish integrity or password correctness. Failures print no summary,
report diagnostics on stderr and exit with 1; invalid calls exit with 2. No files
are extracted. Header-encrypted archives can require a password even for info.

`arc pack project.zip ./src ./README.md` creates a new archive. As in the
[original Arc CLI](https://github.com/samedit66/arc/blob/main/src/cli/main.c),
a single real directory contributes its contents at the archive root;
`--keep-root` retains that directory's basename. Multiple sources retain their
individual basenames. `--keep-root` requires exactly one real directory; a symlink
to a directory is stored as a link, without traversing its target.

Without sources, `arc pack docs.zip` reads `./docs`. The inferred source always
comes from the output basename in the current directory, so
`arc pack ./output/docs.tar.gz` also reads `./docs`. The inferred source must be
a real directory. Missing sources, collisions and unsupported filesystem objects
fail; hidden files and empty directories are included. Existing outputs are never
overwritten. Failure cleanup attempts to remove incomplete output.

The CLI and library share creation-suffix inference, case-insensitively: `.zip`,
`.7z`, `.tar`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tar.xz`, `.tar.zst`.
`--format zip|tar|7z` supplies a container for an unknown suffix; a known suffix
must agree and still selects outer compression. For example,
`arc pack --format tar project.tar.gz ./src` uses gzip, while
`arc pack --format tar project.bin ./src` creates uncompressed TAR.
The CLI exposes the three containers currently supported by the Eiffel writer;
the original's additional explicit formats are not implemented in this library.
The `.tbz2`, `.txz` and `.tzst` aliases select bzip2, XZ and Zstandard TAR,
respectively, in both the CLI and library.
An empty `--format` uses suffix inference, following the CLI's optional-string
convention. General parsing conventions described above still apply.

`--password` writes ZIP AES-256; names and metadata remain visible. Empty
passwords disable encryption. Non-ZIP passwords fail before creating output.
Success prints `Created ARCHIVE` with terminal-safe pathname escaping and exits
with 0. Invalid CLI input exits with 2; unknown explicit formats (regardless of
suffix), filesystem, archive and cleanup failures exit with 1. No terminal progress display is implemented.

```sh
arc pack project.zip ./src ./README.md
arc pack --keep-root project.tar.gz ./src
arc pack --format zip project.bin ./src
arc pack --password secret project.zip ./src
```
`arc --version` prints `arc dev` and the linked libarchive version number.

Parsing, help generation, dispatch and fatal diagnostics are provided by the
vendored [cli](../vendor/cli/README.md) library. Its source revision is recorded in
`vendor/cli/UPSTREAM.md`. Command definitions in `src/cli` declare their
parameters in `interface` and receive typed `CLI_COMMAND_INPUT` values in
`handle`. Optional strings default to empty strings; optional lists default to
empty lists. The entry point `ARC_CLI` registers commands with `CLI_APPLICATION`.
The `arc` library target contains only the archive API; the `cli` executable
target adds command definitions and the cli dependency.

The ZIP includes `vendor/cli`, so its CLI target builds without a sibling
checkout or fetching another dependency. To refresh cli, copy its `src`, `docs`,
`cli.ecf` and `README.md` into `vendor/cli` and update `UPSTREAM.md`.

Check compiled executables with:

```sh
python tests/check_cli.py ./arc ./EIFGENs/cli/F_code/arc
python tests/check_read_cli.py ./arc ./EIFGENs/cli/F_code/arc
```

Conan may use CMake or Autotools inside native dependency recipes. There is no
CMake or Meson project for this Eiffel application.

Operation diagnostics escape external paths and native messages, including cleanup
errors, so control characters cannot become terminal instructions. `verify` and
`unpack` independently validate declared ordinary-file payload sizes and sparse
extents; header-only `list` and `info` do not perform those body checks.
