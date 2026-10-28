#ifndef LIBARCHIVE_WRAPPER_H
#define LIBARCHIVE_WRAPPER_H

#if !defined(ARC_SYSTEM_LIBARCHIVE) && !defined(ARC_CONAN_LIBARCHIVE)
#error "Select ARC_NATIVE_MODE=system or conan"
#endif
#if defined(ARC_SYSTEM_LIBARCHIVE) && defined(ARC_CONAN_LIBARCHIVE)
#error "System and Conan libarchive configurations must not be mixed"
#endif
#if defined(_WIN32) && !defined(LIBARCHIVE_STATIC) && !defined(_DLL)
#error "A libarchive DLL requires shared CRT (/MD); use ARC_SYSTEM_CRT=md or a compatible static library"
#endif

#include <archive.h>
#include <archive_entry.h>
#if ARCHIVE_VERSION_NUMBER < 3008007
#error "Arc requires libarchive 3.8.7 or later"
#endif
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <wchar.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* Positive codes are CRT/POSIX errno; negative codes retain original Win32 errors. */
#ifdef _WIN32
static int libarchive_wrapper_windows_code(DWORD code)
{
    if (code == ERROR_NOT_ENOUGH_MEMORY || code == ERROR_OUTOFMEMORY) abort();
    return -(int)code;
}

static const char *libarchive_wrapper_system_message(int code)
{
    static __declspec(thread) char message[4096];
    wchar_t wide[1024];
    DWORD count;
    int bytes;
    if (code >= 0) {
        const wchar_t *description = _wcserror(code);
        if (!description) abort();
        count = (DWORD)wcslen(description);
        bytes = WideCharToMultiByte(CP_UTF8, 0, description, (int)count,
            message, sizeof(message) - 1, NULL, NULL);
    } else {
        count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, (DWORD)(-code), 0, wide, 1024, NULL);
        bytes = count ? WideCharToMultiByte(CP_UTF8, 0, wide, (int)count,
            message, sizeof(message) - 1, NULL, NULL) : 0;
    }
    if (bytes) message[bytes] = 0;
    else {
        DWORD failure = GetLastError();
        if (failure == ERROR_NOT_ENOUGH_MEMORY || failure == ERROR_OUTOFMEMORY) abort();
        if (snprintf(message, sizeof(message), "System error %d", code) < 0)
            strcpy(message, "Unknown system error");
    }
    return message;
}

struct libarchive_wrapper_locale {
    int previous_mode;
    char *previous_name;
};

static int libarchive_wrapper_enter_locale(struct archive *owner,
    struct libarchive_wrapper_locale *scope)
{
    const char *name = setlocale(LC_CTYPE, NULL);
    if (!name) abort();
    scope->previous_name = _strdup(name);
    if (!scope->previous_name) abort();
    scope->previous_mode = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
    if (scope->previous_mode == -1) abort();
    if (!setlocale(LC_CTYPE, ".UTF8")) {
        if (!setlocale(LC_CTYPE, scope->previous_name)) abort();
        if (_configthreadlocale(scope->previous_mode) == -1) abort();
        free(scope->previous_name);
        archive_set_error(owner, EINVAL, "Cannot create thread-local UTF-8 locale");
        return 0;
    }
    return 1;
}

#define LIBARCHIVE_WRAPPER_ENTER(handle, failure) \
    struct libarchive_wrapper_locale wrapper_locale; \
    if (!libarchive_wrapper_enter_locale((handle), &wrapper_locale)) { failure; }
#define LIBARCHIVE_WRAPPER_LEAVE() \
    do { \
        if (!setlocale(LC_CTYPE, wrapper_locale.previous_name)) abort(); \
        if (_configthreadlocale(wrapper_locale.previous_mode) == -1) abort(); \
        free(wrapper_locale.previous_name); \
    } while (0)
#else
static const char *libarchive_wrapper_system_message(int code)
{
    return strerror(code);
}

