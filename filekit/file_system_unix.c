#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "filekit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "filekit.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* A trailing slash makes POSIX resolve a directory symlink even with no-follow
 * flags. Remove only trailing separators, retaining the root and dot components.
 */
static FilekitResult object_path(const char *path, const char **out, char **owned) {
    FilekitResult r = fk_check_path(path);
    if (fk_failed(r))
        return r;
    size_t length = strlen(path), trimmed = length;
    while (trimmed > 1 && path[trimmed - 1] == '/')
        --trimmed;
    *owned = NULL;
    *out = path;
    if (trimmed != length) {
        char *copy = malloc(trimmed + 1);
        if (!copy)
            return fk_result(FILEKIT_OUT_OF_MEMORY, "Cannot allocate path.");
        memcpy(copy, path, trimmed);
        copy[trimmed] = 0;
        *owned = copy;
        *out = copy;
    }
    return fk_okay();
}

FilekitResult filekit_info(const char *path, FilekitInfo *out) {
    if (!out)
        return fk_result(FILEKIT_INVALID_ARGUMENT, "Missing info output.");
    const char *native_path;
    char *owned;
    FilekitResult checked_path = object_path(path, &native_path, &owned);
    if (fk_failed(checked_path))
        return checked_path;
    FilekitInfo i = {0};
    struct stat s;
    if (lstat(native_path, &s) < 0) {
        if (errno == ENOENT) {
            free(owned);
            *out = i;
            return fk_okay();
        }
        FilekitResult r = fk_system_error("Inspect", path);
        free(owned);
        return r;
    }
    free(owned);
    i.exists = 1;
    i.directory = S_ISDIR(s.st_mode);
    i.symbolic = S_ISLNK(s.st_mode);
    i.identity.key[0] = (uint64_t)s.st_dev;
    i.identity.key[1] = (uint64_t)s.st_ino;
    i.mode = (unsigned)s.st_mode & 0777;
    if (!i.directory && !i.symbolic && !S_ISREG(s.st_mode))
        return fk_result(FILEKIT_UNSUPPORTED, "Unsupported filesystem object '%s'.", path);
    if (s.st_size < 0)
        return fk_result(FILEKIT_OVERFLOW, "Invalid file size '%s'.", path);
    i.size = (uint64_t)s.st_size;
    *out = i;
    return fk_okay();
}

int filekit_same_identity(FilekitInfo a, FilekitInfo b) {
    return a.exists && b.exists && a.identity.key[0] == b.identity.key[0] &&
           a.identity.key[1] == b.identity.key[1];
}

FilekitResult filekit_create_file_exclusive(const char *path, unsigned mode, int *out) {
    if (!out)
        return fk_result(FILEKIT_INVALID_ARGUMENT, "Missing descriptor output.");
    FilekitResult checked_path = fk_check_path(path);
    if (fk_failed(checked_path))
        return checked_path;
    int fd;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, (mode_t)(mode & 0777));
    if (fd < 0)
        return fk_system_error("Open", path);
    *out = fd;
    return fk_okay();
}

FilekitResult filekit_close(int fd, const char *path) {
    if (fd < 0)
        return fk_result(FILEKIT_INVALID_ARGUMENT, "Invalid descriptor.");
    if (close(fd) < 0)
        return fk_system_error("Close", path);
    return fk_okay();
}

FilekitResult filekit_write_all(int fd, const void *data, size_t n) {
    if (fd < 0 || (!data && n))
        return fk_result(FILEKIT_INVALID_ARGUMENT, "Invalid write arguments.");
    const unsigned char *p = data;
    while (n) {
        ssize_t done = write(fd, p, n);
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0)
            return fk_system_error("Write", "");
        p += done;
        n -= (size_t)done;
    }
    return fk_okay();
}

FilekitResult filekit_remove_file(const char *path) {
    FilekitResult checked_path = fk_check_path(path);
    if (fk_failed(checked_path))
        return checked_path;
    if (unlink(path) < 0)
        return fk_system_error("Remove", path);
    return fk_okay();
}

FilekitResult filekit_create_directory_exclusive(const char *path, unsigned mode) {
    FilekitResult checked_path = fk_check_path(path);
    if (fk_failed(checked_path))
        return checked_path;
    if (mkdir(path, (mode_t)(mode & 0777)) < 0)
        return fk_system_error("Create directory", path);
    return fk_okay();
}

