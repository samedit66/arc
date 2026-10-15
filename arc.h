#ifndef ARC_H
#define ARC_H

/** Human-oriented archive API. Returned errors are values; backend-internal fatal termination
 * is outside the return-value guarantee. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Output archive configuration. AR accepts only flat entry names;
 * 7z requires outer compression "none". */
typedef struct {
    /** Non-NULL, case-sensitive arc format identifier. */
    const char *format;

    /** Non-NULL outer compression identifier; "none" means no wrapper. */
    const char *compression;
    /** NULL for no encryption; nonempty UTF-8 password selects ZIP AES-256.
     * Borrowed for the call only; other output formats return UNSUPPORTED.
     * Protects nonempty ordinary-file contents, not names, metadata or link targets. */
    const char *password;
} ArcOptions;

/** Exclusively owned archive writer.
 * Ready: add/begin; entry open: write/end; failed: close/discard only.
 * Every failed content operation on a non-NULL writer invalidates it, including
 * argument, path and state errors. The first failure is retained for close.
 * Close/discard consume the pointer on every outcome; NULL values are no-ops.
 * Output is bound to its creation directory. Sequential caller cwd changes
 * between operations are allowed; relative sources use each call's cwd.
 * Concurrent process cwd changes and output/parent replacement are unsupported.
 */
typedef struct ArcWriter ArcWriter;

/** Exclusively owned sequential reader; input must remain unchanged. */
typedef struct ArcReader ArcReader;

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

/** Machine-readable diagnostic category; messages are not a parsing interface. */
typedef enum {
    ARC_REASON_NONE = 0,
    ARC_REASON_DESTINATION_EXISTS,
    ARC_REASON_ENTRY_EXISTS,
    ARC_REASON_ARCHIVE_CONFLICT,
    ARC_REASON_UNSAFE_PATH,
    ARC_REASON_STRIP_ROOT
} ArcReason;

/** Self-contained operation result returned by value. Path buffers contain UTF-8
 * when not truncated; truncated paths must only be displayed with an indication.
 * Recompile consumers when migrating from the original two-field result. */
typedef struct {
    ArcCode code;
    char message[256];
    ArcReason reason;
    char path[1024];
    int path_truncated;
    /** Output may remain after a failed modifying operation; no extraction rollback. */
    int partial_output;
    /** First secondary cleanup failure; empty if none was recorded. */
    char cleanup_message[256];
} ArcResult;

/** Nonzero exactly when result.code is ARC_OK. */
int arc_ok(ArcResult result);

/** Progress is advisory: only the returned ArcResult determines success. */
typedef enum { ARC_PHASE_PREPARING, ARC_PHASE_PROCESSING, ARC_PHASE_FINALIZING } ArcProgressPhase;

/** Borrowed snapshot, valid only during the callback. No overall total is implied. */
typedef struct {
    ArcProgressPhase phase;
    /** Original archive entry path, or NULL when unavailable or between entries. */
    const char *path;
    /** Records encountered in this phase, including skipped/unselected records.
     * Resets on a phase change; not a count of successfully completed files. */
    uint64_t entries_seen;
    /** Content bytes accepted by the pack backend, written by unpack, returned
     * by reader_read, or read by verify. Skips and preflight add no bytes.
     * Monotonic across phases; not compressed archive bytes or durable writes. */
    uint64_t content_bytes;
    uint64_t entry_bytes;
    /** Ignore entry_size unless entry_size_known is nonzero. */
    uint64_t entry_size;
    int entry_size_known;
} ArcProgress;

/** Synchronous observation on the calling thread, within its temporary UTF-8
 * locale scope. No background threads or maximum callback interval are promised.
 * Copy snapshots/strings before retaining them. Do not reenter the active handle,
 * change cwd, mutate the input/output files, or unwind across this C callback.
 * Callbacks cannot cancel or report errors; finalization can fail after all data
 * was processed. Slow callbacks delay the operation. */
typedef void (*ArcProgressCallback)(const ArcProgress *progress, void *context);

