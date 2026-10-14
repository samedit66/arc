# Arc Library Design

Status: Current architecture and contracts  
Language: C11  
Targets: macOS, Linux, Windows (MSYS2 UCRT64)

This document describes the current design. [arc.h](../arc.h) is authoritative
for public declarations and individual operation contracts. For practical
instructions, see [installation](install.md), [building and embedding](build.md)
and the [tutorial](tutorial.md).

## 1. Purpose

Arc is a small, human-oriented C API for creating, inspecting and extracting archives. Its vocabulary describes user actions: create an archive, add a file or text, write a chunk, close, inspect and unpack. Callers do not need to understand backend handles, entry objects, format codes or initialization sequences.

The implementation uses [libarchive](https://github.com/libarchive/libarchive). The interface is defined by Arc's own semantics, not by a renamed copy of the libarchive API. Another backend may implement the same interface while advertising a different set of supported operations and formats.

All failures controlled by Arc are values. Its own code does not use exceptions, `longjmp`, process termination or assertions as an error-handling mechanism. Returned backend failures are converted to ArcResult. Fatal termination inside libarchive itself, including some out-of-memory paths, is not intercepted; this is an explicitly accepted backend limitation.

## 2. Scope and file boundaries

Arc has one public header and private archive modules. The independently buildable filekit sublibrary has its own public `filekit/filekit.h` and error values. Internal headers are not part of either library’s caller-facing API.

| File | Responsibility |
|---|---|
| `arc.h` | Public types, declarations and contracts; standard C headers only. |
| `arc.c` | Public operation wrappers, locale scopes, output publication and cleanup sequencing. |
| `main.c` | CLI parsing, source/root naming policy, output selection and diagnostics. |
| `internal/arc_internal.h` | Private operation entry points; no writer representation. |
| `internal/writer.c` | Writer state and ownership, chunks, filesystem traversal through libarchive, selected hard-link identity, close/discard. |
| `internal/reader.c` | Sequential reader ownership/state, listing, statistics, file size and full-content verification. |
| `internal/unpack.c` | Selection and extraction state, directories, deferred links and metadata sequencing. |
| `internal/libarchive_adapter.{h,c}` | Format/filter tables, configuration, reader lifecycle, entry conversion and backend error mapping. |
| `internal/archive_paths.{h,c}` | Safe relative archive names, shared file/directory conflict rules and a typed AVL path index. |
| `filekit/filekit.h` | Independent public UTF-8 filesystem API and result/identity/info values. |
| `filekit/file_system_{unix,windows}.c` | Native filesystem implementations with validated public operands. |
| `filekit/windows_text.c` | Windows Unicode conversion and native path rules. |
| `filekit/support.c` | Independent errors, strings and UTF-8 validation. |
| `internal/platform_text.h` | Private locale scope, name comparison and platform path/text operations. |
| `internal/platform_text_{unix,windows}.c` | Thread-local conversion contexts, archive name rules and bridges to filekit Unicode conversion. |
| `internal/support.{h,c}` | Archive result construction, filekit result translation, cleanup diagnostics, strings and a bridge to filekit UTF-8 validation. |

Dependency direction:

```text
main.c -> arc.h
arc.c -> private operations + platform text + support
writer/reader/unpack -> libarchive adapter + filekit + shared path rules
platform text/support -> private filekit text helpers (in-repository integration)
filekit -> C runtime + native platform APIs
```

Only the writer, reader, unpack and libarchive adapter modules use libarchive. Archive modules call filekit directly using its own info, identity and link-kind types. Filekit never accepts backend entries or extraction records. The backend adapter exposes private `ArchiveMetadata` values; unpack owns permission/time restoration policy. `ar_from_filekit` in support translates errors without an operation wrapper layer. `struct ArcReader` and its current-entry text are owned exclusively by `reader.c`; listing delegates to this sequential reader. `struct ArcWriter` and the writer index are owned exclusively by `writer.c`; `unpack.c` owns a distinct extraction index, retained entry clones and deferred-link state. Name-conflict semantics are shared through a typed AVL path index containing copied names, entry types and stable owner record numbers. Writer and extraction records remain separately owned; no generic untyped collection is introduced.

The build selects one filekit filesystem implementation and one arc platform-text implementation at compile time. Filekit builds to its own static library without libarchive or pkg-config; arc embeds its platform objects in libarc.a, which the CLI links. Its public errors are explicitly translated to the unchanged ArcResult contract. The build uses an explicit object list; unselected or stale objects are not linked. There is no runtime platform dispatch. The private text header contains platform-specific locale representation; native branches needed to call libarchive's wide APIs remain in its adapter.

The mise tasks own build, formatting and black-box CLI checks; `.github/workflows/ci.yml` invokes them on macOS, Linux and Windows. Generated test fixtures and binaries remain outside the source tree. Pytest tests use an isolated uv project in `tests/`. The filekit sublibrary retains independent API checks and its own mise tasks; arc tests also compile a focused public C API harness against the built library. No third-party production dependency, formatter configuration, dynamic loader or runtime dispatch is introduced.

## 3. Human-oriented interface principles

1. Operations express caller intent and have ordinary action names.
2. Common actions have helpers: adding a filesystem file or text does not require manual entry construction.
3. Recursive path addition handles files, directories and links; chunk-based writing remains available for caller-generated data.
4. Required operands are explicit. Format choices belong to a small options value.
5. Ownership, state transitions, filesystem effects and failures are documented.
6. Backend types, error codes and configuration strings do not leak through the interface.
7. Capability checks support user interfaces; they are not prerequisites for calling an operation.
8. Requested behavior is never silently replaced by a different format or compression method.

## 4. Backend boundary and extension

There is one backend per build in the current implementation. Backend replacement is a source-level change to the private archive implementation modules; the adapter is shared integration code, not a runtime backend interface. A replacement implementation must preserve lifecycle, error-value, path-safety and data semantics, but may support fewer or more archive configurations.

No runtime backend selector, registration mechanism, function-pointer dispatch table or dynamic backend loading is introduced. Such mechanisms require a future requirement to use multiple backends simultaneously.

The public API does not promise that all backends support every format. Unsupported requests return `ARC_UNSUPPORTED`. Capability information refers to the current implementation and the dependencies actually available in that build.

Inside the libarchive implementation, one authoritative translation table maps Arc identifiers to backend configuration. Creation and capability queries share this mapping. Registration, RAR5 setup and probe cleanup results are checked. Returned allocation and other technical probe failures do not silently remove capability bits; failed queries leave the output mask unchanged. Format registration warnings are errors. Filter registration warnings documented as external-program fallback mean the native filter is unavailable, and the probe is freed without use. ENOMEM has precedence over diagnostic-text classification. Library-wide format enablement is not evidence that every individual feature is available.

## 5. Formats and compression

Archive container format and outer compression are separate concepts.

```c
/** Output archive configuration. */
typedef struct {
    /** Non-NULL, case-sensitive Arc format identifier. */
    const char *format;

    /** Non-NULL outer compression identifier; "none" means no wrapper. */
    const char *compression;

    /** NULL for no encryption; nonempty UTF-8 selects ZIP AES-256. */
    const char *password;
} ArcOptions;
```

Named container identifiers are `tar`, `zip`, `7z`, `cpio`, `ar`, `iso9660`, `xar`, `warc`, `rar`, `cab` and `lha`. Named outer-compression identifiers are `none`, `gzip`, `bzip2`, `xz` and `zstd`.

The named set is extensible. Identifiers belong to Arc; arbitrary backend option strings are not accepted. A new identifier requires a documented mapping and capability behavior.

Examples:

| Output | Format | Outer compression |
|---|---|---|
| `.tar` | `tar` | `none` |
| `.tar.gz`, `.tgz` | `tar` | `gzip` |
| `.tar.bz2` | `tar` | `bzip2` |
| `.tar.xz` | `tar` | `xz` |
| `.tar.zst` | `tar` | `zstd` |
| `.zip` | `zip` | `none` |
| `.7z` | `7z` | `none` |

ZIP and 7z may compress entries internally even when outer compression is `none`. Internal compression uses backend defaults; tuning methods or levels is outside the initial API.

Libarchive provides read/write support for TAR/PAX, ZIP, 7z, CPIO, AR, ISO9660, XAR and WARC, with format-specific limits. RAR/RAR5, CAB and LHA/LZH are read-only. Available compression depends on the linked build. See [libarchive's format summary](https://github.com/libarchive/libarchive/wiki/LibarchiveFormats).

WARC requires libarchive 3.8.9 or newer: older readers misreport entry offsets or skip already-read content. XAR requires libarchive 3.8.2 or newer because older readers report archive offsets at entry EOF. With an older backend, Arc returns `ARC_UNSUPPORTED` for the affected format's capabilities, creation and reading before exposing entries. Other formats remain available.

Reading uses content detection and enables available archive readers rather than applying a filename-extension whitelist. Additional detected formats can be readable without becoming named creation options. `raw`, `mtree` and `shar` are not promised as ordinary multi-file extraction formats: their semantics differ from the primary abstraction.

7z requires outer compression `none`: its reader needs seekable archive data.
Configuration queries and creation reject compressed 7z combinations with
`UNSUPPORTED`; content-detected reads reject those wrappers too. ZIP with outer
compression uses a streaming reader: list, verify and unpack remain available,
but capabilities omit `CAN_INSPECT` because declared sizes may be unavailable.
AR creation accepts only flat entry names and rejects directory components
rather than silently replacing paths with basenames.

Not every format/compression combination is usable. Capability queries and attempted operations decide support. TAR uses a PAX-compatible representation in the initial backend; selecting historical TAR or CPIO variants is outside the initial interface.

## 6. Result values and public types

```c
/** Opaque archive writer. */
typedef struct ArcWriter ArcWriter;

/** Backend-independent operation outcome. */
typedef enum {
    ARC_OK = 0,
    ARC_INVALID_ARGUMENT,
    ARC_INVALID_STATE,
    ARC_INVALID_PATH,
    ARC_ALREADY_EXISTS,
    ARC_UNSUPPORTED,
    ARC_INVALID_ARCHIVE,
    ARC_SIZE_MISMATCH,
    ARC_OVERFLOW,
    ARC_OUT_OF_MEMORY,
    ARC_IO_ERROR,
    ARC_NOT_FOUND,
    ARC_SELECTION_ERROR,
    ARC_PASSWORD_REQUIRED,
    ARC_PASSWORD_REJECTED
} ArcCode;

/** Self-contained operation result returned by value. */
typedef struct {
    ArcCode code;
    char message[256];
} ArcResult;

/** Statistics for a completed archive file. */
typedef struct {
    uint64_t archive_bytes;
    /** Sum of declared sizes of ordinary data-bearing file entries. */
    uint64_t content_bytes;
    /** Ordinary data-bearing entries; hard-link aliases are excluded. */
    uint64_t file_count;
    uint64_t directory_count;
    uint64_t symbolic_link_count;
    uint64_t hard_link_count;
} ArcInfo;

/** Operations available for an explicitly requested configuration. */
enum {
    ARC_CAN_CREATE  = 1u << 0,
    ARC_CAN_INSPECT = 1u << 1,
    ARC_CAN_UNPACK  = 1u << 2,
    ARC_CAN_LIST = 1u << 3,
    ARC_CAN_WRITE_DIRECTORIES = 1u << 4,
    ARC_CAN_WRITE_SYMBOLIC_LINKS = 1u << 5,
    ARC_CAN_WRITE_HARD_LINKS = 1u << 6,
    ARC_CAN_READ_DIRECTORIES = 1u << 7,
    ARC_CAN_READ_SYMBOLIC_LINKS = 1u << 8,
    ARC_CAN_READ_HARD_LINKS = 1u << 9,
    ARC_CAN_VERIFY = 1u << 10
};

/** Entry type exposed without backend-specific constants. */
typedef enum {
    ARC_ENTRY_FILE,
    ARC_ENTRY_DIRECTORY,
    ARC_ENTRY_SYMBOLIC_LINK,
    ARC_ENTRY_HARD_LINK,
    ARC_ENTRY_OTHER
} ArcEntryType;

/** Read-only borrowed metadata, valid only during a visitor call. */
typedef struct {
    const char *path;
    ArcEntryType type;
    uint64_t size;
    int size_known;
    /** NULL for non-links or when the target cannot be exposed. */
    const char *link_target;
} ArcEntry;

/** Root creation policy; neither mode permits file overwrites. */
typedef enum {
    ARC_DESTINATION_NEW,
    ARC_DESTINATION_MERGE
} ArcDestinationMode;

typedef struct {
    ArcDestinationMode destination_mode;
    const char *password;
} ArcUnpackOptions;

/** Return OK to continue, or an error value to stop and propagate it. */
typedef ArcResult (*ArcVisitor)(
    const ArcEntry *entry,
    void *context);
```

`ARC_OK` has an empty message. Errors have a null-terminated diagnostic, which may be truncated. Callers make decisions using the code, not the diagnostic text. Result messages are owned by the returned value and remain valid after later calls. No heap allocation is needed to store an error.

Invalid pointers that are not NULL cannot be safely validated in C; pointer validity remains a caller obligation. All explicitly nullable arguments and NULL rejection rules are documented below.

Error classification:

| Code | Meaning |
|---|---|
| `INVALID_ARGUMENT` | NULL required argument, invalid option structure or otherwise malformed request. |
| `INVALID_STATE` | Operation is not allowed in the writer's current state. |
| `INVALID_PATH` | Unsafe entry name or filesystem path invalid on the target platform. |
| `ALREADY_EXISTS` | A destination or extraction collision would overwrite an existing object. |
| `UNSUPPORTED` | Unavailable format, compression, archive feature or entry type. |
| `INVALID_ARCHIVE` | Recognizable archive data is malformed or truncated. |
| `SIZE_MISMATCH` | Entry data does not match its declared size. |
| `OVERFLOW` | Size or count cannot be represented by the API or backend. |
| `OUT_OF_MEMORY` | An allocation failed. |
| `NOT_FOUND` | At least one extraction selector matched no archive record. |
| `SELECTION_ERROR` | A selected hard link has a missing or excluded target. |
| `PASSWORD_REQUIRED` | Encrypted content needs a password. |
| `PASSWORD_REJECTED` | The backend rejects the supplied password. |
| `IO_ERROR` | Filesystem, permission, input/output or backend finalization failure. |

An unrecognized input is classified as unsupported; malformed recognized input is classified as invalid where the backend provides sufficient information. Exact diagnostics are not a stable API. Operation errors retain their primary code while bounded secondary cleanup diagnostics may be appended. Successful operations report cleanup failure when one occurs. Sequential reader operations and close preserve the exact first stream failure even if context restoration also fails; listing with a visitor failure likewise returns that original result unchanged; later cleanup/conversion-context failures are intentionally subordinate in those two contracts. Backend warnings that would compromise the requested result must not be treated as success. No implicit external-program fallback is permitted.

Capabilities describe format/backend representation, not current privileges or destination-filesystem support. A readable link can be listed even when the host cannot create it. Permission failures return IO_ERROR; unavailable representation or filesystem functionality returns UNSUPPORTED where distinguishable. Unknown native failures return IO_ERROR with a diagnostic.

Statistics count archive records rather than unique filesystem objects. `content_bytes` includes only ordinary data-bearing file entries; symbolic-link target bytes and hard-link aliases are excluded. It does not describe allocated disk usage or the size obtained by following every extracted name. CPIO variants can place data on different members of a hard-link group: the backend must normalize one data-bearing member and its aliases, or return UNSUPPORTED. Repeated paths remain separate records for listing/statistics but are errors when writing or extracting.

## 7. Common function contracts

- All fallible operations return `ArcResult`; internal fallible helpers use explicit results as well.
- All public filesystem and entry paths are UTF-8 strings.
- Input strings and option structures are borrowed for the call. The implementation copies information needed after return.
- Writers are exclusively owned. The same writer must not be accessed concurrently or copied as an independently owned resource.
- There is no process-global mutable error buffer or library working-directory change. A writer output is bound to an owned absolute UTF-8 path at create time. Sequential caller working-directory changes between writer calls are supported; relative source operands refer to the directory at their own call. Concurrent process working-directory changes during any operation are unsupported.
- Output values are modified only on success, except writer/reader creation and close/discard operations, whose pointer behavior is explicitly defined.
- Reading requires the source archive to remain unchanged for the duration of the operation.
- Filesystem parents must exist and be trusted. The API is not a sandbox against another process concurrently replacing path components.
- Size conversions and sums are checked. Silent truncation, integer wraparound and automatic zero padding are prohibited.
- Entry-name uniqueness and file/directory prefix conflicts are enforced across the writer's lifetime. Implicit parent directories may be reused; repeated explicit additions fail.
- Callback metadata is read-only and borrowed for that call. Visitors must not mutate the input archive or reenter the ongoing listing operation.
- UTF-8 input does not require caller locale initialization. Backend operations temporarily use a thread-local UTF-8 conversion context and restore it afterward; the library does not deliberately change the process-wide locale. Unavailable context and restoration failure are returned errors. In the latter exceptional case, the thread's conversion context may remain changed. Visitor callbacks run within this operation context.

## 8. Complete public function surface

The header includes `<stddef.h>` and `<stdint.h>` in addition to the public types above.

```c
/**
 * Query available operations for a format and outer compression.
 *
 * options: Non-NULL requested configuration.
 * operations: Non-NULL output bitmask; unchanged on failure.
 *
 * Creates no files. Unavailable configurations return UNSUPPORTED.
 * Success describes ordinary supported variants in the current build,
 * not every encryption method, entry type or format-specific feature.
 * Entry-feature bits describe read/write representation, not filesystem
 * permissions or guaranteed extraction on the current host.
 */
ArcResult arc_get_capabilities(
    const ArcOptions *options,
    unsigned int *operations);

/** Infer format/compression from the longest recognized output suffix:
 * .tar, .tar.gz, .tgz, .tar.bz2, .tar.xz, .tar.zst, .zip, .7z.
 * ASCII case-insensitive. Unknown suffix: INVALID_ARGUMENT, no file created.
 * Same ownership, non-overwrite and cleanup rules as arc_create_ex.
 */
ArcResult arc_create(const char *path, ArcWriter **out);

/**
 * Create a new archive writer with explicit configuration; suffix is ignored.
 *
 * path: Non-NULL UTF-8 output path; parent must exist.
 * options: Non-NULL requested format and outer compression.
 * out: Non-NULL output pointer; receives a writer or NULL on failure.
 *
 * Never overwrites an existing object. Checks configuration support before
 * creating the file. On success, ownership requires close or discard.
 * A failure after file creation may leave an incomplete output file if
 * cleanup fails; the diagnostic must identify this condition.
 */
ArcResult arc_create_ex(
    const char *path,
    const ArcOptions *options,
    ArcWriter **out);

/**
 * Add an ordinary filesystem file, copying its data in chunks.
 *
 * writer: Active writer with no unfinished entry.
 * source_path: Non-NULL UTF-8 source path; file must remain unchanged.
 * entry_path: Non-NULL safe relative UTF-8 archive path using '/'.
 *
 * Rejects symbolic links and special source files. Records modification time and
 * ordinary permission bits where representable. Does not add directories
 * recursively. Duplicate entry names are rejected. Hard-link relationships
 * among selected ordinary files are preserved across filesystem additions;
 * unsupported preservation returns UNSUPPORTED.
 */
ArcResult arc_add_file(
    ArcWriter *writer,
    const char *source_path,
    const char *entry_path);

/**
 * Add a filesystem file, directory tree or symbolic link.
 *
 * writer: Active writer with no unfinished entry.
 * source_path: Non-NULL UTF-8 source path; tree must remain unchanged.
 * entry_path: Non-NULL relative archive path. An empty string is allowed
 *             only for an actual directory and adds its contents at root.
 *
 * Recurses through actual directories, including hidden files and empty
 * directories. Does not follow symbolic links or consult ignore files.
 * Preserves links where representable, without silently copying targets.
 * Excludes the output object from traversal by filesystem identity.
 * An explicitly selected source equal to the output object is an error.
 * Duplicate paths, special objects and unsupported representations fail.
 */
ArcResult arc_add_path(
    ArcWriter *writer,
    const char *source_path,
    const char *entry_path);

/**
 * Add a null-terminated text string as an ordinary file.
 *
 * writer: Active writer with no unfinished entry.
 * entry_path: Non-NULL safe relative UTF-8 archive path.
 * text: Non-NULL string; bytes before its terminator are stored unchanged.
 *
 * Empty text creates an empty file. Uses permissions 0644 and the current
 * modification time where representable. Embedded null bytes require the
 * chunk API. Duplicate entry names are rejected.
 */
ArcResult arc_add_text(
    ArcWriter *writer,
    const char *entry_path,
    const char *text);

/**
 * Begin an ordinary file entry for chunk-based writing.
 *
 * writer: Active writer with no unfinished entry.
 * entry_path: Non-NULL safe relative UTF-8 archive path.
 * size: Exact content size; must be representable by the backend.
 *
 * Uses permissions 0644 and the current modification time where supported.
 * Success requires a later end_file, including for zero-byte entries.
 * Duplicate entry names are rejected.
 */
ArcResult arc_begin_file(
    ArcWriter *writer,
    const char *entry_path,
    uint64_t size);

/**
 * Write the next chunk of the open entry.
 *
 * writer: Writer with an entry opened by begin_file.
 * data: Borrowed bytes; NULL is allowed only when size is zero.
 * size: Number of bytes to write; must not exceed the remaining size.
 *
 * Success means the complete chunk was accepted. No data pointer is retained.
 * Failure may follow partial backend output and invalidates the writer.
 */
ArcResult arc_write_chunk(
    ArcWriter *writer,
    const void *data,
    size_t size);

/**
 * Complete the currently open entry.
 *
 * writer: Writer with an unfinished entry.
 *
 * Requires exactly the declared byte count. A short entry returns
 * SIZE_MISMATCH. Success makes the writer ready for another entry.
 */
ArcResult arc_end_file(ArcWriter *writer);

/**
 * Finalize an archive and release its writer, even on failure.
 *
 * writer: Non-NULL address of the owned pointer; set to NULL on return.
 *         A NULL pointer value is a successful no-op.
 *
 * Reports unfinished entries, prior failures and finalization errors.
 * A failed close may leave an incomplete file; no writer remains for retry.
 */
ArcResult arc_close(ArcWriter **writer);

/**
 * Release an unfinished writer and remove its output file.
 *
 * writer: Non-NULL address of the owned pointer; set to NULL on return.
 *         A NULL pointer value is a successful no-op.
 *
 * Returns an error if resource cleanup or removal fails. No writer remains
 * for retry. Callers must report cleanup failures without hiding the original
 * operation failure. The output path must not be concurrently replaced.
 */
ArcResult arc_discard(ArcWriter **writer);

/**
 * Extract an archive without overwriting existing files or links.
 *
 * archive_path: Non-NULL UTF-8 input path; input must remain unchanged.
 * destination: Non-NULL UTF-8 directory path; parent must exist.
 * options: Non-NULL root policy: NEW requires atomic directory creation;
 *          MERGE creates a missing directory or reuses an actual directory.
 *
 * Detects format from content. Supports ordinary files, directories and
 * permitted symbolic/hard links. Rejects
 * unsafe paths, special files, file collisions and traversal through links.
 * Creates deferred links after data extraction and validates their targets.
 * Restores supported metadata on new objects. Does not apply archive metadata
 * to existing directories or restore ownership. Creating children may change
 * existing directory timestamps through normal filesystem behavior.
 * Failure may leave partial output.
 */
ArcResult arc_unpack(
    const char *archive_path,
    const char *destination,
    const ArcUnpackOptions *options);

/**
 * Visit archive entries without extracting them to disk.
 *
 * archive_path: Non-NULL UTF-8 input path; input must remain unchanged.
 * visitor: Non-NULL callback receiving borrowed read-only entry metadata.
 * context: Caller-owned callback context; may be NULL.
 *
 * Calls visitor in archive enumeration order. A non-OK visitor result stops
 * enumeration and is returned unchanged; resources are still released.
 * Logical entry types/targets are normalized where the backend can expose
 * them. Unknown sizes use size_known == 0; size must then be ignored.
 * Missing/unrepresentable path text fails rather than emitting a mangled name.
 * Listing does not validate extraction safety or require host link privileges.
 */
ArcResult arc_list(
    const char *archive_path,
    ArcVisitor visitor,
    void *context);

/**
 * Read the size of an ordinary file without validating archive contents.
 *
 * archive_path: Non-NULL UTF-8 path of an existing ordinary file.
 * bytes: Non-NULL output size; unchanged on failure.
 *
 * Use after successful close to obtain the completed archive's disk size.
 */
ArcResult arc_get_size(
    const char *archive_path,
    uint64_t *bytes);

/**
 * Inspect an archive sequentially without extracting files to disk.
 *
 * archive_path: Non-NULL UTF-8 input path; input must remain unchanged.
 * info: Non-NULL statistics output; unchanged on failure.
 *
 * Counts ordinary data-bearing files, directories and both link types,
 * including repeated names. Sums ordinary data-bearing file sizes only.
 * Unknown ordinary-file sizes, unsupported special types and overflow
 * return errors. Does not promise full payload verification
 * or that the archive can safely be extracted on this filesystem.
 */
ArcResult arc_inspect(
    const char *archive_path,
    ArcInfo *info);
```

### 8.1 Additive selection and verification API

Selection uses `ArcExtractOptions`; verification can read content without extracting it:

```c
typedef struct {
    ArcDestinationMode destination_mode;
    const char *const *paths;
    size_t path_count;
    const char *password;
} ArcExtractOptions;

ArcResult arc_unpack_ex(const char *archive_path,
    const char *destination, const ArcExtractOptions *options);
ArcResult arc_verify(const char *archive_path);
```

A positive count requires valid selectors; zero selects everything. Selectors are copied and validated before destination creation. Candidate matching strips leading `./` and trailing `/` only; output validation follows selection and rejects unsafe selected paths. No wildcard syntax or host-dependent case folding is used for selection. Both operations use the existing locale-scoped result/cleanup model. `ARC_CAN_VERIFY` denotes format/filter readability, not universal corruption detection.

### 8.2 Sequential entry reader

```c
typedef struct ArcReader ArcReader;
typedef struct {
    const char *password;
} ArcReadOptions;
ArcResult arc_reader_open_ex(const char *path, const ArcReadOptions *options,
    ArcReader **out);
ArcResult arc_reader_open(const char *path, ArcReader **out);
ArcResult arc_reader_next(ArcReader *reader, ArcEntry *entry, int *end);
ArcResult arc_reader_read(ArcReader *reader, void *buffer,
    size_t capacity, size_t *received);
ArcResult arc_reader_skip(ArcReader *reader);
ArcResult arc_reader_close(ArcReader **reader);
```

The reader has exclusive ownership; its input must remain unchanged until close. Opening publishes an owned handle or NULL. First next returns the first archive record; subsequent next automatically skips unread old content. A valid next invocation ends the prior metadata lifetime even if it fails. Returned strings belong to the reader and remain valid through reads/skips until next/close; callers copy strings to retain them. Listing uses the same reader internally, while its visitor lifetime remains call-scoped.

Next returns OK with end=1 at archive EOF and zeroes the entry. Repeated next then returns the same EOF without backend I/O. Read/skip require a current record and return INVALID_STATE before the first next or after archive EOF. A successful read with received=0 ends the current record, not the archive. Repeated reads at record EOF return zero. Positive capacity and non-NULL buffer/count outputs are required; short reads are allowed and do not establish EOF.

Read exposes only ordinary-file body bytes, including zero-filled sparse gaps. Directories, links and other non-file records return zero; link targets stay in metadata and are never followed. Repeated successful skip is a no-op, retains metadata and causes subsequent read to return zero. Next/skip can perform I/O/decompression and fail. Skips and early close do not promise content verification.

Invalid arguments/state leave reader and outputs unchanged. Backend, integrity, size/overflow and character-conversion context failures poison the reader and retain the first failure. Subsequent well-formed stream operations return INVALID_STATE; close remains available. Failed next/read leave outputs unchanged. A failed read may modify buffer bytes; callers ignore the buffer for that call. Prior successful chunks cannot be retracted if later integrity checks fail. Actual ordinary-file EOF validates known sizes and backend-provided integrity signals. Unsupported encryption and ambiguous CPIO link groups return errors; password failures may occur during content reading.

Close consumes the owned pointer and sets it to NULL even on error; a NULL value is a successful no-op, a NULL pointer address is invalid. It does not drain unread records. The first retained stream failure outranks cleanup errors. The wrapper restores the caller's locale after each operation and publishes metadata/count outputs only after restoration succeeds. Failure to enter/restore that context poisons an active reader; close still releases it when entering the context fails. Backend buffering/seek/temp storage remain possible; constant total memory is not promised.

## 9. Writer state and ownership

| State | Allowed operations |
|---|---|
| Ready | Add file, add path/tree, add text, begin file, close, discard |
| Entry open | Write chunk, end file, close, discard |
| Failed | Close, discard |
| Released | No operations through the old pointer |

All unsuccessful writer-content operations invalidate the writer, including validation and size failures. This conservative rule avoids separate recoverability categories and uncertainty about partial backend effects. The first failure is retained for a later close. An invalid-state call must not overwrite that original failure. The public writer type documentation states the complete state rule. Close retains an earlier writer failure even if entering the conversion context itself fails; it still consumes and releases the writer.

Closing with an unfinished entry is an error even if its byte count is complete: the caller must explicitly end the entry. Close and discard always consume a valid owned handle. Close with a NULL pointer address is invalid; close with a non-NULL address holding NULL succeeds. Discard follows the same rule.

Exclusive destination creation must be atomic; an existence check followed by an ordinary truncating open is insufficient. Failed creation attempts best-effort cleanup of any output they created. Successful finalization is required before the output is treated as a usable archive. Crash durability, atomic publication and transactional replacement are not promised.

The writer owns the entry-name index and filesystem-identity map across all additions. Normalized names must reject aliases and non-directory prefix conflicts; comparisons follow target filesystem behavior where knowable, with extraction still detecting actual destination collisions. The identity map detects selected hard-link groups and prevents archiving the output object through any alias. Metadata tracking grows with entries; constant-memory operation is not promised. Recursive addition is not transactional: on failure the whole writer becomes Failed. Directory-content addition does not store the omitted root directory's metadata. Exact-name lookup uses an AVL path index; conflict checks examine component ancestors and an ordered descendant range. Filesystem identity uses a separate writer-owned AVL tree over stable record numbers, with native identity ordering. Exact name/identity lookup needs O(log N) comparisons and path conflicts need O(D log N) comparisons for D components; string length and Windows conversion add comparison cost. Both AVL insert/free traversals have logarithmically bounded call depth. Record-array growth does not invalidate indexed record numbers.

## 10. Paths and filesystem safety

Filesystem paths may be absolute or relative. Archive entry paths must be relative and use `/` between components. Entry validation applies both when creating archives and when extracting them.

Reject empty stored file names, embedded unsafe components, parent traversal in entry names, absolute entry paths and special entry types during extraction. Normalize benign leading `./` components and directory markers without permitting traversal or conflating files and directories. The empty add_path mapping is a directory-content instruction, not an empty stored entry. On Windows, reject drive/UNC entry paths, alternate-stream syntax, reserved device names (also with extensions), forbidden characters and invalid trailing dots or spaces. See [Microsoft's naming rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).

Validation follows the compiled target's rules rather than imposing Windows restrictions on POSIX targets. Actual component/path-length limits and representability are checked through the selected system APIs. No universal 260-character limit is assumed. Invalid names are errors; they are not renamed or sanitized silently.

Windows arguments and filesystem paths use Unicode-aware operations with conversion at the UTF-8 API boundary. Existing non-UTF-8 POSIX names are outside the initial path contract.

Extraction NEW mode atomically creates its root and fails if any object already exists there. MERGE mode creates a missing root or reuses an actual directory. Neither mode overwrites files or links, including collisions caused by case folding or Unicode normalization. Existing actual directories may be reused; archive metadata is not applied to them. Normal filesystem timestamp changes caused by creating children are permitted, and pre-extraction timestamps are not restored. A file/directory type conflict is an error. Explicit directory records may describe existing directories, but repeated conflicting records and repeated non-directory names fail.

Check destination components without following links. Never replace an existing symlink, junction or other redirection to make extraction succeed. Link entries cannot serve as parents for other archive entries. Protect the input archive itself from extraction collisions when extracting into its containing directory.

Do not rely solely on libarchive's NO_OVERWRITE option: its Windows disk writer can skip an existing object and return success. Arc must classify collisions as ALREADY_EXISTS and preserve exclusive creation; a skipped required entry is not successful extraction. See the [Windows disk-writer implementation](https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive_write_disk_windows.c).

Output anchoring does not resolve future object replacement or parent renaming. POSIX anchoring joins the creation working directory with the original relative suffix without realpath or lexical collapse through symlinks. Windows reuses the existing Unicode long-path conversion. The caller must use a trusted parent directory and prevent concurrent path replacement. Protection against hostile concurrent filesystem mutation would require a stronger directory-handle-based contract and is outside this design.

### 10.1 Symbolic links

Packing preserves the link object and target text without following its target, including a source argument that is itself a link. An empty root mapping is therefore invalid for a link to a directory. Directory traversal uses physical link handling; see [libarchive disk-reader modes](https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive_read_disk.3).

Packing may preserve absolute or externally directed targets, but safe extraction rejects them. Such archives are inspectable, but are not guaranteed to complete a pack/unpack round trip. Relative dangling links may be preserved where their representation and target type are adequate for the host.

Extraction accepts only relative targets whose resolution stays inside the destination root. Unlike entry names, a link target may contain `..`, interpreted relative to the link's parent. Do not use lexical normalization alone: a path such as `link/../file` can have different meaning when `link` is a symlink. Resolve the complete deferred-link graph and relevant existing components without following them for writes. Reject escaping targets, existing symlink/reparse components, cyclic resolution and ambiguous unsupported encodings/types. A target may be absent if its remaining lexical path stays inside the root. Convert separator representation only when the backend can preserve target semantics; never guess or silently rewrite an incompatible target.

Extract ordinary files/directories first, resolve hard-link groups next, and create validated symbolic links last. Deferred hard-link and symbolic-link graph traversal uses checked heap stacks, visiting states and resolved results rather than archive-depth recursion on the C stack. Symbolic frames retain continuations so subsequent components, including .., are interpreted after nested link resolution. Required parent directories are built iteratively. Allocation/size failures return results; no public depth quota is added. Never use an archive-created link as an intermediate directory. Do not apply chmod or timestamp operations by following symbolic-link targets. Link-specific metadata is best-effort only when safely available.

Libarchive's ZIP writer supports symbolic-link records, but its implementation notes that link handling is not standardized. Other ZIP tools may not restore them as links. ZIP symbolic-link support is therefore a tested Arc/backend capability, not a promise of universal ZIP interoperability. See the [ZIP writer](https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive_write_set_format_zip.c).

### 10.2 Hard links

Identify ordinary-file groups using stable filesystem identity, not matching contents or paths. Among selected sources, preserve one data-bearing member and additional aliases where the chosen format supports them. One selected name is an ordinary file even when unselected external aliases exist; do not search outside the requested inputs. Track groups across additions and finish any deferred backend records on close.

On extraction, targets use archive-root-relative entry names, not symbolic-link-relative resolution. Resolve groups even when data or referenced entries occur later. Every alias must ultimately reference an ordinary data-bearing member from this archive created by this extraction. Never link to a pre-existing destination file, directory or symbolic link. Missing/cyclic targets and conflicting member metadata fail as INVALID_ARCHIVE; unsafe targets fail as INVALID_PATH. Shared ordinary-file metadata belongs to the resolved data-bearing member, not independently to aliases. Hard links to symlink objects or directories are outside this contract.

TAR/PAX can preserve hard links. Libarchive's documented ZIP strategy stores independent bodies instead; Arc must detect a second selected alias and return UNSUPPORTED rather than silently lose the relationship. CPIO strategies differ and require group normalization/deferred flushing. See [libarchive's hard-link resolver](https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive_entry_linkify.3). Support of other formats must be verified before advertising their hard-link capability.

### 10.3 Windows limitations

Use Unicode Win32 APIs. Symbolic links require distinguishing file and directory targets. Request unprivileged creation when supported; Microsoft documents its dependency on Developer Mode. Privilege and filesystem availability remain runtime conditions. An unresolved target type, including a dangling link without usable type information, returns UNSUPPORTED rather than guessing. See [CreateSymbolicLinkW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createsymboliclinkw).

Hard-link creation applies to files on the same volume and depends on filesystem/service support; Microsoft's documented baseline is NTFS, and its API documents per-file link-count limits. Do not emulate unavailable hard links with copies. Native failures are reported through the result mapping. See [CreateHardLinkW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createhardlinkw).

Reparse points have different tags and can represent mounted folders or other behavior, not only symbolic links. Recognize supported symbolic-link objects explicitly; reject junctions and other unsupported reparse objects in source traversal rather than following them. Reject all redirection components on extraction paths. See [Microsoft's reparse-point documentation](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-points).

Use wide-character long-path-capable operations with deliberate path conversion; OS settings alone do not make every backend call long-path-capable. Report remaining backend/filesystem limits. The build does not add an application manifest and must not assume one enables long paths automatically.

## 11. Metadata policy

- Preserve source modification time and ordinary permission bits where the format can represent them.
- Synthetic text and chunk entries use the current modification time and ordinary file permissions 0644.
- Strip setuid, setgid and sticky bits.
- On POSIX extraction, ordinary permissions are limited by the process umask; do not request exact permission restoration that bypasses it.
- Extracted ownership follows the operating system's normal creation rules. Do not restore source UID/GID or owner names.
- Do not preserve or restore ACLs, extended attributes, security descriptors or immutable/append-only filesystem flags.
- Do not promise exact POSIX metadata fidelity on Windows or across all formats. Timestamp precision can be reduced by the format or filesystem.
- Restore metadata only on newly created ordinary files/directories. Do not apply archived permissions or timestamps to existing directories, including a reused extraction root. Normal filesystem changes to their timestamps (such as mtime/ctime on POSIX) are permitted on success and partial failure; do not restore pre-extraction timestamps. Finalize new directory metadata after descendants; never modify an external object through a link.

Metadata portability is a documented limitation. Content bytes and paths remain mandatory correctness requirements. Backend controls are private; see [libarchive's disk-writer documentation](https://github.com/libarchive/libarchive/blob/master/libarchive/archive_write_disk.3).

## 12. Streaming and resource limits

The wrapper copies filesystem files and extracted contents through a fixed-size buffer. Text helpers borrow the supplied text. The chunk API accepts arbitrary binary data and requires a known final size. Entry-name indexes, identity maps and deferred link records consume memory proportional to relevant entries; link resolution does not require buffering entire file contents.

Chunk acceptance is an API property, not a guarantee of constant total memory usage. The backend or selected format may buffer data, seek or use temporary storage. The API operates on archive paths, including sequential reader handles; it does not promise support for pipes, non-seekable input, network streams or user-supplied I/O callbacks. The sequential reader holds only the current record metadata at the wrapper layer.

The API sets no extraction byte quota, entry-count quota or decompression-ratio quota. It must not be presented as a resource-isolation mechanism for hostile archives. Such quotas would require additional explicit options.

`arc_inspect` may need to traverse compressed content to reach later entries. It is not guaranteed to be cheap. `content_bytes` is a logical-size sum, not required free disk space or a full integrity check.

## 13. Implemented extensions and deferred scope

### 13.1 Selective extraction and content verification

`arc_unpack_ex` accepts an `ArcExtractOptions` value with the destination mode and a borrowed array of UTF-8 archive paths. Each normalized selector matches its exact name and descendants at '/' component boundaries. Matching is byte-exact on every host. Empty selection extracts all; overlapping selectors do not process archive records twice. Stored paths and existing collision/metadata safety rules are preserved. Unselected directory metadata is not applied to implicit parents. Unmatched selectors return NOT_FOUND; absent/excluded hard-link targets return SELECTION_ERROR in selective mode. Dependencies are not silently extracted. Selected symlink targets may remain safely dangling under existing platform restrictions. Errors can leave partial output.

`arc_verify` reads every entry body to EOF without extracting files, checks declared ordinary-file sizes where known and propagates all backend integrity warnings/errors. It does not require safe extraction names, unique paths, supported host object types or normalized CPIO alias groups. It does not establish authenticity or safe extraction. Plain TAR has no per-file content checksum; same-size payload changes may remain undetected. Skipping entries in list/inspect or selective unpack is not equivalent to verification.

### 13.2 Passwords

Creation with a non-NULL `ArcOptions.password` requires ZIP and uses AES-256.
Only nonempty ordinary-file contents are encrypted; names, metadata, symbolic-link
targets, empty files and directories remain visible. Other output formats reject
password-protected creation before creating output.

`ArcReadOptions`, `ArcUnpackOptions` and `ArcExtractOptions` accept a nonempty
UTF-8 password; NULL means none. Empty or invalid UTF-8 passwords are rejected.
Read options are used by `arc_reader_open_ex`, `arc_list_ex`, `arc_inspect_ex`
and `arc_verify_ex`. Reading supports ZIP AES and ZipCrypto where available in
the backend. Password errors can occur during content reading after a successful
open. Metadata listing and skipping do not necessarily validate a password.
Missing and rejected passwords use `ARC_PASSWORD_REQUIRED` and
`ARC_PASSWORD_REJECTED`; backend diagnostics can limit classification.

The CLI accepts `--password` before operands for each archive command.
Command-line passwords can appear in shell history and process arguments.
Interactive password entry and password callbacks are outside the API.

### 13.3 Not planned for implementation at present

- Compression-level selection, internal compression-method selection and compression-thread tuning.
- Exact ownership, ACL, extended-attribute, security-descriptor or filesystem-flag restoration.
- Public memory, pipe, network or user-callback I/O APIs.
- Progress reporting and cancellation callbacks.
- Appending to or editing existing archives.
- Creating RAR, CAB or LHA with the initial backend.
- Multipart-volume handling.
- Hidden filenames, per-entry passwords and interactive/callback password providers.
- Glob/regex/exclusion selection languages or implicit extraction of unselected link dependencies.
- Special filesystem objects, hard links to directories/symlinks, Windows junction preservation and arbitrary reparse-point preservation.
- Unsafe external-link extraction, unresolved Windows link types and copying files as a substitute for links.
- Runtime backend selection and simultaneous backend instances.
- Automatic format substitution or external compressor execution.
- Rollback of partially extracted trees and crash-safe/atomic archive publication.
- Extraction quotas or resource-isolation guarantees for hostile archives.
- A guarantee that every variant of an advertised format is supported.

## 14. Command-line client

```text
Usage:
  arc pack [--format FORMAT] [--keep-root] [--password PASSWORD] ARCHIVE [SOURCE...]
  arc unpack [--entry PATH]... [--password PASSWORD] ARCHIVE [DESTINATION]
  arc list [--password PASSWORD] ARCHIVE
  arc info [--password PASSWORD] ARCHIVE
  arc verify [--password PASSWORD] ARCHIVE
  arc --help
  arc --version

Use -- to separate options from paths beginning with '-'.
```

### 14.1 Pack

```sh
arc pack ppa.tar ./ppa
arc pack ppa.zip ./ppa
arc pack --keep-root ppa.zip ./ppa
arc pack project.zip src README.md
arc pack backup.zip .
arc pack --format zip backup ./ppa
```

A single actual directory contributes its contents at the archive root; --keep-root retains its basename. A single file or symbolic link retains its basename. Multiple sources each retain their basenames. Trailing separators do not change this policy. --keep-root is valid only for one actual directory; reject other uses rather than silently ignoring it. No common parent is inferred and absolute source prefixes are never stored. Normalize source paths before basename selection; for --keep-root on `.` use the current directory's basename, and reject an unnamed filesystem root.

Recursion includes hidden objects and empty directories. Ignore files are not consulted. Symbolic links are stored without traversal. Exclude the created output by file identity, including hard-link aliases encountered during recursion; direct selection of that object fails. Duplicate stored names, special types and unsupported link representations are errors. Discard the output after a failed pack and report cleanup failures.

Select format/outer compression from the longest recognized output suffix, case-insensitively for ASCII suffixes: `.tar`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tar.xz`, `.tar.zst`, `.zip`, `.7z`. There is no default for an unknown suffix. --format accepts named container identifiers and supplies outer compression `none` for an unrecognized suffix. With a recognized suffix, its outer compression is retained only if the explicit container matches; a contradiction is a usage error. This preserves one clear source of truth without adding CLI compression tuning. Unsupported backend configurations are operational errors with no substitution.

With no source operands, pack uses a directory in the current working directory
named after the output basename with the full recognized suffix removed. An
output directory prefix does not affect that source location. With `--format`
and no recognized suffix, the full basename is used. The inferred source must
be an actual directory; `--keep-root` retains it as with an explicit source.

Other named formats are available through --format or the library without requiring a suffix for every format. Pack only creates an archive; it never unpacks automatically. The text helper remains a library operation, not a dedicated initial CLI mode.

### 14.2 Unpack

```sh
arc unpack ppa.zip
arc unpack ppa.zip .
arc unpack ppa.tar project
arc unpack /tmp/ppa.tar.gz
```

Without DESTINATION, use the archive basename minus its full recognized suffix as a directory in the current working directory; `/tmp/ppa.tar.gz` therefore extracts to `./ppa`. Naming recognizes the creation suffixes plus readable `.rar`, `.cab`, `.lha`, `.lzh`, `.cpio`, `.ar`, `.iso`, `.xar` and `.warc`. Match ASCII suffixes case-insensitively and choose the longest match. An unknown suffix appends `.extracted` to the whole basename. If stripping yields an empty, reserved or invalid component, try the whole basename plus `.extracted`; if that is invalid, require an explicit destination. Never silently choose a numbered directory.

Automatic destination uses NEW mode, so an existing object causes an error. An explicit destination uses MERGE mode, creates a missing directory or reuses an actual existing directory, and never overwrites files or links. Existing directories keep their permissions and ownership; archive metadata is not applied to them. Creating children may naturally change their timestamps, which are not restored. Parent directories must exist. No extraction step removes an archive's top-level directory: entry paths are preserved as stored. Input format is detected from content regardless of the filename.

### 14.3 List, info and parsing

The `verify` command reads all entry bodies and reports success only after complete reading and cleanup; it does not promise safe extraction or universal corruption detection. Repeatable `--entry PATH` on unpack selects the exact path and its subtree, preserving stored names.

List prints every entry's type, logical size when known and path; links also show their targets. It uses arc_list, not extraction. Escape control characters in displayed names/targets so one record cannot forge terminal output. Info prints disk size, data-bearing content size and counts by type. Neither command modifies the filesystem or requires permission to create links.

Paths containing spaces are shell-quoted. After the subcommand, parse supported options before operands; -- ends option parsing. Options after the first operand are not recognized. Unknown options, extra operands and missing required operands are usage errors. Provide --help globally and for each subcommand. Shell wildcard expansion remains the shell's responsibility; Arc does not implement its own globbing.

No interactive overwrite prompt, --overwrite, automatic skipping or silent substitution is included. Successful modifying commands print a brief completion message only after successful finalization/extraction. Operation failures identify the operation and path when available. Link and extraction failures can leave partial extracted output, which the diagnostic must identify. Output paths are never opened with truncation to resolve a conflict.

Exit codes: 0 for success, 1 for operational failure, 2 for command-line usage errors. Diagnostic output is English and written to stderr. CLI status and diagnostic text
escapes control characters from paths and backend messages; presentation color
sequences are generated separately. A stdout flush failure produces a nonzero
exit status, including failures discovered after buffered list/info output. Cleanup failures are reported separately from the original failure. `main.c` uses `wmain` on Windows and `main` on other targets.

## 15. API sequence examples

```c
ArcWriter *writer = NULL;
ArcResult result = arc_create("example.tar.zst", &writer);

if (result.code != ARC_OK)
    return report_error(result);

result = arc_add_text(writer, "message.txt", "Hello!\n");
if (result.code != ARC_OK) {
    ArcResult cleanup = arc_discard(&writer);
    report_cleanup_error(cleanup);
    return report_error(result);
}

result = arc_close(&writer);
if (result.code != ARC_OK)
    return report_error(result);

ArcUnpackOptions unpack_options = { ARC_DESTINATION_NEW };
result = arc_unpack("example.tar.zst", "extracted", &unpack_options);
if (result.code != ARC_OK)
    return report_error(result);
```

```c
result = arc_begin_file(writer, "binary.dat", 4);
if (result.code == ARC_OK) {
    const unsigned char bytes[] = { 0, 1, 2, 3 };
    result = arc_write_chunk(writer, bytes, sizeof bytes);
}
if (result.code == ARC_OK)
    result = arc_end_file(writer);
```

The second fragment assumes a ready writer and requires close on success or discard on failure. Reporting helpers are private to the client, not part of the public API.

The CLI's directory-content policy is expressed explicitly at the library boundary:

```c
/* Add contents at root; use "ppa" instead of "" to keep the source root. */
result = arc_add_path(writer, "./ppa", "");

/* An explicitly selected destination may merge actual directories only. */
ArcUnpackOptions merge = { ARC_DESTINATION_MERGE };
result = arc_unpack("ppa.zip", ".", &merge);
```

These independent fragments require normal result checks and writer cleanup. CLI policy chooses the root mapping; the library owns traversal, link handling and collision enforcement.

## 16. Build and integration

The mise tasks own local builds, formatting and black-box CLI tests. CI runs
builds and tests on macOS, Linux and Windows; sanitizers run on macOS and Linux.
Tests use a freshly built executable and isolated temporary fixtures. Filekit
has a separate API suite. See the [test guide](../tests/README.md).

The mise build emits `libarc.a` containing arc and private filekit objects.
Libarchive remains external. CMake 3.21+ provides `arc::arc` for source-tree
embedding and installed packages, plus relocatable pkg-config metadata. The root `VERSION` supplies the release
version for both build paths and installed package metadata.
Only `arc.h` is installed as arc's public header. C++ consumers use C linkage;
the implementation is compiled as C11. MSYS2 UCRT64 is the supported Windows
toolchain; MSVC is not verified.

The CLI is optional and defaults off in an embedded CMake build. Library
requirements stay scoped to the library; developer warnings are opt-in and
Windows `-municode` applies only to the CLI. Consumer checks cover source and
relocated installed CMake packages and pkg-config with C/C++ consumers.
See [Building arc](build.md) for commands and options.

## 17. Acceptance criteria

1. A caller compiles against `arc.h` without any libarchive header or constant.
2. Available configurations are reported honestly; unavailable requests return explicit errors without substitution or output-file creation.
3. TAR, TAR.GZ and ZIP complete create/add/close/extract byte-preserving cycles on macOS, Linux and Windows. Additional advertised creation formats receive representative round-trip verification.
4. Supported RAR fixtures extract; RAR creation returns unsupported. Available 7z and additional readers receive representative fixture verification.
5. Empty files, binary null bytes, multiple entries and multi-chunk writes preserve bytes. Declared-size mismatches and invalid call sequences produce errors.
6. Close and discard consume handles on every outcome. Cleanup failures do not hide the initiating error.
7. Existing archive files are never overwritten. NEW extraction rejects any existing root; MERGE reuses actual directories without applying archive metadata; natural filesystem timestamp changes are permitted. File/link collisions and traversal through links/reparse objects return errors, never skipped-success results.
8. UTF-8 names, Windows reserved names, long paths and platform-specific collision behavior are exercised on the relevant platform.
9. Statistics match file sizes and entry counts. Unknown sizes and arithmetic overflow cannot produce misleading successful results.
10. POSIX metadata checks verify ordinary permissions subject to umask and absence of restored privilege bits. Existing root and nested directory checks verify retained permissions and ownership, absence of applied archive timestamps, and permitted natural timestamp changes. Cross-format metadata loss follows the documented policy.
11. Compiler warnings are resolved. Sanitizer checks are used where available for lifecycle and memory verification. Real builds and execution on each target are recorded separately from review of the recipes.
12. Temporary verification tools and fixtures remain outside the source tree. The private modules preserve the public API and CLI and library consumers link the selected complete object list.
13. CLI exercises single-directory contents, --keep-root, multiple sources, hidden files, empty directories, compound suffixes, explicit format conflicts, automatic destination naming, existing explicit destinations and -- path parsing.
14. Recursive packing excludes its output object by identity and rejects duplicate names across calls. A lone selected hard-link name remains an ordinary file; multiple selected aliases preserve identity in supported formats and fail explicitly in ZIP.
15. Symbolic-link fixtures cover file/directory targets, chains, forward targets, dangling links, absolute/external targets, cycles and paths containing both links and `..`. Packing never follows targets; extraction never writes through links. Supported internal links round-trip, while unsafe extraction targets fail as documented.
16. Hard-link fixtures cover forward targets, multiple aliases, conflicting metadata, missing/cyclic targets and attempts to link to existing objects. On supported hosts, verify shared identity rather than equal bytes alone.
17. Windows checks run with and without adequate symlink permissions, exercise known/unknown target types, junction/reparse rejection, filesystem limitations and long Unicode paths. Lack of privilege is a reported failure, never an implicit copy.
18. List reports links and unknown sizes without requiring link-creation privileges, escapes display controls, and propagates visitor failures while releasing resources. Info counters and data-byte accounting match normalized fixtures.

## 18. Decisions to revisit only when required

Introduce backend dispatch only when one process must choose among multiple implementations. Add generic callbacks only when non-filesystem I/O becomes a requirement. Add security quotas only when bounded hostile-input processing becomes a requirement. Expand metadata controls only for an explicit backup/restore use case.

Before making any such change, preserve the existing human-oriented operation vocabulary and keep backend details private.

## 19. Backend limitations

Important distinctions:

- Link support is a format/backend feature plus a runtime filesystem/privilege condition.
- ZIP hard-link body duplication is unsuitable for Arc's preservation contract.
- A backend NO_OVERWRITE flag is not sufficient evidence of a conflict error.
- Windows reparse points are not interchangeable with symbolic links.
- A chunk API does not establish constant memory consumption or non-seekable I/O support.

The design intentionally adds entry-name/identity indexes and deferred link resolution because the accepted CLI and preservation contracts require them. All Arc-owned helper failures and returned native/backend failures flow through explicit result values.

### Accepted backend failure boundary

Arc uses unmodified libarchive with the following boundary: Arc-owned failures and returned dependency failures use result values, while dependency-internal fatal termination is outside the return-value guarantee. Certain libarchive entry-string getters call a fatal helper on allocation failure; that helper exits the process. No exception interception, signal recovery, subprocess isolation or patched libarchive build is added.

Evidence: [entry string getters](https://github.com/libarchive/libarchive/blob/master/libarchive/archive_entry.c) and [fatal helper](https://github.com/libarchive/libarchive/blob/master/libarchive/archive_util.c).

### Selection and verification API example

```c
const char *paths[] = {"docs", "images/logo.png"};
ArcExtractOptions options = {ARC_DESTINATION_NEW, paths, 2};
ArcResult extracted = arc_unpack_ex("project.zip", "selected", &options);
ArcResult verified = arc_verify("project.zip");
```

Both results require normal error handling. Verification is separate from extraction; either operation can succeed while the other fails for its own contract reasons.

### Sequential reader example

```c
ArcReader *reader = NULL;
ArcResult r = arc_reader_open("project.zip", &reader);
while (r.code == ARC_OK) {
    ArcEntry entry;
    int end;
    r = arc_reader_next(reader, &entry, &end);
    if (r.code != ARC_OK || end)
        break;
    unsigned char buffer[65536];
    for (;;) {
        size_t received;
        r = arc_reader_read(reader, buffer, sizeof buffer, &received);
        if (r.code != ARC_OK || received == 0)
            break;
        /* Consume received bytes; entry strings remain valid here. */
    }
}
ArcResult cleanup = arc_reader_close(&reader);
/* Report r if it failed; otherwise report any cleanup failure. */
```

The current public header is authoritative for all reader signatures and lifetime/error contracts. Generic callback-based I/O is outside the public API.
