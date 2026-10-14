#define _POSIX_C_SOURCE 200809L
/* Backend-cost probe for known benchmark fixtures; Arc's contracts are broader. */
#include <archive.h>
#include <archive_entry.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void check(int status, struct archive *a) {
    if (status < ARCHIVE_OK) {
        fprintf(stderr, "%s\n", archive_error_string(a));
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts(archive_version_details());
        return 0;
    }
    if (argc != 4)
        return 2;
    if (strcmp(argv[1], "pack") && strcmp(argv[1], "unpack") && strcmp(argv[1], "drain"))
        return 2;
    setlocale(LC_ALL, "");
    struct archive *r = archive_read_new(), *w = NULL;
    if (!r)
        return 1;
    struct archive_entry *e;
    int status;
    if (!strcmp(argv[1], "pack")) {
        archive_read_free(r);
        r = archive_read_disk_new();
        w = archive_write_new();
        if (!r || !w)
            return 1;
        check(archive_write_set_format_zip(w), w);
        check(archive_write_set_format_option(w, "zip", "hdrcharset", "UTF-8"), w);
        check(archive_write_open_filename(w, argv[3]), w);
        if (chdir(argv[2]))
            return 1;
        archive_read_disk_set_symlink_physical(r);
        archive_read_disk_set_behavior(r, ARCHIVE_READDISK_NO_ACL | ARCHIVE_READDISK_NO_XATTR |
                                              ARCHIVE_READDISK_NO_FFLAGS);
        check(archive_read_disk_open(r, "."), r);
        e = archive_entry_new();
        if (!e)
            return 1;
        while ((status = archive_read_next_header2(r, e)) != ARCHIVE_EOF) {
            check(status, r);
            check(archive_read_disk_descend(r), r);
            const char *name = archive_entry_pathname(e);
            if (!strcmp(name, "."))
                continue;
            archive_entry_set_uid(e, 0);
            archive_entry_set_gid(e, 0);
            archive_entry_set_uname(e, NULL);
            archive_entry_set_gname(e, NULL);
            char *stored = strdup(!strncmp(name, "./", 2) ? name + 2 : name);
            if (!stored)
                return 1;
            archive_entry_set_pathname(e, stored);
            free(stored);
            check(archive_write_header(w, e), w);
            unsigned char data[65536];
            la_ssize_t n;
            while ((n = archive_read_data(r, data, sizeof data)) > 0) {
                if (archive_write_data(w, data, (size_t)n) != n)
                    return 1;
            }
            if (n < 0)
                check((int)n, r);
            check(archive_write_finish_entry(w), w);
        }
        archive_entry_free(e);
    } else {
        archive_read_support_filter_all(r);
        archive_read_support_format_all(r);
        check(archive_read_open_filename(r, argv[2], 65536), r);
        if (!strcmp(argv[1], "unpack")) {
            if (mkdir(argv[3], 0777) || chdir(argv[3]))
                return 1;
            w = archive_write_disk_new();
            if (!w)
                return 1;
            check(archive_write_disk_set_options(
                      w, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                             ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                             ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS | ARCHIVE_EXTRACT_NO_OVERWRITE),
                  w);
        }
        while ((status = archive_read_next_header(r, &e)) != ARCHIVE_EOF) {
            check(status, r);
            if (w)
                check(archive_write_header(w, e), w);
            const void *data;
            size_t size;
            la_int64_t offset;
            while ((status = archive_read_data_block(r, &data, &size, &offset)) != ARCHIVE_EOF) {
                check(status, r);
                if (w)
                    check(archive_write_data_block(w, data, size, offset), w);
            }
            if (w)
                check(archive_write_finish_entry(w), w);
        }
    }
    check(archive_read_close(r), r);
    if (archive_read_free(r) != ARCHIVE_OK)
        return 1;
    if (w) {
        check(archive_write_close(w), w);
        if (archive_write_free(w) != ARCHIVE_OK)
            return 1;
    }
    return 0;
}
