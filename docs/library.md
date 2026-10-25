# Using the Eiffel library

This is an Eiffel library and an optional CLI backed by
libarchive. `ARCHIVE` exposes the instance-free integer query
`{ARCHIVE}.libarchive_version` (major * 1000000 + minor * 1000 + patch).
`ARCHIVE` is deferred and provides the common absolute archive path and latest
operation result. Create `ARCHIVE_READER` to read/extract an archive or
`ARCHIVE_WRITER` to register sources and create a new archive. Constructors open no files.
Reader and writer instances keep independent operation results; this does not
promise thread safety or coordinated access to the same file.

Library clients are expected to honor documented contracts. Guarantees for
deliberate misuse or concurrent filesystem replacement are outside the supported
contract; ordinary I/O failures, resource cleanup and archive-data validation
remain part of operation behavior.

The library creates archives and extracts them beside their source files.
The CLI implements pack, unpack, list, info and verify.

## Create an archive through the library

```eiffel
local
    writer: ARCHIVE_WRITER
    options: PACK_OPTIONS
do
    create writer.make_with_name ("project.zip")
    writer.add_path_string ("src")
    writer.add_path_string_as ("README.md", "docs/readme.txt")

    create options.make
    options.set_password ("secret")
    options.set_progress_callback (agent show_pack_progress)
    writer.pack_with_options (options)
end
```

`add_path (a_path: PATH)` and `add_path_as (a_path: PATH; an_entry_name:
READABLE_STRING_GENERAL)` are the native-path alternatives. Registration captures
an absolute source path and copies the archive name, without opening the output,
reading contents or traversing directories. Sources must exist at registration;
this precondition tests the object itself, including dangling symbolic links.
Existence does not guarantee later readability. Sources and archive names remain
registered after success or failure; `source_count` reports their number.
Registration preserves the previous operation result.

Without `_as`, each source retains its basename, independently of other sources.
A directory registered as `code` produces `code/...`. With `_as`, the supplied
nonempty relative archive path replaces the source basename.
`add_directory_contents (a_path: PATH)` and
`add_directory_contents_string (a_path: READABLE_STRING_GENERAL)` register the
contents of a real directory directly at the archive root, without storing the
source directory's own entry or metadata. An empty source directory contributes
no entries. These commands require `is_source_directory (a_path)`; files and
symbolic links, including links to directories, violate the precondition.
They capture one source path at registration and traverse it only during `pack`,
like the other registration commands. Empty names are invalid for `_as`.
Archive names use `/`, contain no NUL, backslash, drive prefix or `..` component;
redundant separators and `.`
components are normalized. The same rules apply to mapped names discovered during
directory traversal: an invalid name fails packing rather than being renamed or
skipped. Adding a filesystem root itself requires an explicit archive name;
adding its contents uses `add_directory_contents`.
A trailing `.` is ignored when selecting an implicit basename. Paths ending in
`..` require `_as` when adding the source itself.

Packing includes hidden files and empty directories and does not consult ignore
files. Symbolic links are stored without following their targets, including
external or dangling targets. Hardlink relationships are deliberately not
preserved: each selected ordinary-file path stores its own complete contents and
extracts as an independent file. Special objects, duplicate stored names,
file/directory conflicts and representations unsupported by the chosen format
are errors. Native source paths are preserved; stored entry names must be
representable as UTF-8. Basic file modes and timestamps are recorded where the
format supports them; exact ownership, ACL and xattr preservation is not promised.
Sources must remain unchanged during packing; there is no filesystem snapshot.
Traversal order within a directory and byte-for-byte reproducibility are not promised.

`pack` uses fresh default options: infer encoding from the output suffix, no
password and no callback. Both packing commands require at least one registered
source and an idle writer. Output creation is exclusive: existing files,
directories and links are never overwritten. Output encountered inside a selected
tree is excluded by filesystem identity. Success includes finalization and cleanup.
On failure, the session attempts to remove its incomplete output, preserving
cleanup errors beneath the primary error. A removal failure may leave output;
inspect the cleanup stages as well as the main diagnostic. Publication is not
atomic or crash-safe.

`PACK_OPTIONS` provides `set_encoding (format, compression)` and
`use_filename_encoding`. Explicit encoding ignores the output suffix entirely;
filename inference rejects unknown suffixes. Identifiers are Eiffel symbolic
constants inherited from `ARCHIVE_CONSTANTS`: `Zip`, `Tar`, `Seven_zip`, and
`None`, `Gzip`, `Bzip2`, `Xz`, `Zstd`. Clients may inherit that class too; existing
qualified access through `PACK_OPTIONS` remains available. Error categories,
entry kinds and encryption identifiers are inherited from the same class.
Their generated numeric values must not be persisted.
TAR uses restricted pax encoding. ZIP and 7z require outer compression `None`;
all five outer-compression choices are available for TAR in the prepared build.
Unsupported combinations fail before creating output, without substitution or
external-compressor fallback. Inferred suffixes, case-insensitively, are `.zip`,
`.7z`, `.tar`, `.tar.gz`/`.tgz`, `.tar.bz2`/`.tbz2`, `.tar.xz`/`.txz`, and
`.tar.zst`/`.tzst`. Compression levels and internal methods are not configurable.

