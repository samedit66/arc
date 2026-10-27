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
- The existing wrapper now has Windows/MSVC branches for native paths, resources,
  iteration and thread locale. Windows runtime validation is still outstanding;
  Conan/profile tests alone do not establish Windows support.

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

## Windows implementation status

Windows code was added without new source files. Filesystem operations use wide
APIs; data transfer and archive policy retain the existing Eiffel orchestration.
`ARC_PATHS` now provides explicit physical source inspection in addition to
instance-free lexical predicates. This avoids relying on POSIX `lstat` semantics
from Eiffel runtime classes on Windows. The output is excluded by checked native
file identity against its owned descriptor. Source reparse points are never
followed; junctions and unsupported tags fail packing.

Extraction validation distinguishes POSIX names from Windows separators, devices,
streams and normalization restrictions. Incompatible names are rejected. System
errors retain positive errno or negative Win32 codes; cleanup failures remain
separate. POSIX behavior and the hardlink policy are retained. The Windows branch
has not been compiled or run on Windows; see development for the validation matrix.

## Build and installation modes

Source ECF now defaults to system libarchive (supported baseline 3.8.7), without
Python, Conan, CLI or `build/native`. Select `ARC_NATIVE_MODE=conan` explicitly for
prepared dependencies in a checkout. Prepared library ZIPs instead bind their
native ECF unconditionally and omit CLI source/targets/submodules. They are ready
to connect from a consumer ECF without mode overrides. Native ABIs still must match.

Application builds use finalized Gobo code with Boehm GC and inspect the resulting
executable. Windows Conan/runtime is static (`/MT`), macOS permits only Apple system
imports, and Linux distribution requires native musl and no interpreter/shared
imports. System Windows DLL clients require a common DLL CRT (`/MD`) because CRT
descriptors cross the boundary. System linkage is not a static distribution guarantee.
Linux package names now distinguish libc; older Linux deployments need preparation
again. Host and build profiles can be selected separately, and failed deployment
swaps restore the previous native directory.

The existing package script produces separate `-library.zip` and `-application.zip`
artifacts. Python is only required for preparation, packaging or test automation;
consumers of the prepared library need Eiffel/C compilers, and application users
need only the executable and permitted OS components. Windows/MSVC and Linux/musl
build paths remain unverified on native hosts. macOS relocation with Unicode uses
the generated Gobo C script explicitly due to the current compiler-driver limitation.

## CLI progress display

`pack --progress` and `unpack --progress` now render a single-row Unicode bar on
stderr using the pinned `progress_bar` submodule. Progress is per file; unknown
sizes use a spinner and byte counter. Initialize submodules before building.

## Portable CI builds

The CI matrix builds and runs the current probes and CLI checks with Gobo and
EiffelStudio on Ubuntu, macOS ARM64 and Windows/MSVC. ZIP creation explicitly
selects UTF-8 header names inside the scoped UTF-8 locale, avoiding Windows’
CP437 default. No caller API changes are required.

Windows output creation explicitly rejects existing reparse points before calling
the CRT, including dangling symlinks that `_wopen` can otherwise follow despite
exclusive creation. Rejected output paths never become owned cleanup targets.
