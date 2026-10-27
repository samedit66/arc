# Source map

The `arc` ECF target includes only `src/core` and either system libarchive or
explicitly selected prepared native dependencies. Prepared library packages bind
their own native ECF and contain no CLI target.
The `cli` target adds commands in `src/cli`, terminal presentation in
`src/cli/presentation`, and the pinned `vendor/cli` submodule. No legacy
`ARCHIVE_*`, session, options or cursor classes remain.

## Library

| File | Responsibility |
| --- | --- |
| [arc_formats.e](../src/core/arc_formats.e) | Instance-free immutable creation profiles, validation and filename suffix inference. |
| [arc_paths.e](../src/core/arc_paths.e) | Instance-free path/name policy and explicit physical source classification. |
| [arc_writer.e](../src/core/arc_writer.e) | One archive writer; ordinary files, directories and symlinks; exclusive output ownership, commit and abort cleanup. |
| [arc_reader.e](../src/core/arc_reader.e) | One sequential reader; metadata snapshots, password-based reading, body consumption and selected block extraction. |
| [arc_archive.e](../src/core/arc_archive.e) | Own one reader or writer; register sources, pack, extract, inspect remaining headers, verify remaining bodies and report progress. |
| [arc_archive_info.e](../src/core/arc_archive_info.e) | Immutable statistics and metadata snapshot from one remaining-header inspection. |
| [arc_error.e](../src/core/arc_error.e) | Immutable native status, errno, diagnostic bytes and separate cleanup failures. |
| [arc_native.e](../src/core/arc_native.e) | Direct libarchive bindings, native command adapters and immediate result queries. |
| [libarchive_wrapper.h](../src/core/c/libarchive_wrapper.h) | Checked POSIX/Windows source/output/directory primitives and single-block transfers; no custom C contexts or enums. |

Reader entries are borrowed from libarchive. The reader copies metadata before
publishing it and clones headers before changing extraction destinations. Writer
entries and FILE sources are owned until finalization. Eiffel owns output file
descriptors and supplies them to libarchive without transferring ownership.

Native additions exist only where libarchive already provides an operation or a
checked platform primitive is needed. The installed Gobo DIRECTORY primitives do
not preserve opendir/readdir/closedir failures, so native helpers perform one
checked directory step; the recursive loop stays in Eiffel. High-level destination
creation uses checked mkdir plus realpath to support secure extraction through
parent-directory aliases. Locale changes are scoped to native calls.

## CLI

| File | Responsibility |
| --- | --- |
| [arc_cli.e](../src/cli/arc_cli.e) | Command registration and execution. |
| [arc_root_command.e](../src/cli/arc_root_command.e) | Help and linked libarchive version. |
| [arc_pack_command.e](../src/cli/arc_pack_command.e) | Profile selection, source defaults, keep-root mapping and registered packing. |
| [arc_unpack_command.e](../src/cli/arc_unpack_command.e) | Destination basename selection and high-level extraction. |
| [arc_list_command.e](../src/cli/arc_list_command.e) | Flat/tree listing without extracting content. |
| [arc_list_tree.e](../src/cli/presentation/arc_list_tree.e) | Copied path/type labels, duplicates and literal pathname components. |
| [arc_entry_display.e](../src/cli/presentation/arc_entry_display.e) | UTF-8 terminal-safe rendering and error formatting. |
| [arc_info_command.e](../src/cli/arc_info_command.e) | Render the core inspection summary and encryption status. |
| [arc_verify_command.e](../src/cli/arc_verify_command.e) | Invoke core verification and report success after closing. |

## Infrastructure and tests

`conanfile.py`, `conan.lock`, `profiles/`, `mise.toml` and `scripts/` retain native
preparation, static application inspection and separate library/application packaging. Profile metadata includes Windows for future
use; Windows native implementation is pending runtime verification.

[tests/demo.ecf](../tests/demo.ecf), [tests/checks.ecf](../tests/checks.ecf) and
[tests/package_checks.ecf](../tests/package_checks.ecf) replace the former API probes.
[tests/archive_checks.ecf](../tests/archive_checks.ecf) and
[check_archive.py](../tests/check_archive.py) cover the high-level facade.
[check_cli.py](../tests/check_cli.py) tests archive commands, corruption detection and passwords;
[check_read_cli.py](../tests/check_read_cli.py) runs its read-only subset.
[test_packaging.py](../tests/test_packaging.py) retains metadata, ZIP and relocation
contracts. See [development](development.md) for commands.

The command cluster contains only commands and the application entry point.
Presentation helpers belong to the CLI target only. Terminal formatting is
instance-free; tree nodes retain state. Profile suffix inference belongs to
`ARC_FORMATS`, while default extraction directory naming stays in the unpack command.

`tests/check_archive.py --build` runs the complete native probe/CLI suite;
`--system` checks a source-only system consumer, and `--relocate ZIP` checks
a built library or application package after moving it.
