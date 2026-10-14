#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "fs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int arc_fs_reserved_component(const char *s, size_t n) {
    char stem[16] = {0};
    size_t k = 0;
    while (k < n && s[k] != '.' && k < sizeof stem - 1) {
        char c = s[k];
        stem[k] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        ++k;
    }
    if (!strcmp(stem, "CON") || !strcmp(stem, "PRN") || !strcmp(stem, "AUX") ||
        !strcmp(stem, "NUL"))
        return 1;
    if (!strncmp(stem, "COM", 3) || !strncmp(stem, "LPT", 3)) {
        if (k == 4 && stem[3] >= '1' && stem[3] <= '9')
            return 1;
        if (k == 5 && (unsigned char)stem[3] == 0xc2 &&
            ((unsigned char)stem[4] == 0xb9 || (unsigned char)stem[4] == 0xb2 ||
             (unsigned char)stem[4] == 0xb3))
            return 1;
    }
    return 0;
}

static ArcFsResult validate_windows_path(const char *s) {
    const char *p = s;
    /* Arc normalizes filesystem separators before traversal. Accept both
     * spellings of extended-path prefixes so '?' is treated as part of
     * the prefix, rather than as an invalid filename character. */
    if (!strncmp(p, "\\\\?\\UNC\\", 8) || !strncmp(p, "//?/UNC/", 8))
        p += 8;
    else if (!strncmp(p, "\\\\?\\", 4) || !strncmp(p, "//?/", 4))
        p += 4;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
        p += 2;
    while (*p) {
        while (*p == '/' || *p == '\\')
            ++p;
        if (!*p)
            break;
        const char *end = p;
        while (*end && *end != '/' && *end != '\\')
            ++end;
        size_t n = (size_t)(end - p);
        if ((n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) {
            p = end;
            continue;
        }
        if (p[n - 1] == '.' || p[n - 1] == ' ' || arc_fs_reserved_component(p, n))
            return arc_fs_result(ARC_FS_INVALID_PATH, "Invalid Windows path '%s'.", s);
        for (size_t i = 0; i < n; ++i)
            if ((unsigned char)p[i] < 32 || strchr("<>:\\\"|?*", p[i]))
                return arc_fs_result(ARC_FS_INVALID_PATH, "Invalid Windows path '%s'.", s);
        p = end;
    }
    return arc_fs_okay();
}

ArcFsResult arc_fs_wide_string(const char *s, int filesystem, wchar_t **out) {
    if (filesystem) {
        ArcFsResult r = validate_windows_path(s);
        if (arc_fs_failed(r))
            return r;
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n)
        return arc_fs_result(ARC_FS_INVALID_PATH, "Invalid UTF-8 path.");
    wchar_t *w = malloc((size_t)n * sizeof *w);
    if (!w)
        return arc_fs_result(ARC_FS_OUT_OF_MEMORY, "Cannot allocate Unicode path.");
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    if (!filesystem) {
        *out = w;
        return arc_fs_okay();
    }
    for (wchar_t *p = w; *p; ++p)
        if (*p == L'/')
            *p = L'\\';
    DWORD needed = GetFullPathNameW(w, 0, NULL, NULL);
    if (!needed) {
        free(w);
        return arc_fs_result(ARC_FS_INVALID_PATH, "Cannot resolve '%s'.", s);
    }
    wchar_t *full = malloc(((size_t)needed + 8) * sizeof *full);
    wchar_t *absolute = malloc((size_t)needed * sizeof *absolute);
    if (!full || !absolute) {
        free(w);
        free(full);
        free(absolute);
        return arc_fs_result(ARC_FS_OUT_OF_MEMORY, "Cannot allocate Unicode path.");
    }
    DWORD written = GetFullPathNameW(w, needed, absolute, NULL);
    free(w);
    if (!written || written >= needed) {
        free(full);
        free(absolute);
        return arc_fs_result(ARC_FS_INVALID_PATH, "Cannot resolve '%s'.", s);
    }
    if (wcsncmp(absolute, L"\\\\?\\", 4) == 0)
        wcscpy(full, absolute);
    else if (wcsncmp(absolute, L"\\\\", 2) == 0) {
        wcscpy(full, L"\\\\?\\UNC\\");
        wcscat(full, absolute + 2);
    } else {
        wcscpy(full, L"\\\\?\\");
        wcscat(full, absolute);
    }
    free(absolute);
    *out = full;
    return arc_fs_okay();
}

ArcFsResult arc_fs_utf8_string(const wchar_t *w, char **out) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, NULL, 0, NULL, NULL);
    if (!n)
        return arc_fs_result(ARC_FS_INVALID_PATH, "Unrepresentable Unicode name.");
    char *s = malloc((size_t)n);
    if (!s)
        return arc_fs_result(ARC_FS_OUT_OF_MEMORY, "Cannot allocate name.");
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, s, n, NULL, NULL);
    *out = s;
    return arc_fs_okay();
}

ArcFsResult arc_fs_windows_error(const char *op, const char *path) {
    DWORD e = GetLastError();
    ArcFsCode c = (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) ? ARC_FS_ALREADY_EXISTS
                  : (e == ERROR_NOT_SUPPORTED || e == ERROR_INVALID_FUNCTION) ? ARC_FS_UNSUPPORTED
                                                                              : ARC_FS_IO_ERROR;
    return arc_fs_result(c, "%s '%s': Windows error %lu.", op, path, (unsigned long)e);
}
