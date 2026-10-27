<div align="center">

# Ꭿrc

A human-centered 🥰 Eiffel library and CLI for everyday archive tasks.

</div>

Arc is a small void-safe Eiffel API over libarchive. Filesystem traversal,
progress callbacks, resource ownership and cleanup policy stay in Eiffel.
The library uses `ARC_*` classes and is independent of the optional CLI.

## Build

Conan, mise, the lockfile, profiles and relocatable native packaging are retained.
Initialize the pinned CLI submodule and prepare the native dependencies:

```sh
git submodule update --init --recursive
mise run prepare
GOBO_EIFFEL=ge gec --gelint --target=cli arc.ecf
./arc --help
```

The current wrapper is POSIX-only. Gobo on macOS ARM64 is verified; Windows
support is not implemented. EiffelStudio ECF selection is retained but this
migration is tested only with Gobo. See [development](docs/development.md).

## CLI

```sh
arc pack backup.tar.gz ./src
arc pack --keep-root backup.zip ./src
arc pack --format tar.xz backup.bin ./src
arc pack --progress backup.7z ./src
arc list backup.zip
arc list --tree backup.zip
arc unpack --progress backup.zip
```

A single real directory contributes its contents by default; `--keep-root`
retains its name. Multiple sources keep their basenames. Without an explicit
source, `arc pack docs.zip` reads `./docs`.

Creation profiles are `tar`, `tar.gz`, `tar.bz2`, `tar.xz`, `tar.zst`, `tar.lz4`,
`zip` and `7z`. The filename suffix selects the profile unless `--format` is used;
`.tgz`, `.tbz2`, `.txz` and `.tzst` aliases are accepted. RAR creation is excluded.
Reading detects formats through libarchive.
`--format` choices are case-sensitive and listed in command help; an empty value
keeps filename-based selection. Invalid choices are rejected before packing starts.

Unpacking creates a new sibling directory: `/backups/docs.zip` becomes
`/backups/docs`. Original entry names are validated before destination paths are
built. Symlinks are preserved without traversal; hardlink extraction is unsupported.
An existing output archive or extraction root is rejected, including dangling
symlinks. Failed packing attempts to remove its output; failed extraction can leave
already restored objects. Names and diagnostics are escaped for terminal output.

`info` reports the detected format, filters, entry counts, declared file sizes
and encryption detection status. It inspects headers without verifying payloads.
`verify` consumes every entry body without extraction and reports success only
after reading and closing succeed. It checks what libarchive can validate for the
format; ordinary tar has no checksum for file contents.

```sh
arc info backup.zip
arc verify backup.zip
arc pack --password secret protected.zip ./src
arc verify --password secret protected.zip
arc unpack --password secret protected.zip
```

A nonempty password enables AES-256 when creating ZIP. Other creation profiles
with a password fail before creating output; there is no unencrypted fallback.
Reading uses the encryption methods supported by libarchive and the native build.
A password may also be supplied for an unencrypted archive. Empty CLI passwords
mean no password. ZIP encryption protects file contents, not entry names.

`--progress` prints per-file successful byte transfers on stderr; it is not an
overall percentage. Success messages go to stdout after finalization and cleanup.

## Eiffel API

Add the library to a project which already selects its Eiffel base library:

```xml
<library name="arc" location="vendor/arc/arc.ecf"/>
```

Use `ARC_ARCHIVE` for registered packing and complete extraction operations:

```eiffel
create archive.make_open_write (output_path, "tar.gz")
if not archive.has_failed then
    archive.add_path (source_directory, "project")
    archive.add_path (readme_path, "README.txt")
    archive.pack
end
archive.close
```

The write constructor creates output exclusively. `add_path` only registers an
absolute source path and an immutable copy of its archive name; source contents
and directory trees are read during `pack`. An empty archive name includes a real
directory's contents without its root. Symlinks are stored without traversal.
`source_count` remains unchanged after packing, cancellation or failure.

`pack` writes all registered sources and closes the archive, including an empty
source set. On failure it attempts to remove incomplete output. `close` before
`pack` cancels creation without reading sources; `abort` also cancels. Neither
operation removes successfully committed output. An archive serves one operation
and cannot be reopened or repacked after closing.

```eiffel
create archive.make_open_read (input_path)
from until archive.after loop
    process_metadata (archive.entry_name, archive.entry_type)
    -- Optionally call archive.extract_current (selected_destination).
    archive.forth
end
archive.close
```

`process_metadata` is application code. Names and symbolic-link targets are
immutable snapshots; sizes and native entry types are available as on `ARC_READER`.
Advancing skips unread content. `extract_current (destination_path)` completes the
current entry without advancing. It uses the explicit destination supplied by the
caller; archive-name mapping is the caller's responsibility for selected entries.