`set_password` copies a nonempty, NUL-free Unicode string; `clear_password` removes
it. Passwords use UTF-8 and select ZIP AES-256. Other formats reject passwords
before creating output. Only nonempty ordinary-file contents are encrypted;
names, metadata, link targets, empty files and directories remain visible.
Passwords are excluded from Arc progress and diagnostic formatting; secure
memory erasure is not promised.

The callback has type `PROCEDURE [TUPLE [progress: PACK_PROGRESS]]`, configured
with `set_progress_callback` and removed with `clear_progress_callback`.
Options and sources are captured before the first notification; the callback
agent itself is retained by reference. No options object is retained after the
operation. Notifications are synchronous, outside native locale scopes: start,
successfully accepted data blocks, completed entries, and final success after
cleanup and result publication. With no callback, no snapshots are allocated.
`PACK_PROGRESS` is immutable and exposes `entry_name`, `entry_written_bytes`,
`written_bytes`, `completed_entries`, and `is_finished`. The name is absent at
start and final success. Byte counts measure uncompressed content accepted by
the archive writer, including represented sparse holes; they are neither output
file sizes nor disk-flush guarantees. Hardlink paths count independently.
There is no fixed block size, percentage, total-size prepass or cancellation.
Failures send no final-success event. Observer exceptions propagate after cleanup;
a final observer exception preserves the completed successful result.
`is_packing` remains true through final notification. Starting another pack or
registering a source on that writer during a callback violates a precondition.
Different instances have independent status; concurrent access to the same file
or instance requires caller coordination.

## Extract an archive through the library

```eiffel
local
    archive: ARCHIVE_READER
do
    create archive.make_with_name ("project.zip")

    archive.unpack
end
```

`make_with_path (a_path: PATH)` is also available. Creation resolves the source
path without opening it or collapsing symlink-sensitive `..` components; `path` and `unpack_path` expose the source and destination.
`project.zip` extracts into its sibling directory `project`. Recognized suffixes
are removed case-insensitively, including `.tar.gz`, `.tar.bz2`, `.tar.xz`,
`.tar.zst`, `.tar.lz4`, their listed shorthand forms, ZIP, TAR, 7z, RAR, CPIO,
XAR and ISO. Other names receive `.unpacked`. Format detection uses file contents.

Extraction creates a new destination and fails if it already exists. Archive
paths stay beneath it; a top-level directory in the archive is preserved. Regular
files, directories, symlinks and hardlinks are supported. Symlink targets are
preserved literally, including dangling or external targets; later entries cannot
be written through symlinks. Hardlink targets are rebased into the destination
and must already exist when the link is encountered. Devices and FIFOs are rejected.
Existing file entries are not silently overwritten or skipped.

Expected failures return normally and are available through the `ARCHIVE` interface:

```eiffel
archive.unpack

if archive.is_successful then
    -- Continue.
else
    -- archive.last_error is attached here.
    -- Inspect archive.last_error.code and archive.last_error.message.
end
```

Before any operation, `has_completed_operation` and `is_successful` are false and
`last_error` requires a completed, failed operation. Each operation resets the previous result. Success is
recorded only after resource cleanup; failures set `has_completed_operation` and
an immutable `ARCHIVE_ERROR`. Saved error objects remain unchanged after later
operations. Contract violations and unexpected Eiffel exceptions still propagate
after cleanup; the public operation remains incomplete in that case.

Errors expose a symbolic Arc category, stage, source and optional entry name, original
native status, system error domain/code, raw message bytes and decoded Unicode
text. Message bytes are interpreted as UTF-8, with invalid sequences escaped
losslessly; a native diagnostic in another encoding is not automatically detected
or transcoded. The original bytes remain available in `raw_message`. Cleanup
failures are separate immutable errors, accessible by count and index, beneath
the primary error.
Compare `error.code` with constants such as `{ARCHIVE_ERROR}.Open_error`.
Category identifiers use Eiffel `unique`; their numeric values are not a storage
or interoperability format.
Native WARN/RETRY results are failures too. Diagnostics are not truncated.
Native allocation failures terminate through `abort`; they are not recoverable
operation errors. The Eiffel runtime retains its own memory-failure behavior.

Already extracted objects remain after a failure; extraction is not transactional.
Timestamps and basic permissions subject to umask are restored; owners, ACLs and
extended attributes are not. Hardlinks must follow their targets.

