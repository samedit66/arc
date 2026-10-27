# Migration to the small ARC API

The former implementation is replaced, not retained behind compatibility aliases.
All library and probe classes use `ARC_*`; the binding class is `ARC_NATIVE`.
The header remains `libarchive_wrapper.h` with its existing function prefix.

## CLI conversion report

| Command | Status | Notes |
| --- | --- | --- |
| Root help/version | Converted | Uses the existing CLI submodule and a direct libarchive version binding. |
| `pack` | Converted | Eight string profiles, source defaults, keep-root, physical recursion and optional per-file byte progress. |
| `unpack` | Converted | Sibling destination, original-name validation, secure disk writer and optional byte progress. |
| `list` | Converted | Names from sequential reader snapshots, without extracting data. |
| `list --tree` | Converted | Retains copied names/types, duplicate names and literal components. |
| `info` | Converted | Header summary with format/filter/encryption snapshots, type counts and declared file bytes. |
| `verify` | Converted | Consume all entry bodies without extraction; report success after closing. |

`info` reads headers; `verify` reads all bodies and checks failures reported by
libarchive, including available format checksums. Neither extracts files. This
does not add content checksums to formats such as ordinary tar.

Password constructors register UTF-8 passphrases before opening. Nonempty CLI
passwords select them; empty passwords retain normal no-password behavior. ZIP
creation uses AES-256, and other profiles with a password fail before output
creation. Reading support depends on libarchive's format and encryption support.
There is no unencrypted fallback. Existing constructors remain available.

`ARC_ARCHIVE.inspect_remaining` now owns header aggregation; successful results
are immutable `ARC_ARCHIVE_INFO` objects exposed through `inspection`. Failure
publishes no partial summary. `verify_remaining` owns the body-consumption loop.
Both start at the current position, leave input open, and require explicit closing.
CLI commands delegate these operations and retain presentation and exit status.

## Deliberate API changes

- `ARC_READER.make_open_read (path)` and
  `ARC_WRITER.make_open_write (path, profile)` each serve one archive.
- Profiles are strings, including `tar.zst` for Zstandard. Use instance-free
  `{ARC_FORMATS}.Tar_zstd` and `{ARC_FORMATS}.is_supported_format (profile)`.
  `ARC_WRITER` and the pack command no longer inherit `ARC_FORMATS`; callers
  using inherited profile features must use `ARC_FORMATS` class calls.
- `ARC_PATHS` provides instance-free lexical path and entry-name checks. The
  nonempty/no-NUL rules, empty registration names and extraction policy are unchanged.
- `ARC_ARCHIVE` owns one reader or writer. Its `add_path` registers sources; `pack`
  traverses and writes them, then closes. `close` before `pack` cancels creation.
  Block-level control remains available on the reader/writer. The separate tree
  packer is removed; traversal belongs to the high-level archive.
- Expected errors use `has_failed` and immutable `last_error`; explicit abandonment
  additionally sets `is_aborted`. Unexpected Eiffel exceptions propagate after cleanup.
- `extract_current` uses an explicit output path. `extract_all` creates a new root
  and validates/maps original relative names. CLI suffix conventions stay in the CLI.
- File/directory/dangling symlinks are preserved physically. Ordinary hardlinked
  source paths are stored independently; hardlink extraction is unsupported.
- The old independent malformed/sparse-payload validation and native fault probes
  belonged to deleted session/header layouts. They are not claimed as preserved.
  New probes cover the replacement contracts, block transfers and cleanup.
- Automatic archive-name collision detection and full raw legacy filename metadata
  are not part of the small API. The CLI tree labels native regular-file types;
  the public `entry_is_hardlink` query now exposes this distinction for inspection.
- POSIX-only native code replaces the former Windows branches. Conan profiles and
  metadata support are retained, but they do not establish Windows API support.

## CLI organization

The command cluster now contains only commands and the application entry point.
`ARC_ENTRY_DISPLAY` and `ARC_LIST_TREE` moved to `src/cli/presentation`, selected only
by the CLI target. Display routines are instance-free. The former `ARC_CLI_PATHS`
is removed: suffix/profile inference is in `ARC_FORMATS`; default extraction
basename policy remains private to the unpack command. CLI arguments, output,
source mapping and archive behavior are unchanged. Pack prepares registrations
in its validation pass instead of inspecting sources a second time.

## Preserved infrastructure

The Conan recipe, lockfile, dependency profiles, native ECF generation, preparation,
package construction and pinned CLI submodule are retained. Packaging fixtures
now refer to the current source/header names. The package remains relocatable and
keeps the library target independent of CLI classes.

Before replacement, the working source/tests/docs and the current diff were copied
to `/tmp/arc-before-micro-migration`. The independent prototype remains in
`/tmp/arc-micro-project`. Neither snapshot is shipped in the library package.

## Verification scope

Only Gobo is used for this migration, as requested for the prototype. The dual
base-library ECF configuration remains, but old EiffelStudio executables are not
used as evidence. See [development](development.md) for fresh probe/CLI commands.

The migration verification completed successfully:

- Fresh Gobo `--gelint` builds of CLI and all three replacement probe targets.
- Eight creation profiles, Unicode names, directory/symlink traversal, progress,
  explicit abort, failed-output cleanup and unreadable-directory errno handling.
- Converted CLI integration checks, including terminal escaping, original-path
  validation, existing-output protection, information, verification and passwords.
- Eight packaging/metadata unit tests.
- A real native ZIP, relocation to a path containing spaces, and a compiled
  consumer running all eight profiles without checkout-specific dependencies.
- Eiffel feature-documentation audit, local Markdown links and `git diff --check`.

The known Gobo/macOS deprecated linker-symbol warnings remain; builds succeed.
