# Engineering agreements

- Treat explicit requirements and repository-local instructions as the source of truth.
- Separate discussion, planning and implementation; do not edit code for analysis-only requests.
- Inspect relevant code and documentation before changing behavior; prefer the smallest maintainable solution.
- Preserve project conventions and explain any new production dependency before adding it.
- Assume clients honor documented contracts. Do not add machinery for deliberate misuse or concurrent filesystem replacement without an explicit requirement.
- Preserve ordinary I/O handling, archive-data validation and cleanup diagnostics. Contract violations and unexpected Eiffel exceptions propagate.

## Eiffel layout

- Use tabs for indentation and one instruction per line.
- Separate semantic blocks inside routines with a blank line: initialization,
  resource acquisition, validation, processing, and cleanup should be easy to scan.
- Separate features and feature clauses with blank lines. Avoid uninterrupted walls
  of instructions; keep closely related instructions together without artificial spacing.
- Keep orchestration in Eiffel. Native primitives must check every fallible operation
  and preserve diagnostics before another native call can overwrite them.

## Current implementation

The micro implementation is now the only library. All Eiffel classes use ARC_*:

- src/core/arc_reader.e: single-use opening constructor, sequential metadata and block extraction.
- src/core/arc_writer.e: single-use opening constructor, immediate entries, exclusive output, commit/abort cleanup.
- src/core/arc_archive.e: high-level source registration, recursive pack, sequential entries and complete extraction.
- src/core/arc_formats.e: immutable string creation profiles.
- src/core/arc_error.e: immutable primary and separate cleanup errors.
- src/core/arc_native.e: direct libarchive bindings and command/result adapters.
- src/core/c/libarchive_wrapper.h: checked POSIX/Windows resource primitives and single-block transfers.
- src/cli/: CLI presentation, filename policy, converted commands and explicit stubs.

Keep the arc library target independent of CLI classes and vendor/cli. Preserve the
pinned CLI submodule revision and interface. Native code has POSIX and Windows/MSVC branches. Only Gobo/macOS is verified;
Windows implementation and Conan metadata are not a verified runtime support claim.

Reader entries are borrowed; writer entries and FILE sources are owned. Eiffel
owns output descriptors and incomplete-output removal decisions. Native allocation
failure is fatal, not an operation error. Snapshot diagnostics before another call
or release can overwrite them. Keep status, errno, message and cleanup errors separate.

Archive loops, name mapping, source classification and callbacks stay in Eiffel.
Scope native locale changes to a call and restore the caller's thread locale.
Use available libarchive primitives instead of duplicating format/link algorithms.
Never follow symbolic links in recursive traversal; pack ordinary hardlinked source
paths independently. ARC_ARCHIVE registers absolute paths and immutable names without source I/O;
pack reads sources and commits/closes, while close before pack cancels. No password API is present.

## Eiffel naming and constants

- Use `UPPER_SNAKE_CASE` for classes and `lower_snake_case` for ordinary features,
  arguments, locals, and assertion labels.
- Start constant feature names with an uppercase letter, for example `Open_error`
  and `End_status`; keep the remaining words lowercase and underscore-separated.
- Use `INTEGER = unique` for symbolic identifiers such as error categories and
  internal alternatives when their numeric values have no external meaning.
  Compare these identifiers symbolically; do not persist their generated numbers
  or rely on their ordering, contiguity, or stability between builds.
- Use explicit values for external ABI/protocol constants, sizes, and quantities.
  Native result codes must match the C primitives; they must not use `unique`.
- Preserve feature names required by inherited interfaces. In particular, CLI
  implementations keep inherited `name` and `description` even when implemented
  as fixed `STRING_32` metadata. Do not rename them solely for constant casing.
- Do not extract obvious identities or illustrative test operands into constants
  merely to eliminate literals.

## Eiffel source documentation

- Start every Eiffel class, including application and test classes, with `note`.
  Include `description`, `author`, and `email`, in that order. Established project
  author details are `samedit66` and `samedit66@yandex.ru`.
- Add copyright/license fields only when ownership and licensing are established.
  Do not maintain date or revision fields manually; Git records history.
- Give public features, selectively exported features, creation procedures, and
  redefinitions short English header comments where they are declared.
- Use an imperative sentence for commands, a noun phrase for value queries, and
  a question for Boolean queries. Explain purpose, ownership, availability, and
  material defaults; express exact obligations in contracts.
- Keep source comments and documentation in English and synchronized with behavior.
  Preserve contracts, command-query separation, void safety, and caller-owned input.

## Documentation and verification

Read README.md and docs/development.md before changing behavior; update their
examples and docs/migration.md when the API or CLI changes. The info and verify
commands are intentional nonzero placeholders; do not make them successful no-ops.

Rebuild affected executables before integration checks. Current probes are
 tests/demo.ecf, tests/checks.ecf, tests/package_checks.ecf and tests/archive_checks.ecf. Run
 tests/check_cli.py with rebuilt CLI executables, and packaging contracts with
 python3 -m unittest discover -s tests -p 'test_*.py'.

Use the compiler scope explicitly requested by the user; only Gobo/macOS is
currently verified. Keep EiffelStudio ECF selection without claiming an untested
port. Do not add Windows implementation without an explicit request.

Do not hand-edit generated build/, EIFGENs/ or .gobo/ files. Preserve semantic
blank lines. Check documentation links, run git diff --check, and report checks
actually run. Packaging changes require relocation verification when include paths
or package contents change.
