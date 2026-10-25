# Engineering agreements

- Treat explicit requirements and repository-local instructions as the source of truth.
- Separate discussion, planning, and implementation; do not edit code for analysis-only requests.
- Inspect relevant code before proposing changes and prefer the smallest maintainable solution.
- Preserve project conventions and explain any new production dependency before adding it.
- Verify changes with relevant tests and both supported Eiffel compilers where available.
- Assume library clients intend to honor documented contracts. Do not add defensive
  machinery or guarantees for deliberate misuse, concurrent filesystem replacement,
  or other behavior outside the supported contract without an explicit requirement.
  Preserve ordinary I/O error handling, resource cleanup, and validation of archive data.

## Eiffel layout

- Use tabs for indentation and one instruction per line.
- Separate semantic blocks inside routines with a blank line: initialization,
  resource acquisition, validation, processing, and cleanup should be easy to scan.
- Separate features and feature clauses with blank lines. Avoid uninterrupted walls
  of instructions; keep closely related instructions together without artificial spacing.
- Keep orchestration in Eiffel. Native primitives must check every fallible operation
  and preserve diagnostics before another native call can overwrite them.

## Archive operation results and native boundary

- Expected I/O, format and extraction failures return through `last_error` and
  `is_successful`; contract violations and unexpected Eiffel exceptions propagate.
- Error objects are immutable snapshots. Keep native status, error domain, code,
  message bytes and cleanup errors separate; do not encode structure only in text.
- Native allocation failure is fatal (`abort`), not an operation error to translate
  into Eiffel. Do not add recovery branches for native out-of-memory conditions.
- Sessions own resource cleanup. Public readers and writers inherit the common path
  and operation-result contract from deferred ARCHIVE; sessions do not inherit it.
- Keep filename policy and extraction loops in Eiffel. Scope native locale changes
  to native calls and restore the caller's thread locale before returning.

## Repository guide

Arc is a void-safe Eiffel wrapper around libarchive with an optional CLI cluster.
Support EiffelStudio (EiffelBase) and Gobo (FreeELKS) within their shared ELKS
subset. Keep the `arc` library target independent of the CLI dependency.

- `src/core/`: shared archive path, operation status, error snapshots and text encoding.
- `src/core/pack/`: public writer, registered sources, packing session, options and progress snapshots.
- `src/core/read/`: shared deferred reader owner, sequential stream, entry metadata and read options.
- `src/core/unpack/`: public reader facade, entry pathname policy, extraction session, options and progress snapshots.
- `src/core/c/`: native diagnostics, resource primitives, and platform calls.
- `src/cli/`: application entry point and declarative CLI command definitions.
- `vendor/cli/`: upstream source snapshot; preserve its interface and provenance.
- `arc.ecf`: library target `arc` and executable target `cli`.
- `tests/`: packaging tests, Eiffel extraction probe, native fault probes, and CLI
  integration checks.
- `scripts/`: native dependency preparation and relocatable ZIP packaging.

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

- Read the relevant documentation before changing its described behavior. Update
  the README's library API, build, packaging, or CLI examples when they change.
- Follow the verification commands in README.md for the affected area. Rebuild
  executables after source changes before trusting integration-check results.
- For Eiffel library changes, compile the extraction probe with EiffelStudio and
  Gobo and run `tests/check_unpack.py` against both executables. For CLI changes,
  build both CLI targets and run `tests/check_cli.py` against both executables.
- For native changes, run `tests/check_unpack_native.py`; for packaging changes,
  run `python3 -m unittest discover -s tests -p 'test_*.py'` and verify relocation
  when include paths or package contents change.
- Preserve semantic blank lines when formatting. Do not hand-edit generated
  `build/`, `EIFGENs/`, or `.gobo/` contents as implementation changes.
- Check local documentation links and affected commands, run `git diff --check`,
  and report checks actually run and any unavailable compiler/platform checks.

## Unpack observers and passwords

- Preserve `unpack` defaults: no password and no observer. Capture operation options
  before the first callback; do not retain mutable options in ARCHIVE_READER.
- Invoke observers in Eiffel, outside native locale scopes. Publish only immutable
  progress snapshots and count bytes only after successful native writes.
- Reject reentry on the same ARCHIVE_READER throughout extraction and final notification.
  Reset the activity guard on all exception paths. Observer exceptions propagate;
  failure of the final notification must preserve the completed extraction result.
- Register passwords before opening the native reader. Do not include passwords
  in progress snapshots or diagnostic formatting; do not promise secure erasure.
- For observer/password changes, build `tests/options.ecf` with both compilers and
  run `tests/check_unpack_options.py` against both executables in addition to the
  ordinary extraction checks. Encrypted fixtures require the system `zip` command.

## Packing contract

- Keep source registration separate from I/O. Capture absolute paths and immutable
  archive names at registration; read contents during pack. Preserve registered
  sources after every outcome. Empty `_as` names add actual-directory contents at root.
- Preserve symbolic links without traversal. Store hardlinked ordinary-file paths
  independently; do not silently preserve hardlink relationships.
- Keep traversal, archive-name mapping, collision policy and callbacks in Eiffel.
  Packing sessions own native resources and remove incomplete output where possible.
- Capture options before the first callback. Follow the same observer-exception,
  final-success and reentry rules as unpack, on the individual writer.
- Verify packing changes with `tests/pack.ecf` on both compilers and
  `tests/check_pack.py` against both executables; use `tests/check_pack_native.py`
  for native writer changes. The independent reader is libarchive-based bsdtar.