/**
 * POSIX scope: bind UTF-8 LC_CTYPE to one native call and restore the
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

#endif

static void libarchive_wrapper_set_native_name(struct archive_entry *entry, const void *path)
{
#ifdef _WIN32
    archive_entry_copy_pathname_w(entry, (const wchar_t *)path);
#else
    archive_entry_copy_pathname(entry, (const char *)path);
#endif
}

static int libarchive_wrapper_open_reader(struct archive *reader, const void *path)
{
    int status;
    LIBARCHIVE_WRAPPER_ENTER(reader, return ARCHIVE_FATAL);
    status = archive_read_support_filter_all(reader);
    if (status == ARCHIVE_OK) status = archive_read_support_format_all(reader);
    if (status == ARCHIVE_OK) {
#ifdef _WIN32
        status = archive_read_open_filename_w(reader, (const wchar_t *)path, 65536);
#else
        status = archive_read_open_filename(reader, (const char *)path, 65536);
#endif
    }
    LIBARCHIVE_WRAPPER_LEAVE();
    return status;
}

/* Windows libarchive caches paths before restoring links; inspect every parent afresh. */
static int libarchive_wrapper_write_extraction_header(struct archive *disk, struct archive_entry *entry)
{
    int status;
    LIBARCHIVE_WRAPPER_ENTER(disk, return ARCHIVE_FATAL);
#ifdef _WIN32
    const wchar_t *name = archive_entry_pathname_w(entry);
    DWORD capacity = GetFullPathNameW(name, 0, NULL, NULL);
    DWORD failure = capacity ? ERROR_SUCCESS : GetLastError();
    wchar_t *path = NULL;
    if (capacity) {
        path = (wchar_t *)malloc((size_t)capacity * sizeof(wchar_t));
        if (!path) abort();
        DWORD copied = GetFullPathNameW(name, capacity, path, NULL);
        if (!copied) failure = GetLastError();
        else if (copied >= capacity) failure = ERROR_INSUFFICIENT_BUFFER;
    }
    if (!failure) {
        wchar_t *parent = path;
        if (wcsncmp(parent, L"\\\\?\\", 4) == 0) parent += 4;
        if (wcsncmp(parent, L"UNC\\", 4) == 0 || wcsncmp(parent, L"\\\\", 2) == 0) {
            parent += parent[0] == L'U' ? 4 : 2;
            for (int part = 0; part < 2; ++part) {
                while (*parent && *parent != L'\\') ++parent;
                if (*parent) ++parent;
            }
        } else if (parent[0] && parent[1] == L':' && parent[2] == L'\\') parent += 3;

        for (; *parent; ++parent) {
            if (*parent != L'\\') continue;
            *parent = 0;
            DWORD attributes = GetFileAttributesW(path);
            DWORD code = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
            *parent = L'\\';
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                failure = ERROR_CANT_ACCESS_FILE;
                break;
            }
            if (code && code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
                failure = code;
                break;
            }
        }
    }
    free(path);
    if (failure) {
        int code = libarchive_wrapper_windows_code(failure);
        archive_set_error(disk, code, "Cannot access extraction parents: %s", libarchive_wrapper_system_message(code));
        LIBARCHIVE_WRAPPER_LEAVE();
        return ARCHIVE_FAILED;
    }
#endif
    status = archive_write_header(disk, entry);
    LIBARCHIVE_WRAPPER_LEAVE();
    return status;
}

/* Compare a source with the owned output without reopening the output or denying its writes. */
#ifdef _WIN32
static int libarchive_wrapper_is_output_source(int descriptor, const void *path,
    int32_t *error_code, int32_t *cleanup_code)
{
    HANDLE source;
    BY_HANDLE_FILE_INFORMATION left, right;
    intptr_t output;
    int same = 0;
    *error_code = 0;
    *cleanup_code = 0;
    source = CreateFileW((const wchar_t *)path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (source == INVALID_HANDLE_VALUE) {
        *error_code = libarchive_wrapper_windows_code(GetLastError());
        return 0;
    }
    output = _get_osfhandle(descriptor);
    if (output == -1) {
        *error_code = errno;
        if (*error_code == ENOMEM) abort();
    } else if (!GetFileInformationByHandle(source, &left) ||
        !GetFileInformationByHandle((HANDLE)output, &right))
        *error_code = libarchive_wrapper_windows_code(GetLastError());
    else same = left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
        left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow;
    if (!CloseHandle(source)) {
        int code = libarchive_wrapper_windows_code(GetLastError());
        if (*error_code) *cleanup_code = code;
        else *error_code = code;
    }
    return same;
}
#else
static int libarchive_wrapper_is_output_source(int descriptor, const void *path,
    int32_t *error_code, int32_t *cleanup_code)
{
    struct stat source, output;
    *error_code = 0;
    *cleanup_code = 0;
    if (stat((const char *)path, &source) != 0 || fstat(descriptor, &output) != 0) {
        *error_code = errno;
        if (*error_code == ENOMEM) abort();
        return 0;
    }
    return source.st_dev == output.st_dev && source.st_ino == output.st_ino;
}
#endif

/* Classify without following symlinks or junctions; unsupported objects have type zero. */
static int libarchive_wrapper_source_type(const void *path, int32_t *error_code, int32_t *cleanup_code)
{
    *error_code = 0;
    *cleanup_code = 0;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesW((const wchar_t *)path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        *error_code = libarchive_wrapper_windows_code(GetLastError());
        return 0;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        FILE_ATTRIBUTE_TAG_INFO tag;
        HANDLE handle = CreateFileW((const wchar_t *)path, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
        int type = 0;
        if (handle == INVALID_HANDLE_VALUE) {
            *error_code = libarchive_wrapper_windows_code(GetLastError());
            return 0;
        }
        if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)))
            *error_code = libarchive_wrapper_windows_code(GetLastError());
        else if (tag.ReparseTag == IO_REPARSE_TAG_SYMLINK) type = AE_IFLNK;
        if (!CloseHandle(handle)) {
            int code = libarchive_wrapper_windows_code(GetLastError());
            if (*error_code) *cleanup_code = code;
            else *error_code = code;
        }
        return type;
    }
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? AE_IFDIR : AE_IFREG;
#else
    struct stat info;
    if (lstat((const char *)path, &info) != 0) {
        *error_code = errno;
        if (*error_code == ENOMEM) abort();
        return 0;
    }
    return info.st_mode & S_IFMT;