FilekitResult filekit_restrict_permissions(const char *path, unsigned allowed_mode) {
    const char *native_path;
    char *owned;
    FilekitResult checked_path = object_path(path, &native_path, &owned);
    if (fk_failed(checked_path))
        return checked_path;
    FilekitInfo i = {0};
    FilekitResult r = filekit_info(native_path, &i);
    if (!fk_failed(r)) {
        if (!i.exists || i.symbolic)
            r = fk_result(FILEKIT_INVALID_PATH, "Expected ordinary file or directory.");
        else if (chmod(native_path, (mode_t)(i.mode & allowed_mode & 0777)) < 0)
            r = fk_system_error("Set permissions", path);
    }
    free(owned);
    return r;
}

FilekitResult filekit_set_mtime(const char *path, int64_t seconds, long nanoseconds) {
    if (nanoseconds < 0 || nanoseconds > 999999999L)
        return fk_result(FILEKIT_INVALID_ARGUMENT, "Invalid nanoseconds.");
    FilekitResult checked_path = fk_check_path(path);
    if (fk_failed(checked_path))
        return checked_path;
    time_t native = (time_t)seconds;
    if ((int64_t)native != seconds)
        return fk_result(FILEKIT_OVERFLOW, "Timestamp exceeds platform range.");
    struct timespec times[2] = {{0, UTIME_OMIT}, {native, nanoseconds}};
    const char *native_path;
    char *owned;
    FilekitResult r = object_path(path, &native_path, &owned);
    if (fk_failed(r))
        return r;
    if (utimensat(AT_FDCWD, native_path, times, AT_SYMLINK_NOFOLLOW) < 0)
        r = fk_system_error("Set modification time", path);
    free(owned);
    return r;
}

FilekitResult filekit_create_hard_link(const char *target, const char *name) {
    FilekitResult checked_target = fk_check_path(target);
    if (fk_failed(checked_target))
        return checked_target;
    FilekitResult checked_name = fk_check_path(name);
    if (fk_failed(checked_name))
        return checked_name;
    if (link(target, name) < 0)
        return fk_system_error("Create hard link", name);
    return fk_okay();
}

FilekitResult filekit_create_symbolic_link(const char *full, const char *link_target,
                                           FilekitLinkKind kind) {
    if (kind < FILEKIT_LINK_UNKNOWN || kind > FILEKIT_LINK_DIRECTORY)
        return fk_result(FILEKIT_INVALID_ARGUMENT, "Invalid link kind.");
    FilekitResult checked_full = fk_check_path(full);
    if (fk_failed(checked_full))
        return checked_full;
    FilekitResult checked_link_target = fk_check_path(link_target);
    if (fk_failed(checked_link_target))
        return checked_link_target;
    (void)kind;
    if (symlink(link_target, full) < 0)
        return fk_system_error("Create symbolic link", full);
    return fk_okay();
}

int filekit_compare_identity(FilekitIdentity a, FilekitIdentity b) {
    for (size_t i = 0; i < ARRAY_COUNT(a.key); ++i)
        if (a.key[i] != b.key[i])
            return a.key[i] < b.key[i] ? -1 : 1;
    return 0;
}

FilekitResult filekit_absolute_path(const char *path, char **out) {
    if (!out)
        return fk_result(FILEKIT_INVALID_ARGUMENT, "Missing path output.");
    FilekitResult checked_path = fk_check_path(path);
    if (fk_failed(checked_path))
        return checked_path;
    if (*path == '/')
        return fk_copy_string(path, out);
    size_t capacity = 256;
    for (;;) {
        char *cwd = malloc(capacity);
        if (!cwd)
            return fk_result(FILEKIT_OUT_OF_MEMORY, "Cannot allocate working directory path.");
        if (getcwd(cwd, capacity)) {
            FilekitResult r = fk_valid_utf8(cwd) ? fk_join(cwd, path, out)
                                                 : fk_result(FILEKIT_INVALID_PATH,
                                                             "Working directory is not UTF-8.");
            free(cwd);
            return r;
        }
        int error = errno;
        free(cwd);
        if (error != ERANGE) {
            errno = error;
            return fk_system_error("Resolve output", path);
        }
        if (capacity > SIZE_MAX / 2)
            return fk_result(FILEKIT_OVERFLOW, "Working directory path is too long.");
        capacity *= 2;
    }
}
