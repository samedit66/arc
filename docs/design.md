# Implementation notes

Arc keeps the archive workflow in Eiffel and uses small C primitives for native
resources and libarchive calls. There is no C Arc library hiding underneath this
port. Both implementations use libarchive, but they own different policy code.

The [public API guide](library.md) describes caller obligations. This document
explains who does the work and who cleans up afterwards.

## Module boundaries

| Location | Responsibility |
| --- | --- |
| `src/core/archive.e` | Absolute archive path and latest whole-operation result |
| `src/core/archive_constants.e` | Shared inherited symbolic identifiers and native status constants |
| `src/core/archive_formats.e` | Filename encoding and destination-basename rules shared with the CLI |
| `src/core/archive_error.e` | Immutable diagnostic and cleanup snapshots |
| `src/core/archive_text_encoding.e` | UTF-8 conversion and lossless diagnostic decoding |
| `src/core/pack/archive_writer.e` | Source registration and public packing operations |
| `src/core/pack/archive_pack_session.e` | One packing operation, traversal decisions, names, collisions and callbacks |
| `src/core/pack/archive_pack_names.e` | Relative archive-name validation and normalization |
| `src/core/unpack/archive_reader.e` | Destination naming, extraction and verification operations |
| `src/core/unpack/archive_unpack_session.e` | One extraction operation, path rebasing and transfer loop |
| `src/core/unpack/archive_entry_paths.e` | Extraction pathname and Windows component policy |
| `src/core/read/archive_read_session.e` | Deferred reader owner: opening, diagnostics, content-size validation and ordered cleanup |
| `src/core/read/archive_entry.e` | Immutable entry metadata independent of native handle lifetime |
| `src/core/read/archive_read_stream.e` | One sequential traversal and retained metadata |
| `src/core/read/archive_read_cursor.e` | `across` adapter sharing that traversal's position |
| `src/core/c/arc_native.h` | Native diagnostics, allocation and thread-local locale scopes |
| `src/core/c/arc_path.h` | Native pathname and entry-name helpers |
| `src/core/c/arc_read.h` | Reader configuration, headers, data blocks and cleanup |
| `src/core/c/arc_unpack.h` | Destination creation and extraction disk-writer primitives |
| `src/core/c/arc_pack.h` | Output creation, disk traversal, transfer and removal primitives |
| `src/cli/` | Command definitions, CLI-only summaries and terminal rendering |
| `vendor/cli/` | Vendored argument parsing, help and command dispatch |

The library target `arc` includes only `core`. Target `cli` extends it with the
CLI cluster and vendored parser. Importing the archive library doesn't import an
application entry point or argument parser.

```text
CLI commands -> public Eiffel API
ARCHIVE_WRITER -> packing session -> native packing primitives
ARCHIVE_READER -> extraction session -> native reader + disk writer
ARCHIVE_READER.new_stream / verify -> ARCHIVE_READ_STREAM -> native reader
ARCHIVE_UNPACK_SESSION and ARCHIVE_READ_STREAM inherit ARCHIVE_READ_SESSION
ARCHIVE_READ_CURSOR -> ARCHIVE_READ_STREAM
native primitives -> libarchive + operating-system APIs
```

Filename encoding inference lives in `ARCHIVE_FORMATS`, shared by the library and
CLI. The CLI adds its explicit-format conflict rule without duplicating suffix
mapping. Its operation diagnostics use `ARC_ENTRY_DISPLAY` to escape paths and
native messages before terminal output, including cleanup failures.

The shared `ARCHIVE` parent belongs to public readers and writers. Sessions own
resources for one operation and do not inherit its public path/result contract.
The deferred `ARCHIVE_READ_SESSION` is the single Eiffel implementation of reader
opening, captured passwords, header advancement, content-size validation and reader
cleanup. The stream and extraction session inherit it. Extraction specializes
context allocation/disposal and disk-writer cleanup; the common cleanup always
releases the disk writer before the reader and its locale. Advancement first skips any unread body, attributing failures to that entry.
Header diagnostics then clear the preceding name before attempting an unidentified
next header.

The read stream owns its own status; traversing one never updates a separate
`ARCHIVE_READER` object. `new_stream` and `new_stream_with_options` are construction
queries on the public reader and open no files.

