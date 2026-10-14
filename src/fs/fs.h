/*
 * Internal filesystem module for Arc.
 *
 * Provides UTF-8 path operations, file metadata and identity, exclusive
 * creation, links, and metadata updates through native platform APIs.
 * It has no archive-format knowledge and does not change the locale.
 *
 * This header is shared by Arc implementation modules and is not part
 * of the installed public API. Individual operations define ownership
 * and error-handling requirements below.
 */
#ifndef ARC_FS_H
#define ARC_FS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Independent errors; diagnostics are owned, bounded, and never parsed by callers. */
typedef enum {
    ARC_FS_OK = 0,
    ARC_FS_INVALID_ARGUMENT,
    ARC_FS_INVALID_PATH,
    ARC_FS_ALREADY_EXISTS,
    ARC_FS_UNSUPPORTED,
    ARC_FS_OVERFLOW,
    ARC_FS_OUT_OF_MEMORY,
    ARC_FS_IO_ERROR
} ArcFsCode;

typedef struct {
    ArcFsCode code;
    char message[256];
} ArcFsResult;

/** Opaque native identity value. Only compare identities from the same host. */
typedef struct {
    uint64_t key[2];
} ArcFsIdentity;

typedef struct {
    ArcFsIdentity identity;
    uint64_t size;
    unsigned mode;
    int exists, directory, symbolic;
} ArcFsInfo;

typedef enum {
    ARC_FS_LINK_UNKNOWN = 0,
    ARC_FS_LINK_FILE = 1,
    ARC_FS_LINK_DIRECTORY = 2
} ArcFsLinkKind;

/** All paths are nonempty UTF-8. Outputs remain unchanged on failure.
 * Returned strings must be freed with free(). No operation changes locale.
 * Path operations do not prevent concurrent replacement of ancestor directories.
 * POSIX inspection, permission restriction and mtime updates ignore trailing
 * slashes so they still operate on the final link itself; root / is preserved.
 */
/**
 * Inspect an object without following the final symbolic link.
 *
 * - `path`: UTF-8 path to inspect.
 * - `out`: Non-NULL metadata output; missing objects have exists == 0.
 *
 * Other native object types (including Windows junctions) are unsupported.
 * POSIX mode is permission bits; Windows mode is synthetic, not an ACL.
 *
 * Example:
 * ```c
 * ArcFsInfo info = {0};
 * ArcFsResult result = arc_fs_info("hello.txt", &info);
 * ```
 */
ArcFsResult arc_fs_info(const char *path, ArcFsInfo *out);
/**
 * Check whether two existing objects have the same native identity.
 *
 * - `a`: First inspected object.
 * - `b`: Second inspected object from the same host.
 *
 * Returns nonzero only when both objects exist and their identities match.
 *
 * Example:
 * ```c
 * // first and second are ArcFsInfo values inspected on this host.
 * int same = arc_fs_same_identity(first, second);
 * ```
 */
int arc_fs_same_identity(ArcFsInfo a, ArcFsInfo b);
/**
 * Order two native identities on the same host.
 *
 * - `a`: First identity.
 * - `b`: Second identity.
 *
 * Returns a negative value, zero or a positive value for less, equal or greater.
 *
 * Example:
 * ```c
 * // first and second are ArcFsInfo values inspected on this host.
 * int order = arc_fs_compare_identity(first.identity, second.identity);
 * ```
 */
int arc_fs_compare_identity(ArcFsIdentity a, ArcFsIdentity b);
/**
 * Anchor a path to the current directory.
 *
 * - `path`: UTF-8 path to anchor.
 * - `out`: Non-NULL output for an owned string; release with free().
 *
 * Does not resolve links or require existence.
 * Windows returns an extended absolute path. POSIX preserves dot components.
 *
 * Example:
 * ```c
 * char *absolute = NULL;
 * ArcFsResult result = arc_fs_absolute_path("hello.txt", &absolute);
 * if (result.code == ARC_FS_OK) {
 *     free(absolute); // Include <stdlib.h>.
 * }
 * ```
 */
ArcFsResult arc_fs_absolute_path(const char *path, char **out);
/**
 * Create a writable file without overwriting an existing object.
 *
 * - `path`: New UTF-8 file path; parent must exist.
 * - `mode`: Requested POSIX permission bits; Windows uses inherited ACLs.
 * - `out`: Non-NULL output for an owned descriptor; close after success.
 *
 * Caller owns the writable CRT/POSIX fd. A failure after native creation rolls
 * back the new file; if removal fails, the diagnostic identifies the leftover.
 * Existing objects are never removed during rollback.
 * POSIX applies mode & 0777 through umask; Windows uses inherited ACLs.
 *
 * Example:
 * ```c
 * int fd;
 * ArcFsResult result = arc_fs_create_file_exclusive("hello.txt", 0644, &fd);
 * if (result.code == ARC_FS_OK) {
 *     result = arc_fs_close(fd, "hello.txt");
 * }
 * ```
 */
