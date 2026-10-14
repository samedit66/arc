#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "fs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "fs.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <windows.h>
#include <io.h>
#include <winioctl.h>

ArcFsResult arc_fs_info(const char *path, ArcFsInfo *out) {
    if (!out)
        return arc_fs_result(ARC_FS_INVALID_ARGUMENT, "Missing info output.");
    ArcFsResult checked_path = arc_fs_check_path(path);
    if (arc_fs_failed(checked_path))
        return checked_path;
    ArcFsInfo i = {0};
    wchar_t *w = NULL;
    ArcFsResult r = arc_fs_wide_string(path, 1, &w);
    if (arc_fs_failed(r))
        return r;
    HANDLE h = CreateFileW(
        w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD native_error = GetLastError();
    free(w);
    SetLastError(native_error);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            *out = i;
            return arc_fs_okay();
        }
        return arc_fs_windows_error("Inspect", path);
    }
    BY_HANDLE_FILE_INFORMATION b;
    if (!GetFileInformationByHandle(h, &b)) {
        r = arc_fs_windows_error("Inspect", path);
        CloseHandle(h);
        return r;
    }
    i.exists = 1;
    i.directory = !!(b.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    i.mode = i.directory ? 0777u : 0666u;
    i.identity.key[0] = b.dwVolumeSerialNumber;
    i.identity.key[1] = ((uint64_t)b.nFileIndexHigh << 32) | b.nFileIndexLow;
    i.size = ((uint64_t)b.nFileSizeHigh << 32) | b.nFileSizeLow;
    if (b.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        unsigned char buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
        DWORD length;
        if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, sizeof buffer, &length,
                             NULL)) {
            r = arc_fs_windows_error("Inspect reparse point", path);
            CloseHandle(h);
            return r;
        }
        DWORD tag;
        memcpy(&tag, buffer, sizeof tag);
        if (tag != IO_REPARSE_TAG_SYMLINK) {
            CloseHandle(h);
            return arc_fs_result(ARC_FS_UNSUPPORTED, "Unsupported reparse point '%s'.", path);
        }
        i.symbolic = 1;
    }
    CloseHandle(h);
    *out = i;
    return arc_fs_okay();
}

int arc_fs_same_identity(ArcFsInfo a, ArcFsInfo b) {
    return a.exists && b.exists && a.identity.key[0] == b.identity.key[0] &&
           a.identity.key[1] == b.identity.key[1];
}

ArcFsResult arc_fs_create_file_exclusive(const char *path, unsigned mode, int *out) {
    if (!out)
        return arc_fs_result(ARC_FS_INVALID_ARGUMENT, "Missing descriptor output.");
    ArcFsResult checked_path = arc_fs_check_path(path);
    if (arc_fs_failed(checked_path))
        return checked_path;
    int fd;
    wchar_t *w = NULL;
    ArcFsResult r = arc_fs_wide_string(path, 1, &w);
    if (arc_fs_failed(r))
        return r;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD native_error = GetLastError();
        free(w);
        SetLastError(native_error);
        return arc_fs_windows_error("Open", path);
    }
    fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        ArcFsResult failure = arc_fs_system_error("Open", path);
        int closed = CloseHandle(h);
        DWORD close_error = closed ? 0 : GetLastError();
        if (!DeleteFileW(w)) {
            DWORD delete_error = GetLastError();
            failure = arc_fs_result(
                failure.code, "%.120s; Incomplete output remains: '%.70s' (Windows error %lu).",
                failure.message, path, (unsigned long)delete_error);
        } else if (!closed)
            failure =
                arc_fs_result(failure.code, "%.170s; handle cleanup failed (Windows error %lu).",
                              failure.message, (unsigned long)close_error);
        free(w);
        return failure;
    }
    free(w);
    (void)mode;
    *out = fd;
    return arc_fs_okay();
}

ArcFsResult arc_fs_close(int fd, const char *path) {
    if (fd < 0)
        return arc_fs_result(ARC_FS_INVALID_ARGUMENT, "Invalid descriptor.");
    if (_close(fd) < 0)
        return arc_fs_system_error("Close", path);
    return arc_fs_okay();
}

ArcFsResult arc_fs_write_all(int fd, const void *data, size_t n) {
    if (fd < 0 || (!data && n))
        return arc_fs_result(ARC_FS_INVALID_ARGUMENT, "Invalid write arguments.");
    const unsigned char *p = data;
    while (n) {
        int done = _write(fd, p, n > INT_MAX ? INT_MAX : (unsigned)n);
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0)
            return arc_fs_system_error("Write", "");
        p += done;
        n -= (size_t)done;
    }
    return arc_fs_okay();
}

ArcFsResult arc_fs_remove_file(const char *path) {
    ArcFsResult checked_path = arc_fs_check_path(path);
    if (arc_fs_failed(checked_path))
        return checked_path;
    wchar_t *w = NULL;
    ArcFsResult r = arc_fs_wide_string(path, 1, &w);
    if (arc_fs_failed(r))
        return r;
    int status = DeleteFileW(w);
    DWORD native_error = GetLastError();
    free(w);
    SetLastError(native_error);
    if (!status)
        return arc_fs_windows_error("Remove", path);
    return arc_fs_okay();
}

ArcFsResult arc_fs_create_directory_exclusive(const char *path, unsigned mode) {
    ArcFsResult checked_path = arc_fs_check_path(path);
    if (arc_fs_failed(checked_path))
        return checked_path;
    wchar_t *w = NULL;
    ArcFsResult r = arc_fs_wide_string(path, 1, &w);
    if (arc_fs_failed(r))
        return r;
    int status = CreateDirectoryW(w, NULL);
    DWORD native_error = GetLastError();
    free(w);
    SetLastError(native_error);
    (void)mode;
    if (!status)
        return arc_fs_windows_error("Create directory", path);
    return arc_fs_okay();
}