typedef struct {
    /** NULL disables observation; context may be NULL. */
    ArcProgressCallback callback;
    void *context;
} ArcObserver;

/** Whole-operation packing options. Zero-initialize before setting fields.
 * This replaces the former configuration pointer; rebuild and migrate clients. */
typedef struct {
    /** NULL infers format from the output suffix; empty strings are invalid. */
    const char *format;
    /** NULL uses suffix compression with inferred format, otherwise "none". */
    const char *compression;
    /** NULL for none; nonempty UTF-8 password requires ZIP AES-256. */
    const char *password;
    /** Valid only for a single actual directory. */
    int keep_root;
    /** Context is borrowed until arc_pack_ex returns. */
    ArcObserver observer;
} ArcPackOptions;

/** Pack one filesystem source. A directory contributes its contents; a file or
 * link retains its basename. Infer encoding from the output suffix. Never
 * overwrite. On failure remove the newly created output where possible;
 * cleanup_message and partial_output report cleanup failures. */
ArcResult arc_pack(const char *archive_path, const char *source_path);

/** Pack explicit sources; count must be positive. Multiple sources retain their
 * basenames. Does not follow symbolic links or consult ignore files. Sources
 * must remain unchanged; concurrent cwd/output replacement is unsupported.
 * NULL options uses defaults. Explicit format/compression override suffix inference.
 * Source strings and options are borrowed only for the call. */
ArcResult arc_pack_ex(const char *archive_path, const char *const *sources, size_t count,
                      const ArcPackOptions *options);

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
    ARC_CAN_CREATE = 1u << 0,
    ARC_CAN_INSPECT = 1u << 1,
    ARC_CAN_UNPACK = 1u << 2,
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

/** Borrowed metadata: visitor-call lifetime, or reader-next lifetime as documented below. */
typedef struct {
    const char *path;
    ArcEntryType type;
    uint64_t size;
    int size_known;
    /** NULL for non-links or when the target cannot be exposed. */
    const char *link_target;
} ArcEntry;

/** Root creation policy; neither mode permits file overwrites. */
typedef enum { ARC_DESTINATION_NEW, ARC_DESTINATION_MERGE } ArcDestinationMode;

/** Whole-operation unpack options. Zero-initialize before setting fields. */
typedef struct {
    ArcDestinationMode destination_mode;
    /** Borrowed UTF-8 relative archive paths; each includes its subtree. */
    const char *const *paths;
    /** Zero extracts all entries. Positive count requires a non-NULL array. */
    size_t path_count;
    /** Nonempty UTF-8 password borrowed for the call; NULL means none. */
    const char *password;
    /** Remove the single common root directory. Preflight all entry names before
     * creating the destination; reject empty/mixed-root archives and root files.
     * Selectors refer to original paths. Hard-link paths are transformed;
     * symlink text is preserved without resolving its target.
     * Hard links escaping the stripped tree are rejected. Root metadata is omitted.
     * Requires a second metadata pass; compressed input may be decompressed twice. */
    int strip_root;
    /** Context is borrowed until arc_unpack_ex returns. */
    ArcObserver observer;
} ArcUnpackOptions;

/** Zero-initialize read options before setting fields.
 * Password is copied by the backend during opening. Empty or invalid UTF-8
 * passwords are invalid. NULL means no password. Errors may occur during
 * reading; metadata enumeration and skipping need not decrypt content. */
typedef struct {
    const char *password;
    /** Copied into a sequential reader: context must survive until reader_close.
     * For list/inspect/verify it is borrowed only until the operation returns. */
    ArcObserver observer;
} ArcReadOptions;

/**
 * Receive borrowed metadata for one entry during arc_list.
 *
 * - `entry`: Non-NULL read-only metadata, valid only during this callback.
 * - `context`: Caller-owned context passed to arc_list; may be NULL.
 *
 * **Returns:** ARC_OK to continue, or an error result to stop and propagate it.
 *
 * Example callback and invocation (inside a caller function):
 * ```c
 * static ArcResult count_entry(const ArcEntry *entry, void *context) {
 *     (void)entry;
 *     ++*(size_t *)context;
 *     return (ArcResult){.code = ARC_OK};
 * }
 * // Inside a caller function:
 * size_t count = 0;
 * ArcResult result = arc_list("backup.tar", count_entry, &count);
 * ```
 */
