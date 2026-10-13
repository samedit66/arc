#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "support.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "libarchive_adapter.h"
#include "arc_internal.h"
#include "platform_text.h"
#include <errno.h>

ArcResult backend_error(struct archive *a, ArcCode fallback) {
    const char *s = archive_error_string(a);
    int e = archive_errno(a);
    ArcCode code = e == ENOMEM ? ARC_OUT_OF_MEMORY : fallback;
    if (e != ENOMEM && s &&
        (strstr(s, "Unsupported") || strstr(s, "unsupported") || strstr(s, "Unrecognized") ||
         strstr(s, "not supported")))
        code = ARC_UNSUPPORTED;
    if (e != ENOMEM && s && code != ARC_UNSUPPORTED) {
        if (strstr(s, "Passphrase required"))
            return ar_result(ARC_PASSWORD_REQUIRED, "Password required for encrypted content.");
        if (strstr(s, "Incorrect passphrase") || strstr(s, "incorrect passphrases"))
            return ar_result(ARC_PASSWORD_REJECTED, "Password rejected for encrypted content.");
        if (strstr(s, "passphrase"))
            return ar_result(code, "Archive password processing failed.");
    }
    return ar_result(code, "%s", s ? s : "Archive backend failed.");
}

ArcResult validate_password(const char *password) {
    if (password && (!*password || !ar_valid_utf8(password)))
        return ar_result(ARC_INVALID_ARGUMENT, "Password must be nonempty UTF-8 text.");
    return ar_okay();
}

static ArcResult configure_encryption(struct archive *a, const ArcOptions *options) {
    ArcResult r = validate_password(options->password);
    if (ar_failed(r) || !options->password)
        return r;
    if (strcmp(options->format, "zip"))
        return ar_result(ARC_UNSUPPORTED, "Password-protected creation requires ZIP.");
    if (archive_write_set_format_option(a, "zip", "encryption", "aes256") != ARCHIVE_OK ||
        archive_write_set_passphrase(a, options->password) != ARCHIVE_OK)
        return backend_error(a, ARC_IO_ERROR);
    return ar_okay();
}

typedef int (*Setup)(struct archive *);

typedef struct {
    const char *name;
    Setup read, write;
    unsigned features;
} Format;

static const Format formats[] = {
    {"tar", archive_read_support_format_tar, archive_write_set_format_pax_restricted,
     ARC_CAN_WRITE_DIRECTORIES | ARC_CAN_WRITE_SYMBOLIC_LINKS | ARC_CAN_WRITE_HARD_LINKS |
         ARC_CAN_READ_DIRECTORIES | ARC_CAN_READ_SYMBOLIC_LINKS | ARC_CAN_READ_HARD_LINKS},
    {"zip", archive_read_support_format_zip, archive_write_set_format_zip,
     ARC_CAN_WRITE_DIRECTORIES | ARC_CAN_WRITE_SYMBOLIC_LINKS | ARC_CAN_READ_DIRECTORIES |
         ARC_CAN_READ_SYMBOLIC_LINKS},
    {"7z", archive_read_support_format_7zip, archive_write_set_format_7zip,
     ARC_CAN_WRITE_DIRECTORIES | ARC_CAN_WRITE_SYMBOLIC_LINKS | ARC_CAN_READ_DIRECTORIES |
         ARC_CAN_READ_SYMBOLIC_LINKS},
    {"cpio", archive_read_support_format_cpio, archive_write_set_format_cpio_odc,
     ARC_CAN_WRITE_DIRECTORIES | ARC_CAN_WRITE_SYMBOLIC_LINKS | ARC_CAN_READ_DIRECTORIES |
         ARC_CAN_READ_SYMBOLIC_LINKS},
    {"ar", archive_read_support_format_ar, archive_write_set_format_ar_bsd, 0},
    {"iso9660", archive_read_support_format_iso9660, archive_write_set_format_iso9660,
     ARC_CAN_WRITE_DIRECTORIES | ARC_CAN_WRITE_SYMBOLIC_LINKS | ARC_CAN_READ_DIRECTORIES |
         ARC_CAN_READ_SYMBOLIC_LINKS},
    {"xar", archive_read_support_format_xar, archive_write_set_format_xar,
     ARC_CAN_WRITE_DIRECTORIES | ARC_CAN_WRITE_SYMBOLIC_LINKS | ARC_CAN_READ_DIRECTORIES |
         ARC_CAN_READ_SYMBOLIC_LINKS},
    {"warc", archive_read_support_format_warc, archive_write_set_format_warc, 0},
    {"rar", archive_read_support_format_rar, NULL,
     ARC_CAN_READ_DIRECTORIES | ARC_CAN_READ_SYMBOLIC_LINKS | ARC_CAN_READ_HARD_LINKS},
    {"cab", archive_read_support_format_cab, NULL, ARC_CAN_READ_DIRECTORIES},
    {"lha", archive_read_support_format_lha, NULL,
     ARC_CAN_READ_DIRECTORIES | ARC_CAN_READ_SYMBOLIC_LINKS}};
