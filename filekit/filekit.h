#ifndef FILEKIT_H
#define FILEKIT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Independent errors; diagnostics are owned, bounded, and never parsed by callers. */
typedef enum {
    FILEKIT_OK = 0,
    FILEKIT_INVALID_ARGUMENT,
    FILEKIT_INVALID_PATH,
    FILEKIT_ALREADY_EXISTS,
    FILEKIT_UNSUPPORTED,
    FILEKIT_OVERFLOW,
    FILEKIT_OUT_OF_MEMORY,
    FILEKIT_IO_ERROR
} FilekitCode;

typedef struct {
    FilekitCode code;
    char message[256];
} FilekitResult;

/** Opaque native identity value. Only compare identities from the same host. */
typedef struct {
    uint64_t key[2];
} FilekitIdentity;

typedef struct {
    FilekitIdentity identity;
    uint64_t size;
    unsigned mode;
    int exists, directory, symbolic;
} FilekitInfo;

typedef enum {
    FILEKIT_LINK_UNKNOWN = 0,
    FILEKIT_LINK_FILE = 1,
    FILEKIT_LINK_DIRECTORY = 2
} FilekitLinkKind;

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
 * FilekitInfo info = {0};
 * FilekitResult result = filekit_info("hello.txt", &info);
 * ```
 */
FilekitResult filekit_info(const char *path, FilekitInfo *out);
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
 * // first and second are FilekitInfo values inspected on this host.
 * int same = filekit_same_identity(first, second);
 * ```
 */
int filekit_same_identity(FilekitInfo a, FilekitInfo b);
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
 * // first and second are FilekitInfo values inspected on this host.
 * int order = filekit_compare_identity(first.identity, second.identity);
 * ```
 */
int filekit_compare_identity(FilekitIdentity a, FilekitIdentity b);
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
 * FilekitResult result = filekit_absolute_path("hello.txt", &absolute);
 * if (result.code == FILEKIT_OK) {
 *     free(absolute); // Include <stdlib.h>.
 * }
 * ```
 */
FilekitResult filekit_absolute_path(const char *path, char **out);
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
 * FilekitResult result = filekit_create_file_exclusive("hello.txt", 0644, &fd);
 * if (result.code == FILEKIT_OK) {
 *     result = filekit_close(fd, "hello.txt");
 * }
 * ```
 */
FilekitResult filekit_create_file_exclusive(const char *path, unsigned mode, int *out);
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
 * FilekitResult result = filekit_write_all(fd, "hello", 5);
 * ```
 */
FilekitResult filekit_write_all(int fd, const void *data, size_t n);
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
 * FilekitResult result = filekit_close(fd, "hello.txt");
 * ```
 */
FilekitResult filekit_close(int fd, const char *path);
/**
 * Remove a file or a file symbolic link.
 *
 * - `path`: UTF-8 path to remove; directories are not removed recursively.
 *
 * Remove a file or file symlink; does not recursively remove directories.
 *
 * Example:
 * ```c
 * FilekitResult result = filekit_remove_file("hello.txt");
 * ```
 */
FilekitResult filekit_remove_file(const char *path);
/**
 * Create a directory without reusing an existing object.
 *
 * - `path`: New UTF-8 directory path; parent must exist.
 * - `mode`: Requested POSIX permission bits subject to umask; Windows uses inherited ACLs.
 *
 * Example:
 * ```c
 * FilekitResult result = filekit_create_directory_exclusive("docs", 0755);
 * ```
 */
FilekitResult filekit_create_directory_exclusive(const char *path, unsigned mode);
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
 * FilekitResult result = filekit_restrict_permissions("hello.txt", 0600);
 * ```
 */
FilekitResult filekit_restrict_permissions(const char *path, unsigned allowed_mode);
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
 * FilekitResult result = filekit_set_mtime("hello.txt", 1700000000, 0);
 * ```
 */
FilekitResult filekit_set_mtime(const char *path, int64_t seconds, long nanoseconds);
/**
 * Create a hard link to an existing file.
 *
 * - `target`: UTF-8 path of the existing target file.
 * - `name`: New UTF-8 link path; parent must exist.
 *
 * Example:
 * ```c
 * FilekitResult result = filekit_create_hard_link("hello.txt", "alias.txt");
 * ```
 */
FilekitResult filekit_create_hard_link(const char *target, const char *name);
/**
 * Create a symbolic link containing the supplied target text.
 *
 * - `name`: New UTF-8 link path; parent must exist.
 * - `target`: UTF-8 link text; not resolved by filekit.
 * - `kind`: Target kind required on Windows; ignored on POSIX.
 *
 * Target is UTF-8 link text, never resolved by filekit. Windows requires kind
 * FILE or DIRECTORY and may require privileges; POSIX ignores kind.
 *
 * Example:
 * ```c
 * FilekitResult result = filekit_create_symbolic_link("alias.txt", "hello.txt",
 *                                                    FILEKIT_LINK_FILE);
 * ```
 */
FilekitResult filekit_create_symbolic_link(const char *name, const char *target,
                                           FilekitLinkKind kind);
#ifdef __cplusplus
}
#endif
#endif