typedef ArcResult (*ArcVisitor)(const ArcEntry *entry, void *context);

/**
 * Query available operations for a format and outer compression.
 *
 * - `options`: Non-NULL requested configuration.
 * - `operations`: Non-NULL output bitmask; unchanged on failure.
 *
 * Creates no files. Unavailable configurations return UNSUPPORTED.
 * Success describes ordinary supported variants in the current build,
 * not every encryption method, entry type or format-specific feature.
 * With a password, CREATE requires available ZIP AES-256 encryption.
 * Read bits do not guarantee support for encrypted variants.
 * 7z requires outer compression "none". ZIP with outer compression does not
 * advertise INSPECT because its streaming reader may expose unknown sizes.
 * Entry-feature bits describe read/write representation, not filesystem
 * permissions or guaranteed extraction on the current host.
 *
 * Example:
 * ```c
 * ArcOptions options = {.format = "tar", .compression = "none"};
 * unsigned int operations = 0;
 * ArcResult result = arc_get_capabilities(&options, &operations);
 * ```
 */
ArcResult arc_get_capabilities(const ArcOptions *options, unsigned int *operations);

/**
 * Create a new archive writer using the longest recognized output suffix.
 *
 * - `path`: Non-NULL UTF-8 output path; parent must exist.
 * - `out`: Non-NULL output pointer; receives a writer or NULL on failure.
 *
 * Recognized suffixes (ASCII case-insensitive): .tar, .tar.gz, .tgz, .tar.bz2,
 * .tar.xz, .tar.zst, .zip, .7z. Unknown suffixes return ARC_INVALID_ARGUMENT
 * without creating a file. Never overwrites an existing object.
 * Ownership and cleanup follow arc_create_ex.
 *
 * Example:
 * ```c
 * ArcWriter *writer = NULL;
 * ArcResult result = arc_create("backup.tar.gz", &writer);
 * if (result.code == ARC_OK) {
 *     result = arc_close(&writer);
 * }
 * ```
 */
ArcResult arc_create(const char *path, ArcWriter **out);

/**
 * Create a new archive writer with explicit configuration.
 * The output suffix is ignored; options alone determine the archive encoding.
 *
 * - `path`: Non-NULL UTF-8 output path; parent must exist.
 * - `options`: Non-NULL requested format and outer compression.
 * - `out`: Non-NULL output pointer; receives a writer or NULL on failure.
 *
 * Never overwrites an existing object. Checks configuration support before
 * creating the file. On success, ownership requires close or discard.
 * A failure after file creation may leave an incomplete output file if
 * cleanup fails; the diagnostic must identify this condition.
 *
 * Example:
 * ```c
 * ArcOptions options = {.format = "tar", .compression = "none"};
 * ArcWriter *writer = NULL;
 * ArcResult result = arc_create_ex("backup.tar", &options, &writer);
 * if (result.code == ARC_OK) {
 *     result = arc_close(&writer);
 * }
 * ```
 */
ArcResult arc_create_ex(const char *path, const ArcOptions *options, ArcWriter **out);

/**
 * Add an ordinary filesystem file, copying its data in chunks.
 *
 * - `writer`: Active writer with no unfinished entry.
 * - `source_path`: Non-NULL UTF-8 source path; file must remain unchanged.
 * - `entry_path`: Non-NULL safe relative UTF-8 archive path using '/'.
 *
 * Rejects symbolic links and special source files. Records modification time and
 * ordinary permission bits where representable. Does not add directories
 * recursively. Duplicate entry names are rejected. Hard-link relationships
 * among selected ordinary files are preserved across filesystem additions;
 * unsupported preservation returns UNSUPPORTED.
 *
 * Example (writer was successfully created and has no unfinished entry):
 * ```c
 * ArcResult result = arc_add_file(writer, "notes.txt", "docs/notes.txt");
 * ```
 */