static const Format filters[] = {
    {"none", archive_read_support_filter_none, archive_write_add_filter_none, 0},
    {"gzip", archive_read_support_filter_gzip, archive_write_add_filter_gzip, 0},
    {"bzip2", archive_read_support_filter_bzip2, archive_write_add_filter_bzip2, 0},
    {"xz", archive_read_support_filter_xz, archive_write_add_filter_xz, 0},
    {"zstd", archive_read_support_filter_zstd, archive_write_add_filter_zstd, 0}};

static const Format *lookup(const Format *table, size_t n, const char *name) {
    for (size_t i = 0; i < n; ++i)
        if (!strcmp(table[i].name, name))
            return &table[i];
    return NULL;
}

/* Registration warnings can denote unavailable native compression (external
 * program fallback). Other failures are probe errors, never missing bits. */
static ArcResult probe_setup(struct archive *a, Setup setup, int filter, int *available) {
    int status = setup(a);
    if (status == ARCHIVE_OK)
        return ar_okay();
    ArcResult r = backend_error(a, ARC_IO_ERROR);
    if (r.code == ARC_UNSUPPORTED ||
        (filter && status == ARCHIVE_WARN && archive_errno(a) != ENOMEM)) {
        *available = 0;
        return ar_okay();
    }
    return r;
}

static ArcResult probe_free(struct archive *a, int write) {
    int status = write ? archive_write_free(a) : archive_read_free(a);
    return status == ARCHIVE_OK ? ar_okay()
                                : ar_result(ARC_IO_ERROR, "Capability probe cleanup failed.");
}

