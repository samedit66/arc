#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "filekit.h"
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
#define OK(expression) CHECK((expression).code == FILEKIT_OK)

int main(void) {
    FilekitInfo info = {0}, saved = {0};
    info.size = 123;
    saved = info;
    CHECK(filekit_info(NULL, &info).code == FILEKIT_INVALID_PATH);
    CHECK(!memcmp(&info, &saved, sizeof info));
    CHECK(filekit_info("missing", NULL).code == FILEKIT_INVALID_ARGUMENT);
    CHECK(filekit_info("\xc0\xaf", &info).code == FILEKIT_INVALID_PATH);
    CHECK(!memcmp(&info, &saved, sizeof info));
    OK(filekit_info("missing", &info));
    CHECK(!info.exists);
    int fd = -1;
    CHECK(filekit_create_file_exclusive("missing/child", 0600, &fd).code == FILEKIT_IO_ERROR);
    CHECK(fd == -1);
    CHECK(filekit_create_file_exclusive("", 0600, &fd).code == FILEKIT_INVALID_PATH);
    CHECK(fd == -1);
    OK(filekit_create_file_exclusive("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", 0600, &fd));
    const unsigned char content[] = {0, 255, '\n', 'A'};
    OK(filekit_write_all(fd, content, sizeof content));
    OK(filekit_write_all(fd, NULL, 0));
    CHECK(filekit_write_all(fd, NULL, 1).code == FILEKIT_INVALID_ARGUMENT);
    OK(filekit_close(fd, "data"));
    fd = -1;
    CHECK(filekit_create_file_exclusive("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", 0600, &fd).code ==
          FILEKIT_ALREADY_EXISTS);
    CHECK(fd == -1);
    OK(filekit_info("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", &info));
    CHECK(info.exists && !info.directory && !info.symbolic && info.size == sizeof content);
    char *absolute = NULL;
    OK(filekit_absolute_path("data-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82", &absolute));
    FilekitInfo anchored;
    OK(filekit_info(absolute, &anchored));
    CHECK(filekit_same_identity(info, anchored));
    CHECK(!filekit_compare_identity(info.identity, anchored.identity));
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
    OK(filekit_info(normalized, &anchored));
    CHECK(filekit_same_identity(info, anchored));
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
    CHECK(filekit_absolute_path("", &absolute).code == FILEKIT_INVALID_PATH);
    CHECK(absolute == sentinel);
    OK(filekit_create_hard_link(absolute, "alias"));
    FilekitInfo alias;
    OK(filekit_info("alias", &alias));
    CHECK(filekit_same_identity(info, alias));
    OK(filekit_set_mtime("alias", 1700000000, 123456700));
    CHECK(filekit_set_mtime("alias", 0, -1).code == FILEKIT_INVALID_ARGUMENT);
    CHECK(filekit_set_mtime("alias", 0, 1000000000L).code == FILEKIT_INVALID_ARGUMENT);
    struct stat native;
    CHECK(stat("alias", &native) == 0);
    CHECK(native.st_mtime == 1700000000);
    OK(filekit_create_directory_exclusive("directory", 0777));
    CHECK(filekit_create_directory_exclusive("directory", 0777).code == FILEKIT_ALREADY_EXISTS);
    OK(filekit_info("directory", &info));
    CHECK(info.exists && info.directory);
#ifdef _WIN32
    CHECK(filekit_restrict_permissions("directory", 0700).code == FILEKIT_UNSUPPORTED);
    CHECK(filekit_info("NUL", &info).code == FILEKIT_INVALID_PATH);
    CHECK(filekit_create_symbolic_link("unknown", "alias", FILEKIT_LINK_UNKNOWN).code ==
          FILEKIT_UNSUPPORTED);
#else
    mode_t mask = umask(0027);
    OK(filekit_create_directory_exclusive("restricted", 0777));
    umask(mask);
    OK(filekit_restrict_permissions("restricted", 0705));
    CHECK(stat("restricted", &native) == 0);
    CHECK((native.st_mode & 0777) == 0700);
    OK(filekit_create_symbolic_link("symlink", "alias", FILEKIT_LINK_FILE));
    OK(filekit_info("symlink", &info));
    CHECK(info.symbolic && !filekit_same_identity(info, alias));
    CHECK(filekit_restrict_permissions("symlink", 0600).code == FILEKIT_INVALID_PATH);
    OK(filekit_set_mtime("symlink", 1600000000, 0));
    CHECK(lstat("symlink", &native) == 0 && native.st_mtime == 1600000000);
    CHECK(stat("alias", &native) == 0 && native.st_mtime == 1700000000);
    OK(filekit_remove_file("symlink"));
    OK(filekit_set_mtime("restricted", 1700000000, 0));
    OK(filekit_create_symbolic_link("directory-link", "restricted", FILEKIT_LINK_DIRECTORY));
    OK(filekit_info("directory-link///", &info));
    CHECK(info.symbolic && !info.directory);
    CHECK(filekit_restrict_permissions("directory-link/", 0500).code == FILEKIT_INVALID_PATH);
    CHECK(stat("restricted", &native) == 0 && (native.st_mode & 0777) == 0700);
    OK(filekit_set_mtime("directory-link///", 1600000000, 0));
    CHECK(lstat("directory-link", &native) == 0 && native.st_mtime == 1600000000);
    CHECK(stat("restricted", &native) == 0 && native.st_mtime == 1700000000);
    OK(filekit_info("restricted///", &info));
    CHECK(info.directory && !info.symbolic);
    OK(filekit_restrict_permissions("restricted/", 0500));
    CHECK(stat("restricted", &native) == 0 && (native.st_mode & 0777) == 0500);
    OK(filekit_set_mtime("restricted/", 1500000000, 0));
    CHECK(stat("restricted", &native) == 0 && native.st_mtime == 1500000000);
    OK(filekit_info("/", &info));
    CHECK(info.exists && info.directory && !info.symbolic);
    OK(filekit_remove_file("directory-link"));
    CHECK(rmdir("restricted") == 0);
#endif
    OK(filekit_remove_file("alias"));
    OK(filekit_info(absolute, &info));
    CHECK(info.exists && info.size == sizeof content);
    OK(filekit_remove_file(absolute));
    free(absolute);
#ifdef _WIN32
    CHECK(_rmdir("directory") == 0);
#else
    CHECK(rmdir("directory") == 0);
#endif
    puts("Independent filekit API checks passed.");
    return 0;
}