ArcFsResult arc_fs_restrict_permissions(const char *path, unsigned allowed_mode) {
    ArcFsResult checked_path = arc_fs_check_path(path);
    if (arc_fs_failed(checked_path))
        return checked_path;
    (void)path;
    (void)allowed_mode;
    return arc_fs_result(ARC_FS_UNSUPPORTED, "POSIX permissions are unavailable on Windows.");
}

ArcFsResult arc_fs_set_mtime(const char *path, int64_t seconds, long nanoseconds) {
    if (nanoseconds < 0 || nanoseconds > 999999999L)
        return arc_fs_result(ARC_FS_INVALID_ARGUMENT, "Invalid nanoseconds.");
    ArcFsResult checked_path = arc_fs_check_path(path);
    if (arc_fs_failed(checked_path))
        return checked_path;
    wchar_t *w = NULL;
    ArcFsResult r = arc_fs_wide_string(path, 1, &w);
    if (arc_fs_failed(r))
        return r;
    HANDLE h =
        CreateFileW(w, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    DWORD native_error = GetLastError();
    free(w);
    SetLastError(native_error);
    if (h == INVALID_HANDLE_VALUE)
        return arc_fs_windows_error("Set metadata", path);
    {
        /* FILETIME starts in 1601; conversion must not wrap. */
        if (seconds < -11644473600LL || seconds > (INT64_MAX / 10000000LL) - 11644473600LL) {
            CloseHandle(h);
            return arc_fs_result(ARC_FS_OVERFLOW, "Timestamp is outside Windows range.");
        }
        uint64_t ticks =
            (uint64_t)(seconds + 11644473600LL) * 10000000ULL + (uint64_t)nanoseconds / 100;
        FILETIME t = {(DWORD)ticks, (DWORD)(ticks >> 32)};
        if (!SetFileTime(h, NULL, NULL, &t)) {
            r = arc_fs_windows_error("Set modification time", path);
            CloseHandle(h);
            return r;
        }
    }
    CloseHandle(h);
    return arc_fs_okay();
}

ArcFsResult arc_fs_create_hard_link(const char *target, const char *name) {
    ArcFsResult checked_target = arc_fs_check_path(target);
    if (arc_fs_failed(checked_target))
        return checked_target;
    ArcFsResult checked_name = arc_fs_check_path(name);
    if (arc_fs_failed(checked_name))
        return checked_name;
    wchar_t *a = NULL, *b = NULL;
    ArcFsResult r = arc_fs_wide_string(target, 1, &a);
    if (!arc_fs_failed(r))
        r = arc_fs_wide_string(name, 1, &b);
    if (!arc_fs_failed(r) && !CreateHardLinkW(b, a, NULL))
        r = arc_fs_windows_error("Create hard link", name);
    free(a);
    free(b);
    return r;
}

ArcFsResult arc_fs_create_symbolic_link(const char *full, const char *link_target,
                                        ArcFsLinkKind kind) {
    if (kind < ARC_FS_LINK_UNKNOWN || kind > ARC_FS_LINK_DIRECTORY)
        return arc_fs_result(ARC_FS_INVALID_ARGUMENT, "Invalid link kind.");
    ArcFsResult checked_full = arc_fs_check_path(full);
    if (arc_fs_failed(checked_full))
        return checked_full;
    ArcFsResult checked_link_target = arc_fs_check_path(link_target);
    if (arc_fs_failed(checked_link_target))
        return checked_link_target;
    if (!kind)
        return arc_fs_result(ARC_FS_UNSUPPORTED, "Unknown Windows link target type '%s'.", full);
    wchar_t *name = NULL, *target = NULL;
    ArcFsResult r = arc_fs_wide_string(full, 1, &name);
    if (!arc_fs_failed(r))
        r = arc_fs_wide_string(link_target, 0, &target);
    if (!arc_fs_failed(r)) {
        for (wchar_t *s = target; *s; ++s)
            if (*s == L'/')
                *s = L'\\';
        DWORD flags = (kind == 2 ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0) | 2u;
        if (!CreateSymbolicLinkW(name, target, flags)) {
            if (GetLastError() == ERROR_INVALID_PARAMETER &&
                CreateSymbolicLinkW(name, target, flags & ~2u)) { /* Legacy privileged API. */
            } else
                r = arc_fs_windows_error("Create symbolic link", full);
        }
    }
    free(name);
    free(target);
    return r;
}

int arc_fs_compare_identity(ArcFsIdentity a, ArcFsIdentity b) {
    for (size_t i = 0; i < ARRAY_COUNT(a.key); ++i)
        if (a.key[i] != b.key[i])
            return a.key[i] < b.key[i] ? -1 : 1;
    return 0;
}

ArcFsResult arc_fs_absolute_path(const char *path, char **out) {
    if (!out)
        return arc_fs_result(ARC_FS_INVALID_ARGUMENT, "Missing path output.");
    ArcFsResult checked_path = arc_fs_check_path(path);
    if (arc_fs_failed(checked_path))
        return checked_path;
    wchar_t *wide = NULL;
    ArcFsResult r = arc_fs_wide_string(path, 1, &wide);
    if (!arc_fs_failed(r))
        r = arc_fs_utf8_string(wide, out);
    free(wide);
    return r;
}