`ARCHIVE_CONSTANTS` supplies inherited error, entry, encryption and encoding
identifiers, plus explicit native result codes. Existing qualified constants on
`ARCHIVE_ERROR`, `ARCHIVE_ENTRY`, `ARCHIVE_READ_STREAM` and `PACK_OPTIONS` remain
available through inheritance. Symbolic numbers are not a persisted ABI.

## Paths, strings and ownership

Archive objects capture an absolute `PATH` at creation without opening a file.
Writer registration captures an absolute source path and an immutable archive
name. Contents are read later, during packing. This makes later sequential cwd
changes independent of those registered paths; it doesn't create a filesystem
snapshot or authorize concurrent source replacement.

Filesystem operands retain their native representation: bytes on POSIX and wide
code units on Windows. Passwords and stored packing names use UTF-8.
`ARCHIVE_TEXT_ENCODING` decodes native diagnostic bytes as UTF-8 and escapes
invalid sequences without losing the original bytes. It doesn't guess arbitrary
legacy encodings. Entry snapshots retain both decoded text and raw name bytes.

Options capture immutable password values and callback references before the first
notification. Callers keep their mutable options objects; later edits affect a
later operation. Packing retains the writer's source registration list, which
cannot be changed while that writer is active. Sources survive every outcome.

## A packing operation

1. The writer marks itself active and clears its previous operation result.
2. A session captures sources and options, then publishes the initial notification.
3. Encoding is resolved and the native writer is configured before exclusive output
   creation. Existing objects are never overwritten.
4. The native disk reader exposes source entries. Eiffel maps their stored names,
   validates them, checks collisions and decides when to descend into directories.
5. Each regular file is transferred in native chunks. Eiffel counts only successful
   writes and publishes immutable progress snapshots.
6. The session closes source handles, finalizes/frees the archive writer and closes
   the output. Failure or an interrupted Eiffel call also triggers output removal.
7. The public writer publishes its result. Only success gets a final notification;
   the activity guard remains set through that call.

Libarchive performs filesystem enumeration through its disk reader. Eiffel owns
the traversal loop and descent decisions. It also owns the `HASH_TABLE` indexes
of entry names and required parents: duplicate entries, non-directory ancestors
and files colliding with an existing subtree fail before their header is written.
Archive names are normalized by removing redundant separators and `.` components;
absolute names, parent components, NUL, backslashes and drive prefixes are rejected.