ArcResult arc_add_file(ArcWriter *writer, const char *source_path, const char *entry_path);

/**
 * Add a filesystem file, directory tree or symbolic link.
 *
 * - `writer`: Active writer with no unfinished entry.
 * - `source_path`: Non-NULL UTF-8 source path; tree must remain unchanged.
 * - `entry_path`: Non-NULL relative archive path. An empty string is allowed
 *             only for an actual directory and adds its contents at root.
 *
 * Recurses through actual directories, including hidden files and empty
 * directories. Does not follow symbolic links or consult ignore files.
 * Preserves links where representable, without silently copying targets.
 * Excludes the output object from traversal by filesystem identity.
 * An explicitly selected source equal to the output object is an error.
 * Duplicate paths, special objects and unsupported representations fail.
 *
 * Example (writer was successfully created and has no unfinished entry):
 * ```c
 * ArcResult result = arc_add_path(writer, "photos", "photos");
 * ```
 */
ArcResult arc_add_path(ArcWriter *writer, const char *source_path, const char *entry_path);

/**
 * Add a null-terminated text string as an ordinary file.
 *
 * - `writer`: Active writer with no unfinished entry.
 * - `entry_path`: Non-NULL safe relative UTF-8 archive path.
 * - `text`: Non-NULL string; bytes before its terminator are stored unchanged.
 *
 * Empty text creates an empty file. Uses permissions 0644 and the current
 * modification time where representable. Embedded null bytes require the
 * chunk API. Duplicate entry names are rejected.
 *
 * Example (writer was successfully created and has no unfinished entry):
 * ```c
 * ArcResult result = arc_add_text(writer, "hello.txt", "Hello!");
 * ```
 */
ArcResult arc_add_text(ArcWriter *writer, const char *entry_path, const char *text);

/**
 * Begin an ordinary file entry for chunk-based writing.
 *
 * - `writer`: Active writer with no unfinished entry.
 * - `entry_path`: Non-NULL safe relative UTF-8 archive path.
 * - `size`: Exact content size; must be representable by the backend.
 *
 * AR requires flat entry names; directory components return UNSUPPORTED.
 * Uses permissions 0644 and the current modification time where supported.
 * Success requires a later end_file, including for zero-byte entries.
 * Duplicate entry names are rejected.
 *
 * Example (writer was successfully created and has no unfinished entry):
 * ```c
 * ArcResult result = arc_begin_file(writer, "data.bin", 3);
 * if (result.code == ARC_OK) {
 *     result = arc_write_chunk(writer, "abc", 3);
 * }
 * if (result.code == ARC_OK) {
 *     result = arc_end_file(writer);
 * }
 * ```
 */
ArcResult arc_begin_file(ArcWriter *writer, const char *entry_path, uint64_t size);

/**
 * Write the next chunk of the open entry.
 *
 * - `writer`: Writer with an entry opened by begin_file.
 * - `data`: Borrowed bytes; NULL is allowed only when size is zero.
 * - `size`: Number of bytes to write; must not exceed the remaining size.
 *
 * Success means the complete chunk was accepted. No data pointer is retained.
 * The exact final size is required by begin_file. Backend/format buffering and
 * temporary storage remain possible: chunk acceptance does not promise bounded
 * total memory or pipe/callback output. Unsupported writes return UNSUPPORTED.
 * Failure may follow partial backend output and invalidates the writer.
 *
 * Example (writer has an open entry with at least sizeof data bytes remaining):
 * ```c
 * const unsigned char data[] = {0x00, 0x01, 0x02};
 * ArcResult result = arc_write_chunk(writer, data, sizeof data);
 * ```
 */
ArcResult arc_write_chunk(ArcWriter *writer, const void *data, size_t size);

/**
 * Complete the currently open entry.
 *
 * - `writer`: Writer with an unfinished entry.
 *
 * Requires exactly the declared byte count. A short entry returns
 * SIZE_MISMATCH. Success makes the writer ready for another entry.
 *
 * Example (writer has an open entry with all declared bytes written):
 * ```c
 * ArcResult result = arc_end_file(writer);
 * ```
 */
