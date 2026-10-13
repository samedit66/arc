#include "filekit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

FilekitResult fk_result(FilekitCode code, const char *format, ...) {
    FilekitResult r = {code, {0}};
    if (format) {
        va_list args;
        va_start(args, format);
        vsnprintf(r.message, sizeof r.message, format, args);
        va_end(args);
    }
    return r;
}

FilekitResult fk_okay(void) {
    return fk_result(FILEKIT_OK, NULL);
}

int fk_failed(FilekitResult r) {
    return r.code != FILEKIT_OK;
}

FilekitResult fk_system_error(const char *operation, const char *path) {
    int e = errno;
    return fk_result(e == EEXIST   ? FILEKIT_ALREADY_EXISTS
                     : e == ENOMEM ? FILEKIT_OUT_OF_MEMORY
                                   : FILEKIT_IO_ERROR,
                     "%s '%s': %s", operation, path ? path : "", strerror(e));
}

FilekitResult fk_copy_string(const char *s, char **out) {
    size_t n = strlen(s);
    if (n == SIZE_MAX)
        return fk_result(FILEKIT_OVERFLOW, "Path is too long.");
    char *p = malloc(n + 1);
    if (!p)
        return fk_result(FILEKIT_OUT_OF_MEMORY, "Cannot allocate path.");
    memcpy(p, s, n + 1);
    *out = p;
    return fk_okay();
}

FilekitResult fk_join(const char *a, const char *b, char **out) {
    size_t x = strlen(a), y = strlen(b);
    if (y > SIZE_MAX - 2 || x > SIZE_MAX - y - 2)
        return fk_result(FILEKIT_OVERFLOW, "Path is too long.");
    char *p = malloc(x + y + 2);
    if (!p)
        return fk_result(FILEKIT_OUT_OF_MEMORY, "Cannot allocate path.");
    memcpy(p, a, x);
    if (x && y)
        p[x++] = '/';
    memcpy(p + x, b, y + 1);
    *out = p;
    return fk_okay();
}

int fk_valid_utf8(const char *s) {
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

FilekitResult fk_check_path(const char *path) {
    if (!path || !*path || !fk_valid_utf8(path))
        return fk_result(FILEKIT_INVALID_PATH, "Invalid UTF-8 filesystem path.");
    return fk_okay();
}
