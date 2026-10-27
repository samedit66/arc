#ifndef LIBARCHIVE_WRAPPER_H
#define LIBARCHIVE_WRAPPER_H

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * POSIX prototype: scope UTF-8 LC_CTYPE to one native call and restore the
 * caller's thread locale before returning to Eiffel. No global setlocale.
 * The failure statement returns from the enclosing primitive. Allocation
 * failure and inability to restore thread state are fatal internal failures.
 */
#define LIBARCHIVE_WRAPPER_ENTER(handle, failure) \
    locale_t wrapper_locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0); \
    locale_t wrapper_previous; \
    if (!wrapper_locale) { \
        if (errno == ENOMEM) abort(); \
        wrapper_locale = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0); \
    } \
    if (!wrapper_locale) { \
        int wrapper_code = errno; \
        if (wrapper_code == ENOMEM) abort(); \
        archive_set_error((handle), wrapper_code, "Cannot create UTF-8 locale"); \
        failure; \
    } \
    wrapper_previous = uselocale(wrapper_locale); \
    if (!wrapper_previous) abort()

/** Restore the caller's locale and release the temporary locale. */
#define LIBARCHIVE_WRAPPER_LEAVE() \
    do { \
        if (!uselocale(wrapper_previous)) abort(); \
        freelocale(wrapper_locale); \
    } while (0)

/**
 * Open one ordinary POSIX source file, fill caller-owned entry metadata,
 * and leave the archive pathname and header to Eiffel. Return an owned FILE*
 * or NULL with errno diagnostics in writer. A failed open retains no FILE*.
 * Reset cleanup_code to zero; store any separate fclose errno there.
 */
static FILE *libarchive_wrapper_source_open(struct archive *writer,
    struct archive_entry *entry, const void *source_path, int32_t *cleanup_code)
{
    FILE *source;
    *cleanup_code = 0;
    source = fopen((const char *)source_path, "rb");
    struct stat info;
    int code;
    if (!source) {
        code = errno;
        if (code == ENOMEM) abort();
        archive_set_error(writer, code, "Cannot open source");
        return NULL;
    }
    errno = 0;
    if (fstat(fileno(source), &info) != 0 || !S_ISREG(info.st_mode)) {
        code = errno ? errno : EINVAL;
        if (fclose(source) != 0) {
            *cleanup_code = (int32_t)errno;
            if (*cleanup_code == ENOMEM) abort();
        }
        archive_set_error(writer, code, "Source must be an ordinary readable file");
        if (code == ENOMEM) abort();
        return NULL;
    }
    archive_entry_copy_stat(entry, &info);
    return source;
}

/**
 * Transfer one source block without copying through Eiffel. Return
 * ARCHIVE_OK, ARCHIVE_EOF, or a libarchive failure status. Set byte count
 * only after a complete write; reset it to zero otherwise. Diagnostics belong
 * to writer. Do not finish the entry or close the source. Short writes fail.
 */
static int libarchive_wrapper_pack_block(struct archive *writer, FILE *source,
    uint64_t *written_bytes)
{
    char buffer[65536];
    size_t count;
    la_ssize_t written;
    *written_bytes = 0;
    errno = 0;
    count = fread(buffer, 1, sizeof(buffer), source);
    if (ferror(source)) {
        int code = errno ? errno : EIO;
        if (code == ENOMEM) abort();
        archive_set_error(writer, code, "Cannot read source");
        return ARCHIVE_FAILED;
    }
    if (!count) return ARCHIVE_EOF;
    {
        LIBARCHIVE_WRAPPER_ENTER(writer, return ARCHIVE_FATAL);
        written = archive_write_data(writer, buffer, count);
        LIBARCHIVE_WRAPPER_LEAVE();
    }
    if (written < 0) {
        if (archive_errno(writer) == ENOMEM) abort();
        return (int)written;
    }
    if ((size_t)written != count) {
        archive_set_error(writer, EIO, "Short archive write");
        return ARCHIVE_FAILED;
    }
    *written_bytes = (uint64_t)count;
    return ARCHIVE_OK;
}

/** Consume an owned FILE*, returning ARCHIVE_OK or diagnostics in writer. */
static int libarchive_wrapper_source_close(struct archive *writer, FILE *source)
{
    if (fclose(source) != 0) {
        int code = errno;
        if (code == ENOMEM) abort();
        archive_set_error(writer, code, "Cannot close source");
        return ARCHIVE_FAILED;
    }
    return ARCHIVE_OK;
}

