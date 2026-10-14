/* Shared fs implementation helpers; archive text/support modules also use
 * UTF-8 validation and Windows conversion hooks. Not part of the public API. */
#ifndef ARC_FS_INTERNAL_H
#define ARC_FS_INTERNAL_H
#include "fs.h"
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
ArcFsResult arc_fs_result(ArcFsCode code, const char *format, ...);
ArcFsResult arc_fs_okay(void);
int arc_fs_failed(ArcFsResult r);
ArcFsResult arc_fs_system_error(const char *operation, const char *path);
ArcFsResult arc_fs_copy_string(const char *s, char **out);
ArcFsResult arc_fs_join(const char *a, const char *b, char **out);
int arc_fs_valid_utf8(const char *s);
ArcFsResult arc_fs_check_path(const char *path);
#ifdef _WIN32
#include <wchar.h>
ArcFsResult arc_fs_wide_string(const char *s, int filesystem, wchar_t **out);
ArcFsResult arc_fs_utf8_string(const wchar_t *s, char **out);
ArcFsResult arc_fs_windows_error(const char *operation, const char *path);
int arc_fs_reserved_component(const char *s, size_t n);
#endif
#endif