ArcResult arc_end_file(ArcWriter *writer);

/**
 * Finalize an archive and release its writer, even on failure.
 *
 * - `writer`: Non-NULL address of the owned pointer; set to NULL on return.
 *         A NULL pointer value is a successful no-op.
 *
 * Reports unfinished entries, prior failures and finalization errors.
 * A failed close may leave an incomplete file; no writer remains for retry.
 * Its code preserves the first failure; secondary cleanup diagnostics are
 * retained separately in cleanup_message.
 *
 * Example (writer is an owned ArcWriter * or NULL):
 * ```c
 * ArcResult result = arc_close(&writer);
 * // writer is now NULL; check result for finalization errors.
 * ```
 */
ArcResult arc_close(ArcWriter **writer);

/**
 * Release an unfinished writer and remove its output file.
 *
 * - `writer`: Non-NULL address of the owned pointer; set to NULL on return.
 *         A NULL pointer value is a successful no-op.
 *
 * Returns an error if resource cleanup or removal fails. No writer remains
 * for retry. Callers must report cleanup failures without hiding the original
 * operation failure. The output path must not be concurrently replaced.
 *
 * Example (writer is an owned ArcWriter * or NULL):
 * ```c
 * ArcResult cleanup = arc_discard(&writer);
 * // writer is now NULL; report cleanup errors alongside the original failure.
 * ```
 */
ArcResult arc_discard(ArcWriter **writer);

/**
 * Extract an archive without overwriting existing files or links.
 *
 * - `archive_path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * - `destination`: Non-NULL UTF-8 directory path; parent must exist.
 * Requires atomic creation of a new destination directory. Use arc_unpack_ex
 * to merge directories, select paths, strip the root or supply a password.
 *
 * Detects format from content. Supports ordinary files, directories and
 * permitted symbolic/hard links. Rejects
 * unsafe paths, special files, file collisions and traversal through links.
 * Creates deferred links after data extraction. Symbolic-link targets are
 * preserved without filesystem lookup; absolute, external, dangling and cyclic
 * targets are permitted on POSIX. Windows requires a known file/directory kind,
 * supplied by archive metadata or inferred from selected archive records.
 * Unknown Windows kinds return UNSUPPORTED. Hard-link targets are validated.
 * Restores supported metadata on new objects. Does not apply archive metadata
 * to existing directories or restore ownership. Creating children may change
 * existing directory timestamps through normal filesystem behavior.
 * Failure may leave partial output.
 *
 * Example:
 * ```c
 * ArcResult result = arc_unpack("backup.tar", "restored");
 * ```
 */
ArcResult arc_unpack(const char *archive_path, const char *destination);

/**
 * Extract selected archive paths without overwriting existing objects.
 *
 * - `archive_path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * - `destination`: Non-NULL UTF-8 directory path; parent must exist.
 * - `options`: NULL uses defaults (NEW destination); zero path_count extracts all entries.
 * Selectors are safe relative UTF-8 names; repeated/overlapping selectors
 * process each archive record once. Matching is byte-exact, case-sensitive
 * on every host, and includes descendants at '/' boundaries. Stored paths
 * are preserved; required parent directories are created. Only selected
 * directory records supply metadata. An unmatched selector returns NOT_FOUND.
 * A selected hard link requires its complete target chain in the selection;
 * an absent target returns SELECTION_ERROR. Symlink targets are not added
 * implicitly; dangling links follow the existing platform restrictions.
 * Without strip_root, unselected paths/types are not extraction-validated;
 * strip_root checks every entry name during preflight. Unreadable headers,
 * names or skipped data can still fail the operation. Encrypted selected
 * entries use the password where supported. Skipping does not verify unselected content.
 * All ordinary unpack guarantees apply. Failure may leave partial output,
 * including an empty destination when no selector matched.
 *
 * Example:
 * ```c
 * const char *paths[] = {"docs", "hello.txt"};
 * ArcUnpackOptions options = {
 *     .destination_mode = ARC_DESTINATION_NEW,
 *     .paths = paths,
 *     .path_count = sizeof paths / sizeof paths[0]
 * };
 * ArcResult result = arc_unpack_ex("backup.tar", "selected", &options);
 * ```
 */