/* Read and discard one current-entry block without copying or writing it. */
static int libarchive_wrapper_consume_block(struct archive *reader)
{
    const void *buffer;
    size_t size;
    la_int64_t offset;
    int status;
    LIBARCHIVE_WRAPPER_ENTER(reader, return ARCHIVE_FATAL);
    status = archive_read_data_block(reader, &buffer, &size, &offset);
    LIBARCHIVE_WRAPPER_LEAVE();
    if (status != ARCHIVE_OK && status != ARCHIVE_EOF && archive_errno(reader) == ENOMEM) abort();
    return status;
}

/**
 * Transfer one current-entry block, preserving its sparse offset. Return
 * ARCHIVE_OK, ARCHIVE_EOF, or a failure status. Reset byte count on every call
 * and publish it only after a successful write. Copy disk-writer errors into
 * reader before returning. Do not finish the entry or advance the reader.
 */
static int libarchive_wrapper_extract_block(struct archive *reader,
    struct archive *disk, uint64_t *written_bytes)
{
    const void *buffer;
    size_t size;
    la_int64_t offset;
    int status;
    *written_bytes = 0;
    {
        LIBARCHIVE_WRAPPER_ENTER(reader, return ARCHIVE_FATAL);
        status = archive_read_data_block(reader, &buffer, &size, &offset);
        if (status == ARCHIVE_OK) {
            status = (int)archive_write_data_block(disk, buffer, size, offset);
            if (status != ARCHIVE_OK) {
                int code = archive_errno(disk);
                const char *message = archive_error_string(disk);
                if (code == ENOMEM) abort();
                archive_set_error(reader, code, "%s", message ? message : "Disk write failed");
            } else *written_bytes = (uint64_t)size;
        }
        LIBARCHIVE_WRAPPER_LEAVE();
    }
    if (status != ARCHIVE_OK && status != ARCHIVE_EOF && archive_errno(reader) == ENOMEM) abort();
    return status;
}
/**
 * Create an owned POSIX descriptor exclusively, without truncating any existing
 * file or symlink. Return the descriptor or -1 and capture errno separately.
 * Ownership, finalization and removal policy belong to Eiffel.
 */
static int libarchive_wrapper_create_output(const void *path, int32_t *error_code)
{
    int fd;
    *error_code = 0;
    fd = open((const char *)path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        *error_code = (int32_t)errno;
        if (*error_code == ENOMEM) abort();
    }
    return fd;
}

/** Consume an output descriptor; return zero or the captured closing errno. */
static int libarchive_wrapper_close_output(int fd)
{
    if (close(fd) != 0) {
        int code = errno;
        if (code == ENOMEM) abort();
        return code;
    }
    return 0;
}

/** Remove a caller-selected incomplete output; return zero or captured errno. */
static int libarchive_wrapper_remove_output(const void *path)
{
    if (unlink((const char *)path) != 0) {
        int code = errno;
        if (code == ENOMEM) abort();
        return code;
    }
    return 0;
}
/** Open one directory; return an owned DIR* or NULL with captured errno. */
static DIR *libarchive_wrapper_directory_open(const void *path, int32_t *error_code)
{
    DIR *directory;
    *error_code = 0;
    directory = opendir((const char *)path);
    if (!directory) {
        *error_code = (int32_t)errno;
        if (*error_code == ENOMEM) abort();
    }
    return directory;
}

/**
 * Read one directory name, borrowed until the next read or close.
 * NULL with zero errno is normal EOF; NULL with nonzero errno is failure.
 * Enumeration order and recursion are deliberately left to Eiffel.
 */
static const char *libarchive_wrapper_directory_next(DIR *directory, int32_t *error_code)
{
    struct dirent *entry;
    *error_code = 0;
    errno = 0;
    entry = readdir(directory);
    if (!entry) {
        *error_code = (int32_t)errno;
        if (*error_code == ENOMEM) abort();
        return NULL;
    }
    return entry->d_name;
}

/** Consume one owned DIR* and return zero or the captured closing errno. */
static int libarchive_wrapper_directory_close(DIR *directory)
{
    if (closedir(directory) != 0) {
        int code = errno;
        if (code == ENOMEM) abort();
        return code;
    }
    return 0;
}
/**
 * Exclusively create an extraction directory and return an owned realpath string.
 * NULL reports mkdir/realpath errno separately. Eiffel frees the returned string.
 * Resolving parent aliases permits the disk writer's secure-symlink checks.
 */
static char *libarchive_wrapper_create_directory(const void *path, int32_t *error_code)
{
    char *resolved;
    *error_code = 0;
    if (mkdir((const char *)path, 0777) != 0) {
        *error_code = (int32_t)errno;
        if (*error_code == ENOMEM) abort();
        return NULL;
    }
    resolved = realpath((const char *)path, NULL);
    if (!resolved) {
        *error_code = (int32_t)errno;
        if (*error_code == ENOMEM) abort();
    }
    return resolved;
}
#endif
