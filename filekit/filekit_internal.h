#ifndef FILEKIT_INTERNAL_H
#define FILEKIT_INTERNAL_H
#include "filekit.h"
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
FilekitResult fk_result(FilekitCode code, const char *format, ...);
FilekitResult fk_okay(void);
int fk_failed(FilekitResult r);
FilekitResult fk_system_error(const char *operation, const char *path);
FilekitResult fk_copy_string(const char *s, char **out);
FilekitResult fk_join(const char *a, const char *b, char **out);
int fk_valid_utf8(const char *s);
FilekitResult fk_check_path(const char *path);
#ifdef _WIN32
#include <wchar.h>
FilekitResult fk_wide_string(const char *s, int filesystem, wchar_t **out);
FilekitResult fk_utf8_string(const wchar_t *s, char **out);
FilekitResult fk_windows_error(const char *operation, const char *path);
int fk_reserved_component(const char *s, size_t n);
#endif
#endif