ArcFsResult arc_fs_create_file_exclusive(const char *path, unsigned mode, int *out);
/**
 * Write all supplied bytes to an open descriptor.
 *
 * - `fd`: Writable caller-owned descriptor.
 * - `data`: Borrowed bytes; NULL is allowed only when n is zero.
 * - `n`: Number of bytes to write.
 *
 * Accept all bytes; failure can leave partial output. NULL data allowed for n=0.
 *
 * Example:
 * ```c
 * // fd is an open writable descriptor.
 * ArcFsResult result = arc_fs_write_all(fd, "hello", 5);
 * ```
 */
ArcFsResult arc_fs_write_all(int fd, const void *data, size_t n);
/**
 * Close and consume a descriptor, including on failure.
 *
 * - `fd`: Owned descriptor; do not retry after this call.
 * - `path`: UTF-8 path used as diagnostic context.
 *
 * Consumes fd even on close failure; do not retry. path is diagnostic context.
 *
 * Example:
 * ```c
 * // fd is an owned open descriptor.
 * ArcFsResult result = arc_fs_close(fd, "hello.txt");
 * ```
 */
ArcFsResult arc_fs_close(int fd, const char *path);
/**
 * Remove a file or a file symbolic link.
 *
 * - `path`: UTF-8 path to remove; directories are not removed recursively.
 *
 * Remove a file or file symlink; does not recursively remove directories.
 *
 * Example:
 * ```c
 * ArcFsResult result = arc_fs_remove_file("hello.txt");
 * ```
 */
ArcFsResult arc_fs_remove_file(const char *path);
/**
 * Create a directory without reusing an existing object.
 *
 * - `path`: New UTF-8 directory path; parent must exist.
 * - `mode`: Requested POSIX permission bits subject to umask; Windows uses inherited ACLs.
 *
 * Example:
 * ```c
 * ArcFsResult result = arc_fs_create_directory_exclusive("docs", 0755);
 * ```
 */
ArcFsResult arc_fs_create_directory_exclusive(const char *path, unsigned mode);
/**
 * Remove disallowed POSIX permission bits from a file or directory.
 *
 * - `path`: UTF-8 path controlled by the caller, including its parents.
 * - `allowed_mode`: Permission bits allowed to remain; never adds permissions.
 *
 * POSIX: remove permission bits from an ordinary file/directory; rejects a
 * final symbolic link. Ancestor links are followed. Windows: UNSUPPORTED. Caller must control the
 * path and its parents.
 *
 * Example:
 * ```c
 * ArcFsResult result = arc_fs_restrict_permissions("hello.txt", 0600);
 * ```
 */
ArcFsResult arc_fs_restrict_permissions(const char *path, unsigned allowed_mode);
/**
 * Set modification time without following the final symbolic link.
 *
 * - `path`: UTF-8 path to update.
 * - `seconds`: Seconds since the Unix epoch.
 * - `nanoseconds`: Subsecond component in [0, 999999999].
 *
 * Set modification time without following the final link; seconds since epoch,
 * nanoseconds in [0, 999999999]. Windows precision is 100 ns.
 *
 * Example:
 * ```c
 * ArcFsResult result = arc_fs_set_mtime("hello.txt", 1700000000, 0);
 * ```
 */
ArcFsResult arc_fs_set_mtime(const char *path, int64_t seconds, long nanoseconds);
/**
 * Create a hard link to an existing file.
 *
 * - `target`: UTF-8 path of the existing target file.
 * - `name`: New UTF-8 link path; parent must exist.
 *
 * Example:
 * ```c
 * ArcFsResult result = arc_fs_create_hard_link("hello.txt", "alias.txt");
 * ```
 */
ArcFsResult arc_fs_create_hard_link(const char *target, const char *name);
/**
 * Create a symbolic link containing the supplied target text.
 *
 * - `name`: New UTF-8 link path; parent must exist.
 * - `target`: UTF-8 link text; not resolved by fs.
 * - `kind`: Target kind required on Windows; ignored on POSIX.
 *
 * Target is UTF-8 link text, never resolved by fs. Windows requires kind
 * FILE or DIRECTORY and may require privileges; POSIX ignores kind.
 *
 * Example:
 * ```c
 * ArcFsResult result = arc_fs_create_symbolic_link("alias.txt", "hello.txt",
 *                                                    ARC_FS_LINK_FILE);
 * ```
 */
ArcFsResult arc_fs_create_symbolic_link(const char *name, const char *target, ArcFsLinkKind kind);
#ifdef __cplusplus
}
#endif
#endif