The extraction loop, filename policy and byte accounting live in Eiffel. Each
native transfer reads and writes one block, preserving its sparse-file offset
without copying it into Eiffel storage. An optional progress observer is configured through `UNPACK_OPTIONS`.

On POSIX, individual native calls temporarily select a UTF-8 locale for the
calling thread and restore its prior locale before returning to Eiffel; the
process-global locale is never changed. `C.UTF-8` or `en_US.UTF-8` must be available. On Windows, wide filesystem
APIs are used. Arc rejects unrepresentable component names instead of accepting
libarchive's silent substitutions (reserved device names, invalid characters,
trailing dots/spaces). Native Windows extraction is not yet verified.

`ARCHIVE_TEXT_ENCODING` centralizes text-to-UTF-8 conversion and diagnostic
decoding. Filesystem paths retain `PATH`/`NATIVE_STRING` representations: native
bytes on POSIX and wide code units on Windows. Locale creation, scoped selection
and release stay in `arc_native.h`; each native reader context owns its locale,
retained through extraction writer cleanup.

## Stream archive entries without extraction

Native reader setup, header/block reading and reader cleanup live in `arc_read.h`.
Extraction embeds that reader context and adds a disk writer and destination in
`arc_unpack.h`; it uses the same locale and diagnostic storage. The deferred Eiffel
`ARCHIVE_READ_SESSION` owns common reader setup, header advancement, size validation,
diagnostics and cleanup; the stream and extraction session inherit it. Eiffel snapshots
each failure before the next cleanup action. Standalone streams own a heap reader
context; extraction owns an embedded one. Context disposal releases locale and
diagnostics after the handles have been explicitly closed and freed.

`ARCHIVE_READ_STREAM` owns a single sequential reading session, independently of
`ARCHIVE_READER` and its latest operation result. Creation opens nothing:

```eiffel
create options.make                         -- ARCHIVE_READ_OPTIONS
options.set_password ("secret")             -- Optional.
stream := reader.new_stream_with_options (options)
stream.open

across stream as entry loop
    -- entry is an immutable ARCHIVE_ENTRY snapshot.
    -- Process entry.pathname, entry.kind, entry.has_size / entry.size,
    -- and optional entry.link_target.
end

stream.close
if stream.has_failed then
    -- Inspect stream.last_error, including separate cleanup errors.
else
    check complete: stream.is_complete end
end
```

`reader.new_stream` uses no password; `reader.new_stream_with_options (options)`
captures its options. Both return fresh unopened streams without changing the
reader's operation result. Direct stream creation remains available.
`make (a_path: PATH)` uses no password. Creation captures an absolute path and an
immutable password; later changes to options do not affect the stream. `open`
may be called once and positions at the first header. An empty archive reaches
normal EOF immediately. `forth` skips unread contents and reads the next header;
`consume` fully reads and discards the current entry's remaining data, without
copying blocks into Eiffel storage or creating files. Neither operation follows
stored links. Consuming ordinary-file contents additionally checks declared sizes,
including sparse-map payload lengths and bounded block offsets. Hardlinks and
entries without a declared size are excluded from that comparison. The stream
does not enforce extraction path/type policy.

`item` requires `not after`. `after` means there is no current entry: unopened,
closed, EOF or failure. Distinguish normal completion using `is_complete`, which
requires EOF and successful cleanup. `has_failed` and its guarded `last_error`
query preserve expected I/O/format failures, native WARN/RETRY, and cleanup errors.
EOF and expected failures close resources automatically; explicit `close` is
idempotent and early closing never implies complete traversal. Unexpected Eiffel
exceptions during reading release resources and propagate.

**The caller must also close the stream in its routine's rescue clause** if entry
processing raises or the traversal ends early. `across` does not call `close`.
The cursor is only an adapter: `new_cursor` performs no I/O and does not rewind.
Use one traversal at a time, without interleaving adapters or manually advancing
the same stream during iteration. A later loop continues at the current position;
create another stream for a fresh or independent pass. These obligations describe
a single-pass stream, not a repeatable in-memory collection.

For a complete header traversal with library-owned cleanup, call `stream.visit`
on an unopened stream with an agent of type `PROCEDURE [TUPLE [entry: ARCHIVE_ENTRY]]`:

```eiffel
stream := reader.new_stream
stream.visit (agent process_entry)
if stream.has_failed then
    -- Inspect stream.last_error.
end
```

`visit` opens once and closes after normal completion, ordinary failure or a
visitor exception. Visitor exceptions propagate. The visitor receives retainable
immutable snapshots and must not advance or close that same stream. Like `across`,
this traversal skips bodies and does not replace `verify`.