#endif
}

/**
 * Open one ordinary native source file, fill caller-owned entry metadata,
 * and leave the archive pathname and header to Eiffel. Return an owned FILE*
 * or NULL with errno diagnostics in writer. A failed open retains no FILE*.
 * Reset cleanup_code to zero; store any separate fclose errno there.
 */
static FILE *libarchive_wrapper_source_open(struct archive *writer,
    struct archive *metadata, struct archive_entry *entry, const void *source_path, int32_t *cleanup_code)
{
    FILE *source;
    *cleanup_code = 0;
#ifdef _WIN32
    source = _wfopen((const wchar_t *)source_path, L"rb");
#else
    source = fopen((const char *)source_path, "rb");
    struct stat info;
#endif
    int code;
    if (!source) {
        code = errno;
        if (code == ENOMEM) abort();
        archive_set_error(writer, code, "Cannot open source");
        return NULL;
    }
#ifdef _WIN32
    {
        int status;
        LIBARCHIVE_WRAPPER_ENTER(writer, goto close_failed_source);
        libarchive_wrapper_set_native_name(entry, source_path);
        status = archive_read_disk_entry_from_file(metadata, entry, _fileno(source), NULL);
        LIBARCHIVE_WRAPPER_LEAVE();
        if (status != ARCHIVE_OK) {
            int error = archive_errno(metadata);
            const char *message = archive_error_string(metadata);
            if (error == ENOMEM) abort();
            archive_set_error(writer, error, "%s", message ? message : "Cannot read source metadata");
            goto close_failed_source;
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            archive_set_error(writer, EINVAL, "Source must be an ordinary readable file");
            goto close_failed_source;
        }
    }
#else
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
#endif
    return source;
#ifdef _WIN32
close_failed_source:
    if (fclose(source) != 0) {
        *cleanup_code = errno;
        if (*cleanup_code == ENOMEM) abort();
    }
    return NULL;
#endif
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
 * Create an owned binary descriptor exclusively, without truncating any existing
 * file or symlink. Return the descriptor or -1 and capture the system error separately.
 * Ownership, finalization and removal policy belong to Eiffel.
 */
static int libarchive_wrapper_create_output(const void *path, int32_t *error_code)
{
    int fd;
    *error_code = 0;
#ifdef _WIN32
    /* The CRT can follow a dangling symlink despite _O_EXCL. Reject the link itself. */
    DWORD attributes = GetFileAttributesW((const wchar_t *)path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            *error_code = EEXIST;
            return -1;
        }
    } else {
        DWORD code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
            *error_code = libarchive_wrapper_windows_code(code);
            return -1;
        }
    }
    fd = _wopen((const wchar_t *)path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
        _S_IREAD | _S_IWRITE);
#else
    fd = open((const char *)path, O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
    if (fd < 0) {
        *error_code = (int32_t)errno;
        if (*error_code == ENOMEM) abort();
    }
    return fd;
}