ArcResult arc_unpack_ex(const char *archive_path, const char *destination,
                        const ArcUnpackOptions *options);

/**
 * Read every archive entry body to EOF without extracting it to disk.
 *
 * - `archive_path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * Success means complete readable entry data, declared ordinary-file sizes
 * where known, and backend-provided integrity checks passed. All backend
 * warnings are failures. Encrypted content requires a password where supported.
 * Does not validate extraction safety, link targets or unique names, require
 * known sizes, or normalize CPIO link groups. Plain TAR has no per-file data
 * checksum: arbitrary same-size payload changes can remain undetected.
 * Does not establish authenticity or inspect every trailing container byte.
 * May decompress the entire archive and use backend buffering/temp storage.
 *
 * Example:
 * ```c
 * ArcResult result = arc_verify("backup.tar");
 * ```
 */
ArcResult arc_verify(const char *archive_path);

/**
 * Visit archive entries without extracting them to disk.
 *
 * - `archive_path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * - `visitor`: Non-NULL callback receiving borrowed read-only entry metadata.
 * - `context`: Caller-owned callback context; may be NULL.
 *
 * Calls visitor in archive enumeration order. A non-OK visitor result stops
 * enumeration and is returned unchanged; resources are still released.
 * Logical entry types/targets are normalized where the backend can expose
 * them. Unknown sizes use size_known == 0; size must then be ignored.
 * Missing/unrepresentable path text fails rather than emitting a mangled name.
 * Listing does not validate extraction safety or require host link privileges.
 *
 * Example (count_entry is the callback shown in ArcVisitor):
 * ```c
 * size_t count = 0;
 * ArcResult result = arc_list("backup.tar", count_entry, &count);
 * ```
 */
ArcResult arc_list(const char *archive_path, ArcVisitor visitor, void *context);

/**
 * Open a sequential reader without extracting files.
 *
 * - `path`: Non-NULL UTF-8 archive path; input must remain unchanged until close.
 * - `out`: Non-NULL owned output; set to NULL on failure. Success requires close.
 * A reader must not be used concurrently. Formats may buffer, seek or use
 * backend temporary storage; this API does not promise constant total memory.
 *
 * Example:
 * ```c
 * ArcReader *reader = NULL;
 * ArcResult result = arc_reader_open("backup.tar", &reader);
 * if (result.code == ARC_OK) {
 *     result = arc_reader_close(&reader);
 * }
 * ```
 */
ArcResult arc_reader_open(const char *path, ArcReader **out);

/**
 * Open a sequential reader with optional password credentials and progress observer.
 *
 * - `path`: Non-NULL UTF-8 input path; unchanged until close.
 * - `options`: Borrowed read configuration, or NULL for no password/observer.
 * - `out`: Non-NULL owned output; set to NULL on failure; close after success.
 *
 * NULL options means no password. Existing operation contracts apply.
 * Unsupported encryption returns UNSUPPORTED. Missing passwords return
 * PASSWORD_REQUIRED when decryption is needed; rejected passwords return
 * PASSWORD_REJECTED. Corrupt encrypted data can return INVALID_ARCHIVE.
 * ZIP names remain visible.
 *
 * Example:
 * ```c
 * ArcReadOptions options = {.password = "secret"};
 * ArcReader *reader = NULL;
 * ArcResult result = arc_reader_open_ex("backup.zip", &options, &reader);
 * if (result.code == ARC_OK) {
 *     result = arc_reader_close(&reader);
 * }
 * ```
 */
ArcResult arc_reader_open_ex(const char *path, const ArcReadOptions *options, ArcReader **out);

