#include "fs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

ArcFsResult arc_fs_result(ArcFsCode code, const char *format, ...) {
    ArcFsResult r = {code, {0}};
    if (format) {
        va_list args;
        va_start(args, format);
        vsnprintf(r.message, sizeof r.message, format, args);
        va_end(args);
    }
    return r;
}

ArcFsResult arc_fs_okay(void) {
    return arc_fs_result(ARC_FS_OK, NULL);
}

int arc_fs_failed(ArcFsResult r) {
    return r.code != ARC_FS_OK;
}

ArcFsResult arc_fs_system_error(const char *operation, const char *path) {
    int e = errno;
    return arc_fs_result(e == EEXIST   ? ARC_FS_ALREADY_EXISTS
                         : e == ENOMEM ? ARC_FS_OUT_OF_MEMORY
                                       : ARC_FS_IO_ERROR,
                         "%s '%s': %s", operation, path ? path : "", strerror(e));
}

ArcFsResult arc_fs_copy_string(const char *s, char **out) {
    size_t n = strlen(s);
    if (n == SIZE_MAX)
        return arc_fs_result(ARC_FS_OVERFLOW, "Path is too long.");
    char *p = malloc(n + 1);
    if (!p)
        return arc_fs_result(ARC_FS_OUT_OF_MEMORY, "Cannot allocate path.");
    memcpy(p, s, n + 1);
    *out = p;
    return arc_fs_okay();
}

ArcFsResult arc_fs_join(const char *a, const char *b, char **out) {
    size_t x = strlen(a), y = strlen(b);
    if (y > SIZE_MAX - 2 || x > SIZE_MAX - y - 2)
        return arc_fs_result(ARC_FS_OVERFLOW, "Path is too long.");
    char *p = malloc(x + y + 2);
    if (!p)
        return arc_fs_result(ARC_FS_OUT_OF_MEMORY, "Cannot allocate path.");
    memcpy(p, a, x);
    if (x && y)
        p[x++] = '/';
    memcpy(p + x, b, y + 1);
    *out = p;
    return arc_fs_okay();
}

int arc_fs_valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        unsigned n;
        if (*p < 0x80) {
            ++p;
            continue;
        }
        if (*p >= 0xc2 && *p <= 0xdf) {
            cp = *p & 31;
            n = 1;
        } else if (*p >= 0xe0 && *p <= 0xef) {
            cp = *p & 15;
            n = 2;
        } else if (*p >= 0xf0 && *p <= 0xf4) {
            cp = *p & 7;
            n = 3;
        } else
            return 0;
        ++p;
        for (unsigned i = 0; i < n; ++i) {
            if ((*p & 0xc0) != 0x80)
                return 0;
            cp = (cp << 6) | (*p++ & 63);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return 0;
    }
    return 1;
}

ArcFsResult arc_fs_check_path(const char *path) {
    if (!path || !*path || !arc_fs_valid_utf8(path))
        return arc_fs_result(ARC_FS_INVALID_PATH, "Invalid UTF-8 filesystem path.");
    return arc_fs_okay();
}