/** Consume an output descriptor; return zero or the captured closing errno. */
static int libarchive_wrapper_close_output(int fd)
{
#ifdef _WIN32
    if (_close(fd) != 0) {
#else
    if (close(fd) != 0) {
#endif
        int code = errno;
        if (code == ENOMEM) abort();
        return code;
    }
    return 0;
}

/** Remove a caller-selected incomplete output; return zero or captured errno. */
static int libarchive_wrapper_remove_output(const void *path)
{
#ifdef _WIN32
    if (_wunlink((const wchar_t *)path) != 0) {
#else
    if (unlink((const char *)path) != 0) {
#endif
        int code = errno;
        if (code == ENOMEM) abort();
        return code;
    }
    return 0;
}
#ifdef _WIN32
/* One checked iterator, with its first result retained until the first next call. */
struct libarchive_wrapper_directory {
    HANDLE handle;
    WIN32_FIND_DATAW entry;
    int first;
};

static void *libarchive_wrapper_directory_open(const void *path, int32_t *error_code)
{
    const wchar_t *name = (const wchar_t *)path;
    size_t length = wcslen(name);
    wchar_t *pattern = (wchar_t *)malloc((length + 3) * sizeof(wchar_t));
    struct libarchive_wrapper_directory *directory;
    *error_code = 0;
    if (!pattern) abort();
    memcpy(pattern, name, length * sizeof(wchar_t));
    if (length && name[length - 1] != L'\\' && name[length - 1] != L'/') pattern[length++] = L'\\';
    pattern[length++] = L'*';
    pattern[length] = 0;
    directory = (struct libarchive_wrapper_directory *)malloc(sizeof(*directory));
    if (!directory) abort();
    directory->handle = FindFirstFileW(pattern, &directory->entry);
    if (directory->handle == INVALID_HANDLE_VALUE) {
        *error_code = libarchive_wrapper_windows_code(GetLastError());
        free(pattern);
        free(directory);
        return NULL;
    }
    free(pattern);
    directory->first = 1;
    return directory;
}

static const void *libarchive_wrapper_directory_next(void *iterator, int32_t *error_code)
{
    struct libarchive_wrapper_directory *directory = (struct libarchive_wrapper_directory *)iterator;
    *error_code = 0;
    if (directory->first) directory->first = 0;
    else if (!FindNextFileW(directory->handle, &directory->entry)) {
        DWORD code = GetLastError();
        if (code != ERROR_NO_MORE_FILES) *error_code = libarchive_wrapper_windows_code(code);
        return NULL;
    }
    return directory->entry.cFileName;
}

static int libarchive_wrapper_directory_close(void *iterator)
{
    struct libarchive_wrapper_directory *directory = (struct libarchive_wrapper_directory *)iterator;
    int code = 0;
    if (!FindClose(directory->handle)) code = libarchive_wrapper_windows_code(GetLastError());
    free(directory);
    return code;
}
#else
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
#endif
/**
 * Exclusively create an extraction directory and return an owned realpath string.
 * NULL reports mkdir/realpath errno separately. Eiffel frees the returned string.
 * Resolving parent aliases permits the disk writer's secure-symlink checks.
 */
#ifdef _WIN32
static void *libarchive_wrapper_create_directory(const void *path, int32_t *error_code,
    int32_t *cleanup_code)
{
    HANDLE handle;
    DWORD count, copied;
    wchar_t *resolved = NULL;
    *error_code = 0;
    *cleanup_code = 0;
    if (!CreateDirectoryW((const wchar_t *)path, NULL)) {
        *error_code = libarchive_wrapper_windows_code(GetLastError());
        return NULL;
    }
    handle = CreateFileW((const wchar_t *)path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        *error_code = libarchive_wrapper_windows_code(GetLastError());
        return NULL;
    }
    count = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count) *error_code = libarchive_wrapper_windows_code(GetLastError());
    else {
        resolved = (wchar_t *)malloc((size_t)count * sizeof(wchar_t));
        if (!resolved) abort();
        copied = GetFinalPathNameByHandleW(handle, resolved, count, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!copied) *error_code = libarchive_wrapper_windows_code(GetLastError());
        else if (copied >= count) *error_code = ERANGE;
    }
    if (!CloseHandle(handle)) {
        int code = libarchive_wrapper_windows_code(GetLastError());
        if (*error_code) *cleanup_code = code;
        else *error_code = code;
    }
    if (*error_code) { free(resolved); return NULL; }
    return resolved;
}
#else
static void *libarchive_wrapper_create_directory(const void *path, int32_t *error_code, int32_t *cleanup_code)
{
    char *resolved;
    *error_code = 0;
    *cleanup_code = 0;
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
#endif
