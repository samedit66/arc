#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "support.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "platform_text.h"
#include <errno.h>

ArcResult platform_compare_names(const char *a, const char *b, int prefix, int *equal) {
    size_t n = strlen(a);
    *equal = prefix ? strlen(b) > n && b[n] == '/' && !strncmp(a, b, n) : !strcmp(a, b);
    return ar_okay();
}

ArcResult locale_begin(LocaleScope *scope) {
    memset(scope, 0, sizeof *scope);
    const char *names[] = {"C.UTF-8", "UTF-8", "en_US.UTF-8"};
    for (size_t i = 0; i < ARRAY_COUNT(names); ++i) {
        scope->utf8 = newlocale(LC_CTYPE_MASK, names[i], (locale_t)0);
        if (scope->utf8)
            break;
        if (errno == ENOMEM)
            return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate character-conversion context.");
    }
    if (!scope->utf8)
        return ar_result(ARC_UNSUPPORTED, "UTF-8 character-conversion context is unavailable.");
    scope->previous = uselocale(scope->utf8);
    if (!scope->previous) {
        freelocale(scope->utf8);
        return ar_result(ARC_IO_ERROR, "Cannot enter thread-local character conversion.");
    }
    return ar_okay();
}

ArcResult locale_end(LocaleScope *scope, ArcResult r) {
    if (!uselocale(scope->previous)) {
        /* Do not free a locale still installed on this thread. */
        r = ar_cleanup(r, ar_result(ARC_IO_ERROR, "Cannot restore character-conversion context."));
    } else
        freelocale(scope->utf8);
    return r;
}

int platform_valid_component(const char *s, size_t length) {
    (void)s;
    (void)length;
    return 1;
}

void platform_normalize_separators(char *path) {
    (void)path;
}

int platform_drive_separator(const char *path, const char *separator) {
    (void)path;
    (void)separator;
    return 0;
}

int platform_link_target_compatible(const char *target) {
    (void)target;
    return 1;
}

ArcResult platform_order_names(const char *a, const char *b, int *order) {
    *order = strcmp(a, b);
    return ar_okay();
}