/**
 * Verify every entry body with optional password credentials.
 *
 * - `path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * - `options`: Borrowed read configuration, or NULL for no password/observer.
 *
 * NULL options means no password. Existing operation contracts apply.
 * Unsupported encryption returns UNSUPPORTED. Missing passwords return
 * PASSWORD_REQUIRED when decryption is needed; rejected passwords return
 * PASSWORD_REJECTED. Corrupt encrypted data can return INVALID_ARCHIVE.
 * ZIP names remain visible.
 *
 * Example:
 * ```c
 * ArcReadOptions options = {.password = "secret"};
 * ArcResult result = arc_verify_ex("backup.zip", &options);
 * ```
 */
ArcResult arc_verify_ex(const char *path, const ArcReadOptions *options);

/**
 * Visit entry metadata with optional password credentials.
 *
 * - `path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * - `options`: Borrowed read configuration, or NULL for no password/observer.
 * - `visitor`: Non-NULL metadata callback; borrowed entries have callback lifetime.
 * - `context`: Caller-owned callback context; may be NULL.
 *
 * NULL options means no password. Existing operation contracts apply.
 * Unsupported encryption returns UNSUPPORTED. Missing passwords return
 * PASSWORD_REQUIRED when decryption is needed; rejected passwords return
 * PASSWORD_REJECTED. Corrupt encrypted data can return INVALID_ARCHIVE.
 * ZIP names remain visible.
 *
 * Example:
 * ```c
 * // count_entry is the callback shown in ArcVisitor.
 * ArcReadOptions options = {.password = "secret"};
 * size_t count = 0;
 * ArcResult result = arc_list_ex("backup.zip", &options, count_entry, &count);
 * ```
 */
ArcResult arc_list_ex(const char *path, const ArcReadOptions *options, ArcVisitor visitor,
                      void *context);

/**
 * Inspect archive sizes and counts with optional password credentials.
 *
 * - `path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * - `options`: Borrowed read configuration, or NULL for no password/observer.
 * - `info`: Non-NULL statistics output; unchanged on failure.
 *
 * NULL options means no password. Existing operation contracts apply.
 * Unsupported encryption returns UNSUPPORTED. Missing passwords return
 * PASSWORD_REQUIRED when decryption is needed; rejected passwords return
 * PASSWORD_REJECTED. Corrupt encrypted data can return INVALID_ARCHIVE.
 * ZIP names remain visible.
 *
 * Example:
 * ```c
 * ArcReadOptions options = {.password = "secret"};
 * ArcInfo info = {0};
 * ArcResult result = arc_inspect_ex("backup.zip", &options, &info);
 * ```
 */
ArcResult arc_inspect_ex(const char *path, const ArcReadOptions *options, ArcInfo *info);

/**
 * Advance to the next archive record, automatically skipping unread content.
 *
 * - `reader`: Non-NULL active reader. First call returns the first record.
 * - `entry`: Non-NULL output for borrowed metadata; cleared at archive EOF.
 * - `end`: Non-NULL output: 0 for an entry, 1 for archive EOF.
 *
 * Success with *end == 0 publishes borrowed metadata. Strings remain valid
 * through read/skip until the next valid next call (even if it fails) or close.
 * Copy strings to retain them. Success with *end == 1 clears *entry; repeated
 * next calls then return the same EOF outcome without backend I/O.
 * Invalid arguments/state leave outputs and reader unchanged. Other failures
 * leave outputs unchanged, retain the first failure and invalidate the reader.
 * Advancing can perform I/O/decompression and fail while skipping old content.
 * Records retain archive order and stored names; no extraction safety check
 * or link traversal occurs. Ambiguous CPIO link groups are unsupported. EOF is ordinary success,
 * not an error code.
 *
 * Example (reader was successfully opened):
 * ```c
 * ArcEntry entry = {0};
 * int end = 0;
 * ArcResult result = arc_reader_next(reader, &entry, &end);
 * if (result.code == ARC_OK && !end) {
 *     // entry describes the current record; copy strings if retaining them.
 * }
 * ```
 */
ArcResult arc_reader_next(ArcReader *reader, ArcEntry *entry, int *end);

