#include "support.h"
#include "../filekit/filekit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

ArcResult ar_result(ArcCode code, const char *format, ...) {
    ArcResult r = {code, {0}};
    if (format) {
        va_list args;
        va_start(args, format);
        vsnprintf(r.message, sizeof r.message, format, args);
        va_end(args);
    }
    return r;
}

ArcResult ar_okay(void) {
    return ar_result(ARC_OK, NULL);
}

int ar_failed(ArcResult r) {
    return r.code != ARC_OK;
}

ArcResult ar_system_error(const char *operation, const char *path) {
    int e = errno;
    return ar_result(e == EEXIST   ? ARC_ALREADY_EXISTS
                     : e == ENOMEM ? ARC_OUT_OF_MEMORY
                                   : ARC_IO_ERROR,
                     "%s '%s': %s", operation, path ? path : "", strerror(e));
}

ArcResult ar_copy_string(const char *s, char **out) {
    size_t n = strlen(s);
    if (n == SIZE_MAX)
        return ar_result(ARC_OVERFLOW, "Path is too long.");
    char *p = malloc(n + 1);
    if (!p)
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate path.");
    memcpy(p, s, n + 1);
    *out = p;
    return ar_okay();
}

ArcResult ar_join(const char *a, const char *b, char **out) {
    size_t x = strlen(a), y = strlen(b);
    if (y > SIZE_MAX - 2 || x > SIZE_MAX - y - 2)
        return ar_result(ARC_OVERFLOW, "Path is too long.");
    char *p = malloc(x + y + 2);
    if (!p)
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate path.");
    memcpy(p, a, x);
    if (x && y)
        p[x++] = '/';
    memcpy(p + x, b, y + 1);
    *out = p;
    return ar_okay();
}

int ar_valid_utf8(const char *s) {
    return s && fk_valid_utf8(s);
}

ArcResult ar_cleanup(ArcResult primary, ArcResult cleanup) {
    if (!ar_failed(cleanup))
        return primary;
    if (!ar_failed(primary))
        return cleanup;
    return ar_result(primary.code, "%.170s; cleanup: %.70s", primary.message, cleanup.message);
}

ArcResult ar_from_filekit(FilekitResult result) {
    ArcCode code;
    switch (result.code) {
    case FILEKIT_OK:
        code = ARC_OK;
        break;
    case FILEKIT_INVALID_ARGUMENT:
        code = ARC_INVALID_ARGUMENT;
        break;
    case FILEKIT_INVALID_PATH:
        code = ARC_INVALID_PATH;
        break;
    case FILEKIT_ALREADY_EXISTS:
        code = ARC_ALREADY_EXISTS;
        break;
    case FILEKIT_UNSUPPORTED:
        code = ARC_UNSUPPORTED;
        break;
    case FILEKIT_OVERFLOW:
        code = ARC_OVERFLOW;
        break;
    case FILEKIT_OUT_OF_MEMORY:
        code = ARC_OUT_OF_MEMORY;
        break;
    case FILEKIT_IO_ERROR:
        code = ARC_IO_ERROR;
        break;
    default:
        return ar_result(ARC_IO_ERROR, "Unknown filekit error.");
    }
    ArcResult out = {code, {0}};
    memcpy(out.message, result.message, sizeof out.message);
    out.message[sizeof out.message - 1] = '\0';
    return out;
}

static int ascii_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return 0;
    }
    return *a == *b;
}

static const ArchiveSuffix suffixes[] = {
    {".tar.gz", "tar", "gzip"},  {".tar.bz2", "tar", "bzip2"}, {".tar.xz", "tar", "xz"},
    {".tar.zst", "tar", "zstd"}, {".tgz", "tar", "gzip"},      {".tar", "tar", "none"},
    {".zip", "zip", "none"},     {".7z", "7z", "none"},        {".rar", "rar", "none"},
    {".cab", "cab", "none"},     {".lha", "lha", "none"},      {".lzh", "lha", "none"},
    {".cpio", "cpio", "none"},   {".ar", "ar", "none"},        {".iso", "iso9660", "none"},
    {".xar", "xar", "none"},     {".warc", "warc", "none"}};

const ArchiveSuffix *ar_suffix_of(const char *path, int creation) {
    if (!path)
        return NULL;
    size_t length = strlen(path);
    const ArchiveSuffix *best = NULL;
    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; ++i) {
        if (creation && i >= 8)
            break;
        size_t n = strlen(suffixes[i].suffix);
        if (length >= n && ascii_equal(path + length - n, suffixes[i].suffix) &&
            (!best || n > strlen(best->suffix)))
            best = &suffixes[i];
    }
    return best;
}
