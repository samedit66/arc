#ifndef ARC_PATH_H
#define ARC_PATH_H

#include <archive_entry.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
/* Paths follow the native ABI; passwords remain UTF-8 on both platforms. */
typedef wchar_t arc_char;
#define arc_pathname archive_entry_pathname_w
#define arc_hardlink archive_entry_hardlink_w
#define arc_copy_pathname archive_entry_copy_pathname_w
#define arc_copy_hardlink archive_entry_copy_hardlink_w
#define arc_compare wcscmp
#else
typedef char arc_char;
#define arc_pathname archive_entry_pathname
#define arc_hardlink archive_entry_hardlink
#define arc_copy_pathname archive_entry_copy_pathname
#define arc_copy_hardlink archive_entry_copy_hardlink
#define arc_compare strcmp
#endif

#endif