/**
 * Read up to capacity bytes of the current ordinary-file record.
 *
 * - `reader`: Non-NULL reader positioned by next on a record, including record EOF.
 * - `buffer`: Non-NULL caller-owned output buffer with capacity bytes.
 * - `capacity`: Positive maximum number of bytes to read.
 * - `received`: Non-NULL output byte count; unchanged on failure.
 *
 * Short reads are permitted. Only successful *received == 0 means this record's end; repeated
 * reads then return zero. Directories/links/other non-file records yield zero,
 * with link targets available in metadata; targets are never read implicitly.
 * Known ordinary-file sizes and backend integrity signals are checked when
 * actual data EOF is reached. Sparse gaps are returned as zero bytes.
 * The buffer is caller-owned and never retained. On failure, *received is
 * unchanged but the buffer may have been partly modified and must be ignored.
 * Argument/state errors do not invalidate the reader; backend, integrity,
 * size/overflow and conversion-context failures do. Earlier successful chunks
 * remain accepted by the caller; a later error cannot retract them.
 *
 * Example (reader is positioned on a record by a successful next call):
 * ```c
 * unsigned char buffer[4096];
 * size_t received = 0;
 * ArcResult result = arc_reader_read(reader, buffer, sizeof buffer, &received);
 * if (result.code == ARC_OK && received > 0) {
 *     // Consume buffer[0..received); repeat reads until received is zero.
 * }
 * ```
 */
ArcResult arc_reader_read(ArcReader *reader, void *buffer, size_t capacity, size_t *received);

/**
 * Skip the current record's remaining body without advancing its metadata.
 *
 * - `reader`: Non-NULL active reader positioned on a current record.
 *
 * Requires a current record. Repeated successful skip is a no-op; read then
 * returns zero. Skipping does not promise integrity verification. May perform
 * I/O/decompression and fail; stream failures invalidate the reader.
 *
 * Example (reader is positioned on a record by a successful next call):
 * ```c
 * ArcResult result = arc_reader_skip(reader);
 * ```
 */
ArcResult arc_reader_skip(ArcReader *reader);

/**
 * Close/release a sequential reader, without draining unread records.
 * - `reader`: Non-NULL address of the owned pointer; set to NULL even on failure.
 * A NULL pointer value is a successful no-op. Returns the first stream failure
 * if one was retained, otherwise reports cleanup/conversion-context failure.
 * Successful early close does not verify the archive's remaining content.
 * After stream failure only close is usable; other well-formed calls return INVALID_STATE.
 *
 * Example (reader is an owned ArcReader * or NULL):
 * ```c
 * ArcResult result = arc_reader_close(&reader);
 * // reader is now NULL; check result for retained stream or cleanup errors.
 * ```
 */
ArcResult arc_reader_close(ArcReader **reader);

/**
 * Read the size of an ordinary file without validating archive contents.
 *
 * - `archive_path`: Non-NULL UTF-8 path of an existing ordinary file.
 * - `bytes`: Non-NULL output size; unchanged on failure.
 *
 * Use after successful close to obtain the completed archive's disk size.
 *
 * Example:
 * ```c
 * uint64_t bytes = 0;
 * ArcResult result = arc_get_size("backup.tar", &bytes);
 * ```
 */
ArcResult arc_get_size(const char *archive_path, uint64_t *bytes);

/**
 * Inspect an archive sequentially without extracting files to disk.
 *
 * - `archive_path`: Non-NULL UTF-8 input path; input must remain unchanged.
 * - `info`: Non-NULL statistics output; unchanged on failure.
 *
 * Counts ordinary data-bearing files, directories and both link types,
 * including repeated names. Sums ordinary data-bearing file sizes only.
 * Unknown ordinary-file sizes, unsupported special types and overflow
 * return errors. Does not promise full payload verification
 * or that the archive can safely be extracted on this filesystem.
 *
 * Example:
 * ```c
 * ArcInfo info = {0};
 * ArcResult result = arc_inspect("backup.tar", &info);
 * ```
 */
ArcResult arc_inspect(const char *archive_path, ArcInfo *info);

#ifdef __cplusplus
}
#endif

#endif
