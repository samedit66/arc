#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "fs/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define OK(expression) CHECK((expression).code == ARC_FS_OK)

int main(void) {
    ArcFsInfo info = {0}, saved = {0};
    info.size = 123;
    saved = info;
    CHECK(arc_fs_info(NULL, &info).code == ARC_FS_INVALID_PATH);
    CHECK(!memcmp(&info, &saved, sizeof info));
    CHECK(arc_fs_info("missing", NULL).code == ARC_FS_INVALID_ARGUMENT);
    CHECK(arc_fs_info("\xc0\xaf", &info).code == ARC_FS_INVALID_PATH);
    CHECK(!memcmp(&info, &saved, sizeof info));
    OK(arc_fs_info("missing", &info));
    CHECK(!info.exists);
    int fd = -1;
    CHECK(arc_fs_create_file_exclusive("missing/child", 0600, &fd).code == ARC_FS_IO_ERROR);
    CHECK(fd == -1);
    CHECK(arc_fs_create_file_exclusive("", 0600, &fd).code == ARC_FS_INVALID_PATH);
    CHECK(fd == -1);
    OK(arc_fs_create_file_exclusive("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", 0600, &fd));
    const unsigned char content[] = {0, 255, '\n', 'A'};
    OK(arc_fs_write_all(fd, content, sizeof content));
    OK(arc_fs_write_all(fd, NULL, 0));
    CHECK(arc_fs_write_all(fd, NULL, 1).code == ARC_FS_INVALID_ARGUMENT);
    OK(arc_fs_close(fd, "data"));
    fd = -1;
    CHECK(arc_fs_create_file_exclusive("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", 0600, &fd).code ==
          ARC_FS_ALREADY_EXISTS);
    CHECK(fd == -1);
    OK(arc_fs_info("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", &info));
    CHECK(info.exists && !info.directory && !info.symbolic && info.size == sizeof content);
    char *absolute = NULL;
    OK(arc_fs_absolute_path("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", &absolute));
    ArcFsInfo anchored;
    OK(arc_fs_info(absolute, &anchored));
    CHECK(arc_fs_same_identity(info, anchored));
    CHECK(!arc_fs_compare_identity(info.identity, anchored.identity));
#ifdef _WIN32
    CHECK(absolute[0] == '\\');
    /* Match Arc's normalized spelling of extended paths; it must still
     * identify the same file as the native backslash spelling. */
    char *normalized = malloc(strlen(absolute) + 1);
    CHECK(normalized);
    strcpy(normalized, absolute);
    for (char *p = normalized; *p; ++p)
        if (*p == '\\')
            *p = '/';
    OK(arc_fs_info(normalized, &anchored));
    CHECK(arc_fs_same_identity(info, anchored));
    free(normalized);
#else
    CHECK(absolute[0] == '/');
    FILE *input = fopen(absolute, "rb");
    CHECK(input);
    unsigned char readback[sizeof content];
    CHECK(fread(readback, 1, sizeof readback, input) == sizeof readback);
    CHECK(!memcmp(readback, content, sizeof content));
    CHECK(fclose(input) == 0);
#endif
    char *sentinel = absolute;
    CHECK(arc_fs_absolute_path("", &absolute).code == ARC_FS_INVALID_PATH);
    CHECK(absolute == sentinel);
    OK(arc_fs_create_hard_link(absolute, "alias"));
    ArcFsInfo alias;
    OK(arc_fs_info("alias", &alias));
    CHECK(arc_fs_same_identity(info, alias));
    OK(arc_fs_set_mtime("alias", 1700000000, 123456700));
    CHECK(arc_fs_set_mtime("alias", 0, -1).code == ARC_FS_INVALID_ARGUMENT);
    CHECK(arc_fs_set_mtime("alias", 0, 1000000000L).code == ARC_FS_INVALID_ARGUMENT);
    struct stat native;
    CHECK(stat("alias", &native) == 0);
    CHECK(native.st_mtime == 1700000000);
    OK(arc_fs_create_directory_exclusive("directory", 0777));
    CHECK(arc_fs_create_directory_exclusive("directory", 0777).code == ARC_FS_ALREADY_EXISTS);
    OK(arc_fs_info("directory", &info));
    CHECK(info.exists && info.directory);
#ifdef _WIN32
    CHECK(arc_fs_restrict_permissions("directory", 0700).code == ARC_FS_UNSUPPORTED);
    CHECK(arc_fs_info("NUL", &info).code == ARC_FS_INVALID_PATH);
    CHECK(arc_fs_create_symbolic_link("unknown", "alias", ARC_FS_LINK_UNKNOWN).code ==
          ARC_FS_UNSUPPORTED);
#else
    mode_t mask = umask(0027);
    OK(arc_fs_create_directory_exclusive("restricted", 0777));
    umask(mask);
    OK(arc_fs_restrict_permissions("restricted", 0705));
    CHECK(stat("restricted", &native) == 0);
    CHECK((native.st_mode & 0777) == 0700);
    OK(arc_fs_create_symbolic_link("symlink", "alias", ARC_FS_LINK_FILE));
    OK(arc_fs_info("symlink", &info));
    CHECK(info.symbolic && !arc_fs_same_identity(info, alias));
    CHECK(arc_fs_restrict_permissions("symlink", 0600).code == ARC_FS_INVALID_PATH);
    OK(arc_fs_set_mtime("symlink", 1600000000, 0));
    CHECK(lstat("symlink", &native) == 0 && native.st_mtime == 1600000000);
    CHECK(stat("alias", &native) == 0 && native.st_mtime == 1700000000);
    OK(arc_fs_remove_file("symlink"));
    OK(arc_fs_set_mtime("restricted", 1700000000, 0));
    OK(arc_fs_create_symbolic_link("directory-link", "restricted", ARC_FS_LINK_DIRECTORY));
    OK(arc_fs_info("directory-link///", &info));
    CHECK(info.symbolic && !info.directory);
    CHECK(arc_fs_restrict_permissions("directory-link/", 0500).code == ARC_FS_INVALID_PATH);
    CHECK(stat("restricted", &native) == 0 && (native.st_mode & 0777) == 0700);
    OK(arc_fs_set_mtime("directory-link///", 1600000000, 0));
    CHECK(lstat("directory-link", &native) == 0 && native.st_mtime == 1600000000);
    CHECK(stat("restricted", &native) == 0 && native.st_mtime == 1700000000);
    OK(arc_fs_info("restricted///", &info));
    CHECK(info.directory && !info.symbolic);
    OK(arc_fs_restrict_permissions("restricted/", 0500));
    CHECK(stat("restricted", &native) == 0 && (native.st_mode & 0777) == 0500);
    OK(arc_fs_set_mtime("restricted/", 1500000000, 0));
    CHECK(stat("restricted", &native) == 0 && native.st_mtime == 1500000000);
    OK(arc_fs_info("/", &info));
    CHECK(info.exists && info.directory && !info.symbolic);
    OK(arc_fs_remove_file("directory-link"));
    CHECK(rmdir("restricted") == 0);
#endif
    OK(arc_fs_remove_file("alias"));
    OK(arc_fs_info(absolute, &info));
    CHECK(info.exists && info.size == sizeof content);
    OK(arc_fs_remove_file(absolute));
    free(absolute);
#ifdef _WIN32
    CHECK(_rmdir("directory") == 0);
#else
    CHECK(rmdir("directory") == 0);
#endif
    puts("Filesystem module checks passed.");
    return 0;
}