Symbolic links are stored without following their targets. The native primitive
clears hardlink metadata, so selected ordinary-file paths store independent data.
That is the current [packing contract](library.md#create-an-archive-through-the-library),
even where TAR could represent hardlinks. Basic modes and timestamps come from
the disk entry; exact ownership, ACL and xattr preservation is not promised.

The output object is excluded from traversal by filesystem identity. Failure
cleanup removes only output created by this session, where removal succeeds.
There is no temporary-file publication protocol, rollback guarantee or crash-safe
commit. A cleanup failure is retained separately and may leave incomplete output.

## An extraction operation

The reader derives a sibling destination from the archive basename. The session
opens the input, creates a new destination, resolves that directory's native path,
and then reads one entry at a time. The public API currently offers no destination
override, merge mode, selection or root stripping.

Eiffel validates the original entry name before handing it to `PATH`, then rebases
it beneath the destination. Hardlink target paths are validated and rebased too;
symbolic-link text is preserved literally. Windows policy rejects reserved names,
invalid characters and trailing dots/spaces rather than relying on substitutions.

The native disk writer uses secure-symlink, no-parent-traversal and no-overwrite
options. An explicit filesystem check catches collisions that libarchive might
otherwise skip. Existing real directories may serve as parents inside this newly
created tree. Devices and FIFOs are unsupported.

Each transfer primitive reads one borrowed data block and writes it with its sparse
offset, inside one native locale scope. Bytes are counted only after the write
succeeds; unpack progress excludes sparse holes. Eiffel then finishes the entry
and advances. Hardlinks are handled in archive order and require an already
available target. There is no deferred-link graph like the original C unpacker.

Cleanup runs after success, ordinary failure and unexpected Eiffel exceptions.
Extracted objects remain after failure. Extraction is deliberately nontransactional.

## A sequential traversal

A read stream captures its inputs at creation. `open` may be called once and
positions it on the first header. `forth` asks libarchive for the next header,
skipping unread contents. `consume` reads and discards the current body. There is
currently no command returning content bytes to Eiffel.

The stream copies each entry's name, type, optional size and link target into an
immutable `ARCHIVE_ENTRY`. Format, outer-filter and encryption observations are
also copied, and survive close. Earlier entry snapshots remain valid after the
stream advances; the stream itself retains only the current entry.

Normal EOF and ordinary failures release native resources automatically. Explicit
`close` is idempotent. A caller that stops early or whose own processing raises
must close the stream, including from its rescue clause. `across` only advances
the cursor; it doesn't provide a resource-management scope or rewind the input.

`visit` scopes a complete header traversal around an Eiffel visitor and closes
resources if that visitor raises. The visitor receives immutable entries and must
not advance or close the same stream. Manual and `across` traversal keep their
explicit cleanup obligations and single shared position.

Verification uses a separate stream and calls `consume` for every entry. Both
consumption and extraction use the common Eiffel size validator. Ordinary entries
with a declared size must supply the exact declared payload length. Sparse entries
use the sum of sparse-map data lengths, excluding holes; extents must fit within
the declared logical size, and delivered blocks must be ordered and bounded.
Overflow checks precede arithmetic. Hardlinks and entries without a declared size
are excluded from this comparison. Metadata-only traversal skips this validation.
Listing and info therefore aren't substitutes for verification.

## Results and cleanup errors

`has_completed_operation` becomes true only when a whole operation returns
normally. `is_successful` also requires no saved error. `last_error` requires a
completed failure. Contract violations and unexpected Eiffel exceptions propagate;
they aren't translated into ordinary I/O failures.

A native primitive saves its diagnostic before another native call can overwrite
it. The Eiffel owner immediately copies a failure into `ARCHIVE_ERROR`, preserving
stage, Arc category, native status, system domain/code, raw message and decoded
message. Later cleanup failures produce a new immutable error snapshot with the
same primary failure and appended cleanup errors.

Native `ARCHIVE_WARN` and `ARCHIVE_RETRY` results are failures too. A source,
read or write failure doesn't become a success because cleanup happened to work.
Conversely, failed cleanup makes an otherwise completed operation fail.

Native allocation failures terminate with `abort`, including native out-of-memory
diagnostics. This is an explicit project policy, not a recoverable result. Eiffel
runtime exceptions retain their own behavior.

## Locales and callbacks

On POSIX, each native context owns a UTF-8 locale. It tries `C.UTF-8`, then
`en_US.UTF-8`. Native primitives select that locale for the calling thread and
restore the previous locale before returning. They never change the process-wide
locale. Windows uses wide filesystem calls where required.

Observers run in Eiffel after the native scope has ended. Their snapshots are
immutable and may be retained. Packing and extraction report start, successful
blocks, completed entries and final success; they do not report totals or phases.
Byte totals represent accepted content, not compressed output size or durable I/O.
No callback means no progress-snapshot allocation.

Observer exceptions propagate after cleanup. The final-success callback runs after
the result is published, so an exception there preserves `is_successful = True`.
Activity guards reset on exception paths and prevent reentry on the same public
object through final notification. None of this promises concurrent access to the
same instance or file, cancellation or background dispatch.

## Dependencies and distribution

Conan prepares static libarchive and compression dependencies; the generated
`native.ecf` references them relative to its own location. System crypto backends
are CommonCrypto on macOS and CNG on Windows; Linux uses static OpenSSL. The recipe
disables ACL/xattr restoration. The [build guide](build.md) records exact enabled
dependencies and ABI limits.

EiffelStudio uses EiffelBase, Gobo uses FreeELKS, selected in ECF. Shared Eiffel
sources stay within their supported common subset. The consumer ZIP includes
sources, CLI dependency, docs, native headers/archives, dependency notices and a
manifest. It doesn't include compilers, the Conan cache or generated executables.

See [Testing](testing.md) for the probes that check these boundaries and
[Parity audit](parity.md) for the parts still missing.
