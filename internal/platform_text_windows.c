#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "support.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "platform_text.h"
#include "../filekit/filekit.h"
#include "../filekit/filekit_internal.h"
#include <errno.h>
#include <windows.h>

ArcResult platform_wide_string(const char *s, int filesystem, wchar_t **out) {
    return ar_from_filekit(fk_wide_string(s, filesystem, out));
}

ArcResult platform_utf8_string(const wchar_t *s, char **out) {
    return ar_from_filekit(fk_utf8_string(s, out));
}

ArcResult platform_windows_error(const char *operation, const char *path) {
    return ar_from_filekit(fk_windows_error(operation, path));
}

ArcResult platform_compare_names(const char *a, const char *b, int prefix, int *equal) {
    wchar_t *wa = NULL, *wb = NULL;
    ArcResult r = platform_wide_string(a, 0, &wa);
    if (!ar_failed(r))
        r = platform_wide_string(b, 0, &wb);
    if (!ar_failed(r)) {
        size_t na = wcslen(wa), nb = wcslen(wb);
        if (na > INT_MAX || nb > INT_MAX)
            r = ar_result(ARC_OVERFLOW, "Entry comparison exceeds platform range.");
        else if (prefix && (nb <= na || wb[na] != L'/'))
            *equal = 0;
        else {
            int comparison =
                CompareStringOrdinal(wa, (int)na, wb, prefix ? (int)na : (int)nb, TRUE);
            if (!comparison)
                r = platform_windows_error("Compare entry names", a);
            else
                *equal = comparison == CSTR_EQUAL;
        }
    }
    free(wa);
    free(wb);
    return r;
}

ArcResult locale_begin(LocaleScope *scope) {
    memset(scope, 0, sizeof *scope);
    const char *previous = setlocale(LC_CTYPE, NULL);
    if (!previous)
        return ar_result(ARC_IO_ERROR, "Cannot query character-conversion context.");
    ArcResult r = ar_copy_string(previous, &scope->previous);
    if (ar_failed(r))
        return r;
    scope->previous_mode = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
    if (scope->previous_mode == -1) {
        free(scope->previous);
        return ar_result(ARC_IO_ERROR, "Cannot enable thread-local character conversion.");
    }
    if (!setlocale(LC_CTYPE, ".UTF8")) {
        _configthreadlocale(scope->previous_mode);
        free(scope->previous);
        return ar_result(ARC_UNSUPPORTED, "UTF-8 character-conversion context is unavailable.");
    }
    return ar_okay();
}

ArcResult locale_end(LocaleScope *scope, ArcResult r) {
    int restored = setlocale(LC_CTYPE, scope->previous) != NULL;
    int mode_restored = _configthreadlocale(scope->previous_mode) != -1;
    free(scope->previous);
    if (!restored || !mode_restored)
        r = ar_cleanup(r, ar_result(ARC_IO_ERROR, "Cannot restore character-conversion context."));
    return r;
}

int platform_valid_component(const char *p, size_t k) {
    if (p[k - 1] == '.' || p[k - 1] == ' ' || fk_reserved_component(p, k))
        return 0;
    for (size_t i = 0; i < k; ++i)
        if ((unsigned char)p[i] < 32 || strchr("<>:\"\\|?*", p[i]))
            return 0;
    return 1;
}

void platform_normalize_separators(char *path) {
    for (char *p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
}

int platform_root_separator(const char *path, const char *separator) {
    if (path[0] && path[1] == ':')
        return separator == path + 2;
    const char *server = NULL;
    if (!strncmp(path, "//?/UNC/", 8))
        server = path + 8;
    else if (!strncmp(path, "//?/", 4))
        return separator <= path + 3 || (path[4] && path[5] == ':' && separator == path + 6);
    else if (!strncmp(path, "//", 2))
        server = path + 2;
    if (server) {
        const char *end = strchr(server, '/');
        return separator < server || (end && separator == end);
    }
    return 0;
}

int platform_link_target_compatible(const char *target) {
    return !strchr(target, '\\') && !strchr(target, ':');
}

ArcResult platform_order_names(const char *a, const char *b, int *order) {
    wchar_t *wa = NULL, *wb = NULL;
    ArcResult r = platform_wide_string(a, 0, &wa);
    if (!ar_failed(r))
        r = platform_wide_string(b, 0, &wb);
    if (!ar_failed(r)) {
        int comparison = CompareStringOrdinal(wa, -1, wb, -1, TRUE);
        if (!comparison)
            r = platform_windows_error("Order entry names", a);
        else
            *order = comparison - CSTR_EQUAL;
    }
    free(wa);
    free(wb);
    return r;
}