Alternatively, call `archive.extract_all (new_destination_directory)` after
opening for reading. It creates a new root, validates original relative entry
names and restores the remaining entries through EOF. The input remains open;
call `close` afterwards. `extraction_directory` is the physical destination path
when root creation succeeded. Existing roots, absolute entry names, parent
components and writes through restored symlinks are rejected. Failed extraction
can leave already restored objects. A second pass requires a new reader object.

Use `set_progress (callback)` before packing or extraction, or pass Void to disable
notifications. The default is no callback. Its synchronous agent receives
`[name: IMMUTABLE_STRING_32; written, total: NATURAL_64]` after successful blocks;
`total` is zero when unknown. Callbacks must not mutate or reenter the archive.
Observer exceptions propagate after releasing resources and clearing `is_busy`;
packing also discards incomplete output.

For explicit block control, use `ARC_WRITER` (`begin_file`, `write_block`,
`finish_entry`, `begin_directory`, `begin_symbolic_link`) or `ARC_READER`
(`begin_extraction`, `extract_block`, `finish_extraction`). These low-level classes
own their resources independently. String profiles and profile validation are instance-free:
`{ARC_FORMATS}.Zip` and `{ARC_FORMATS}.is_supported_format (profile)`.
`{ARC_FORMATS}.profile_for_name (filename)` and `output_suffix (filename)`
provide filename inference without I/O. `ARC_WRITER` no longer inherits
`ARC_FORMATS`; use the class calls directly.
`{ARC_PATHS}.is_valid_path (path)` and
`{ARC_PATHS}.is_valid_entry_name (name)` check nonempty values without NUL,
without filesystem I/O. These checks do not enforce extraction pathname policy;
`add_path` still allows an empty stored name for directory contents.

Password constructors are `make_open_read_with_password (path, password)` and
`make_open_write_with_password (path, profile, password)` on `ARC_ARCHIVE` and the
corresponding reader/writer. Existing constructors keep their behavior. Passwords
are nonempty Unicode strings without NUL, encoded as UTF-8 at the native boundary;
caller strings are not retained or modified. Native passphrase storage is released
with its archive handle.

`consume_current` on the reader or facade reads the remaining current body to EOF
without advancing, writing files or changing current-entry metadata. Use it before
`forth` for a complete verification pass starting at the first entry.

`ARC_ARCHIVE.inspect_remaining` walks headers from the current entry through EOF
without verifying payloads. On success, `inspection` holds an immutable
`ARC_ARCHIVE_INFO` with entry/type counts, declared ordinary-file bytes, unknown
size count, format, filters and encryption status. Before the first inspection or
when that inspection fails, `inspection` is Void; partial results are not published.
Each inspection replaces the result, and retained older summaries remain stable.

`ARC_ARCHIVE.verify_remaining` consumes all remaining entry bodies without
extraction. Both commands leave input open, report expected errors through
`has_failed` / `last_error`, and process only the remaining entries. Called
immediately after opening, they cover the whole archive; on normal EOF they
process an empty remainder. Call `close` and check errors before reporting success.
Neither command rewinds or sends extraction progress notifications.

Reader and facade expose `format_name`, `filter_count`, `filter_name (index)` and
`encryption_status`. Summary snapshots survive closing; facade summary queries
require `is_readable`. Filter indexes are one-based and include the terminal
`none` filter reported by libarchive. `ARC_READER` defines `Encryption_detected`,
`Encryption_none_detected`, `Encryption_unknown` and `Encryption_unsupported`.
Detection describes inspected data so far; "none detected" is not a general proof
that encryption is absent. `entry_is_hardlink` only exposes metadata: hardlink
packing and extraction policies are unchanged.

Check `has_failed` after commands and after `close`; retain `last_error` for
immutable primary and cleanup diagnostics. Use `close` or `abort` in application
rescue clauses and on early exit. `is_open_read` and `is_open_write` describe
available modes; contracts restrict mode-specific commands. Unexpected Eiffel
exceptions propagate. No archive class provides a GC finalizer for native resources.

## Packaging and verification

```sh
mise run package
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tests/check_cli.py ./arc
```

Packages retain the prepared static dependencies, their licenses, the CLI submodule
sources and relocatable ECF. Consumers need compatible Eiffel and C compilers;
Conan and mise are not required to consume the ZIP.

See the [source map](docs/code.md), [development guide](docs/development.md), and
[migration report](docs/migration.md) for API details, probe commands and limits.
