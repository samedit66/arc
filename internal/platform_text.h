#ifndef ARC_INTERNAL_PLATFORM_TEXT_H
#define ARC_INTERNAL_PLATFORM_TEXT_H

#include "../arc.h"
#include <locale.h>
#ifdef _WIN32
#include <wchar.h>

typedef struct {
    char *previous;
    int previous_mode;
} LocaleScope;

/**
 * Returned strings are owned by the caller; filesystem mode resolves long paths.
 *
 * - `s`: UTF-8 source string.
 * - `filesystem`: Nonzero enables filesystem path conversion, including long paths.
 * - `out`: Output for an owned wide string; caller frees it.
 */
ArcResult platform_wide_string(const char *s, int filesystem, wchar_t **out);
ArcResult platform_utf8_string(const wchar_t *w, char **out);
ArcResult platform_windows_error(const char *operation, const char *path);
#else
typedef struct {
    locale_t previous, utf8;
} LocaleScope;
#endif
ArcResult locale_begin(LocaleScope *scope);
ArcResult locale_end(LocaleScope *scope, ArcResult r);
ArcResult platform_order_names(const char *a, const char *b, int *order);
ArcResult platform_compare_names(const char *a, const char *b, int prefix, int *equal);
int platform_valid_component(const char *s, size_t length);
void platform_normalize_separators(char *path);
/* Normalized separators within a native root cannot be inspected as parents. */
int platform_root_separator(const char *path, const char *separator);
int platform_link_target_compatible(const char *target);

#endif