Entry snapshots own immutable strings and remain valid after advancement/closing.
Names and targets are not normalized: duplicates, absolute paths, `..`, unusual
entry types and link targets remain inspectable. Types are symbolic constants
`Regular_file`, `Directory`, `Symbolic_link`, `Hard_link`, and `Other`.
`has_size` distinguishes missing size metadata from zero. `raw_pathname` and
`raw_link_target` contain copied UTF-8 bytes when available, otherwise native
name bytes; decoded names preserve invalid UTF-8 with ELKS escapes. No exact
original archive-header encoding is promised. Metadata availability depends on
format and the native reader. Arc retains only the current snapshot; libarchive
may internally buffer format-specific data.

The stream also retains archive metadata independently of native handle lifetime.
`format_name` is an optional immutable detected format name. `filter_count` and
`filter_name (index)` expose outer filters from the format towards the input,
excluding the input pseudo-filter. Zero filters means no outer compression;
ZIP's internal per-entry compression methods are not represented by this list.
Format and filter snapshots update after successful header reads, including EOF.
`encryption_status` updates after successful header reads and `consume`, using
symbolic constants `Encryption_detected`, `Encryption_none_detected`,
`Encryption_unknown`, and `Encryption_unsupported`. Unknown is the initial state;
unsupported means libarchive cannot detect encryption for the format. None
detected means no encrypted entry has been found so far, not proof of a valid
password or a complete integrity check. Metadata survives early/repeated close,
normal EOF and later failures; it describes the last successful observations.
Saved immutable format/filter strings remain valid after later reads.

`reader.verify` and `reader.verify_with_options (options: ARCHIVE_READ_OPTIONS)`
fully consume every entry through an independent stream and publish the common
`ARCHIVE` operation result after cleanup. Verification requires an idle reader;
it is also prohibited during extraction callbacks. `is_reading` covers unpack
and verify, while `is_unpacking` keeps its original meaning. Stream operations
have their own result and never update an associated reader.

Verification checks readable headers, decompression/decryption, integrity
checks implemented by the linked libarchive for that format, and Arc's independent
declared-size checks. Those size checks also apply during extraction. Sparse
payload comparisons exclude leading, interior and trailing holes; block extents
must stay within the declared logical size. A declared body with missing bytes
is an ordinary `Read_error`, even if libarchive reports normal entry EOF. It does not promise
checksums where none exist (ordinary TAR has no file-content checksum),
authenticity, malicious-content detection, successful extraction, or validation of
every physical input byte beyond the logical archive EOF recognized by libarchive.
A successful header-only traversal is not an integrity verification.

## Passwords and progress callbacks

`unpack` uses no password and sends no notifications. To configure one operation:

```eiffel
create options.make
options.set_password ("secret")
options.set_progress_callback (agent show_progress)

archive.unpack_with_options (options)
```

The observer has signature `show_progress (progress: UNPACK_PROGRESS)`; its agent
has type `PROCEDURE [TUPLE [progress: UNPACK_PROGRESS]]`. `set_password` requires a
nonempty string without NUL and copies its contents. `clear_password` removes it.
`clear_progress_callback` disables notifications. Both options are captured before
the first callback; changing the original options affects subsequent operations.
The callback agent itself is retained by reference. ARCHIVE_READER does not retain options.

Passwords are encoded as UTF-8 and registered with libarchive before opening the
file. A missing or wrong password is an ordinary operation error, possibly detected
only while reading data. Support depends on archive format and encryption scheme;
the probes cover traditional encrypted ZIP and ZIP AES-256 round trips, not every encryption mode.
Passwords are excluded from progress and Arc diagnostic formatting. Eiffel and
libarchive strings do not promise secure memory erasure.

Callbacks run synchronously on the extraction thread, outside native locale scopes.
They observe operation start, successful data-block writes, completed entries and
final success. No fixed block size or notification frequency is guaranteed. With
no callback, progress snapshots are not allocated. `UNPACK_PROGRESS` is immutable:

- `entry_name`: current entry, absent at start and final success;
- `entry_written_bytes`: current entry's successfully transferred data bytes;
- `written_bytes`: total successfully transferred data bytes, excluding sparse holes;
- `completed_entries`: entries whose native finish step succeeded;
- `is_finished`: extraction and cleanup successfully completed.

There is no percentage or total-size estimate, asynchronous dispatch, or cancellation.
The observer may retain snapshots; later notifications never mutate them. An observer
exception stops extraction, releases resources and propagates, leaving the public
operation incomplete. Already extracted objects remain. Final success is published
before its notification: if that callback raises, `is_successful` stays true.
`is_unpacking` remains true throughout notifications and is reset even on exceptions.
Starting another operation on the same ARCHIVE_READER during a callback violates a precondition.


See [implementation details](design.md) for resource ownership and the native boundary,
and the [parity audit](parity.md) for differences from the original C API.