ArcResult arc_get_capabilities_impl(const ArcOptions *options, unsigned *out) {
    if (!options || !options->format || !options->compression || !out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing archive configuration.");
    ArcResult validation = validate_password(options->password);
    if (ar_failed(validation))
        return validation;
    const Format *f = lookup(formats, ARRAY_COUNT(formats), options->format);
    const Format *c = lookup(filters, ARRAY_COUNT(filters), options->compression);
    if (!f || !c)
        return ar_result(ARC_UNSUPPORTED, "Unknown archive configuration.");
    struct archive *read = archive_read_new(), *write = archive_write_new();
    if (!read || !write) {
        ArcResult r = ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate archive backend.");
        if (read)
            r = ar_cleanup(r, probe_free(read, 0));
        if (write)
            r = ar_cleanup(r, probe_free(write, 1));
        return r;
    }
    unsigned flags = 0;
    int available = 1;
    ArcResult r = probe_setup(read, f->read, 0, &available);
    if (!ar_failed(r) && available && !strcmp(f->name, "rar"))
        r = probe_setup(read, archive_read_support_format_rar5, 0, &available);
    if (!ar_failed(r) && available)
        r = probe_setup(read, c->read, 1, &available);
    if (!ar_failed(r) && available)
        flags |= ARC_CAN_INSPECT | ARC_CAN_UNPACK | ARC_CAN_LIST | ARC_CAN_VERIFY |
                 (f->features & (ARC_CAN_READ_DIRECTORIES | ARC_CAN_READ_SYMBOLIC_LINKS |
                                 ARC_CAN_READ_HARD_LINKS));
    available = f->write != NULL;
    if (!ar_failed(r) && available)
        r = probe_setup(write, f->write, 0, &available);
    if (!ar_failed(r) && available)
        r = probe_setup(write, c->write, 1, &available);
    if (!ar_failed(r) && available)
        r = configure_encryption(write, options);
    if (!ar_failed(r) && available)
        flags |= ARC_CAN_CREATE |
                 (f->features & (ARC_CAN_WRITE_DIRECTORIES | ARC_CAN_WRITE_SYMBOLIC_LINKS |
                                 ARC_CAN_WRITE_HARD_LINKS));
    r = ar_cleanup(r, probe_free(read, 0));
    r = ar_cleanup(r, probe_free(write, 1));
    if (ar_failed(r))
        return r;
    if (!flags)
        return ar_result(ARC_UNSUPPORTED, "Configuration is unavailable in this build.");
    *out = flags;
    return ar_okay();
}

ArcResult entry_text(struct archive_entry *e, int which, char **out) {
#ifdef _WIN32
    const wchar_t *w = which == 0   ? archive_entry_pathname_w(e)
                       : which == 1 ? archive_entry_symlink_w(e)
                                    : archive_entry_hardlink_w(e);
    if (w)
        return platform_utf8_string(w, out);
#endif
    const char *s = which == 0   ? archive_entry_pathname_utf8(e)
                    : which == 1 ? archive_entry_symlink_utf8(e)
                                 : archive_entry_hardlink_utf8(e);
    if (!s)
        s = which == 0   ? archive_entry_pathname(e)
            : which == 1 ? archive_entry_symlink(e)
                         : archive_entry_hardlink(e);
    if (!s || !ar_valid_utf8(s))
        return ar_result(ARC_INVALID_PATH, "Missing or unrepresentable entry text.");
    return ar_copy_string(s, out);
}

ArcResult disk_open(struct archive *disk, const char *path) {
#ifdef _WIN32
    wchar_t *w = NULL;
    ArcResult r = platform_wide_string(path, 0, &w);
    if (ar_failed(r))
        return r;
    int status = archive_read_disk_open_w(disk, w);
    free(w);
#else
    int status = archive_read_disk_open(disk, path);
#endif
    return status == ARCHIVE_OK ? ar_okay() : backend_error(disk, ARC_IO_ERROR);
}

void clean_metadata(struct archive_entry *e) {
    archive_entry_set_perm(e, archive_entry_perm(e) & 0777);
    archive_entry_set_uid(e, 0);
    archive_entry_set_gid(e, 0);
    archive_entry_set_uname(e, NULL);
    archive_entry_set_gname(e, NULL);
    archive_entry_acl_clear(e);
    archive_entry_xattr_clear(e);
    archive_entry_set_fflags(e, 0, 0);
}

ArcResult reader_open(const char *path, const char *password, struct archive **out) {
    if (!path || !*path || !ar_valid_utf8(path))
        return ar_result(ARC_INVALID_ARGUMENT, "Missing or invalid archive path.");
    ArcResult validation = validate_password(password);
    if (ar_failed(validation))
        return validation;
    struct archive *a = archive_read_new();
    if (!a)
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate reader.");
    if (archive_read_support_format_all(a) != ARCHIVE_OK) {
        ArcResult r = backend_error(a, ARC_IO_ERROR);
        return ar_cleanup(r, probe_free(a, 0));
    }
    const Setup readers[] = {archive_read_support_filter_none,     archive_read_support_filter_gzip,
                             archive_read_support_filter_bzip2,    archive_read_support_filter_xz,
                             archive_read_support_filter_zstd,     archive_read_support_filter_lzma,
                             archive_read_support_filter_lzip,     archive_read_support_filter_lz4,
                             archive_read_support_filter_compress, archive_read_support_filter_rpm,
                             archive_read_support_filter_uu};
    for (size_t i = 0; i < ARRAY_COUNT(readers); ++i) {
        struct archive *probe = archive_read_new();
        if (!probe) {
            return ar_cleanup(ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate filter probe."),
                              probe_free(a, 0));
        }
        int supported = 1;
        ArcResult r = probe_setup(probe, readers[i], 1, &supported);
        r = ar_cleanup(r, probe_free(probe, 0));
        if (!ar_failed(r) && supported && readers[i](a) != ARCHIVE_OK)
            r = backend_error(a, ARC_IO_ERROR);
        if (ar_failed(r)) {
            return ar_cleanup(r, probe_free(a, 0));
        }
    }
    ArcResult r = ar_okay();
    if (password && archive_read_add_passphrase(a, password) != ARCHIVE_OK)
        return ar_cleanup(backend_error(a, ARC_IO_ERROR), probe_free(a, 0));
#ifdef _WIN32
    wchar_t *w = NULL;
    r = platform_wide_string(path, 1, &w);
    int status = ar_failed(r) ? ARCHIVE_FATAL : archive_read_open_filename_w(a, w, 65536);
    free(w);
#else
    int status = archive_read_open_filename(a, path, 65536);
#endif
    if (!ar_failed(r) && status != ARCHIVE_OK)
        r = backend_error(a, ARC_IO_ERROR);
    if (ar_failed(r)) {
        return ar_cleanup(r, probe_free(a, 0));
    }
    *out = a;
    return ar_okay();
}

ArcEntryType entry_type(struct archive_entry *e) {
    if (archive_entry_hardlink(e) || archive_entry_hardlink_w(e))
        return ARC_ENTRY_HARD_LINK;
    switch (archive_entry_filetype(e)) {
    case AE_IFREG:
        return ARC_ENTRY_FILE;
    case AE_IFDIR:
        return ARC_ENTRY_DIRECTORY;
    case AE_IFLNK:
        return ARC_ENTRY_SYMBOLIC_LINK;
    default:
        return ARC_ENTRY_OTHER;
    }
}

ArcResult read_header(struct archive *a, struct archive_entry **e, int *end) {
    int status = archive_read_next_header(a, e);
    *end = status == ARCHIVE_EOF;
    if (*end)
        return ar_okay();
    if (status != ARCHIVE_OK)
        return backend_error(a, ARC_INVALID_ARCHIVE);
    return ar_okay();
}

ArcResult normalized_entry(struct archive *a, struct archive_entry *e) {
    /* Ambiguous CPIO inode groups need normalization before streaming callbacks. */
    if ((archive_format(a) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_CPIO &&
        archive_entry_filetype(e) == AE_IFREG && archive_entry_nlink(e) > 1)
        return ar_result(ARC_UNSUPPORTED,
                         "CPIO hard-link groups require unsupported normalization.");
    return ar_okay();
}

ArcResult reader_release(struct archive *a, ArcResult r) {
    int status = archive_read_close(a);
    if (status != ARCHIVE_OK)
        r = ar_cleanup(r, backend_error(a, ARC_IO_ERROR));
    status = archive_read_free(a);
    if (status != ARCHIVE_OK)
        r = ar_cleanup(r, ar_result(ARC_IO_ERROR, "Reader cleanup failed."));
    return r;
}

ArcResult backend_configure_writer(struct archive *backend, const ArcOptions *options) {
    const Format *f = lookup(formats, ARRAY_COUNT(formats), options->format);
    const Format *c = lookup(filters, ARRAY_COUNT(filters), options->compression);
    if (f->write(backend) != ARCHIVE_OK || c->write(backend) != ARCHIVE_OK)
        return backend_error(backend, ARC_IO_ERROR);
    /* Explicit UTF-8 keeps ZIP names portable: the Windows backend may
     * otherwise choose CP437, which cannot represent names such as Cyrillic. */
    if (!strcmp(f->name, "zip") &&
        archive_write_set_format_option(backend, "zip", "hdrcharset", "UTF-8") != ARCHIVE_OK)
        return backend_error(backend, ARC_IO_ERROR);
    return configure_encryption(backend, options);
}

ArchiveMetadata backend_file_metadata(struct archive_entry *entry) {
    ArchiveMetadata metadata = {(unsigned)archive_entry_perm(entry) & 0777,
                                archive_entry_mtime_is_set(entry), archive_entry_mtime(entry),
                                archive_entry_mtime_nsec(entry)};
    return metadata;
}
