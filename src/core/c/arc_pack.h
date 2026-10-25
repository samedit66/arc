#ifndef ARC_PACK_H
#define ARC_PACK_H

#include "arc_native.h"
#include "arc_path.h"
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#define arc_pack_open _wopen
#define arc_pack_close _close
#define arc_pack_unlink _wunlink
#define ARC_PACK_BINARY _O_BINARY
#else
#include <unistd.h>
#define arc_pack_open open
#define arc_pack_close close
#define arc_pack_unlink unlink
#define ARC_PACK_BINARY 0
#endif

struct arc_pack_context {
    struct archive *writer, *disk;
    struct archive_entry *entry;
    struct arc_native_error error;
    arc_char *output;
    int fd, created, disk_open, writer_open, is_output;
#ifdef _WIN32
    HANDLE identity_handle;
    BY_HANDLE_FILE_INFORMATION output_identity;
#endif
    int64_t entry_bytes;
    uint64_t last_block_size;
    la_int64_t output_dev, output_ino;
#ifndef _WIN32
    locale_t locale;
#endif
};

static struct arc_pack_context *arc_pack_new(void)
{
    struct arc_pack_context *ctx = arc_allocate(sizeof(*ctx));
    ctx->fd = -1;
#ifdef _WIN32
    ctx->identity_handle = INVALID_HANDLE_VALUE;
#endif
    ctx->entry = archive_entry_new();
    if (!ctx->entry) abort();
    return ctx;
}

/* Configure everything, including encryption, before creating output. */
static int arc_pack_configure(struct arc_pack_context *ctx, const char *format,
                              const char *compression, const char *password)
{
    int status = 0;
#ifndef _WIN32
    ctx->locale = arc_new_locale(&ctx->error);
    if (!ctx->locale) return -1;
#endif
    {
        ARC_ENTER(ctx);
        ctx->writer = archive_write_new();
        if (!ctx->writer) abort();
        if (!strcmp(format, "tar"))
            status = arc_native_result(&ctx->error, "configure format", ctx->writer,
                archive_write_set_format_pax_restricted(ctx->writer));
        else if (!strcmp(format, "zip"))
            status = arc_native_result(&ctx->error, "configure format", ctx->writer,
                archive_write_set_format_zip(ctx->writer));
        else if (!strcmp(format, "7z"))
            status = arc_native_result(&ctx->error, "configure format", ctx->writer,
                archive_write_set_format_7zip(ctx->writer));
        else status = arc_error(&ctx->error, "configure format", 0, "none", 0, "Unknown archive format");

        if (!status && strcmp(format, "tar") && strcmp(compression, "none"))
            status = arc_error(&ctx->error, "configure compression", 0, "none", 0,
                "Outer compression is supported only for TAR");
        if (!status) status = arc_native_result(&ctx->error, "configure compression", ctx->writer,
            (!strcmp(compression, "none") ? archive_write_add_filter_none(ctx->writer) :
                archive_write_add_filter_by_name(ctx->writer, compression)));
        if (!status && password) {
            if (strcmp(format, "zip"))
                status = arc_error(&ctx->error, "configure password", 0, "none", 0,
                    "Password-protected creation requires ZIP");
            else {
                status = arc_native_result(&ctx->error, "configure password", ctx->writer,
                    archive_write_set_passphrase(ctx->writer, password));
                if (!status) status = arc_native_result(&ctx->error, "configure encryption", ctx->writer,
                    archive_write_set_format_option(ctx->writer, "zip", "encryption", "aes256"));
            }
        }
        ARC_LEAVE();
    }
    return status;
}

static int arc_pack_create_output(struct arc_pack_context *ctx, const arc_char *path)
{
    int status, code;
    size_t length;
#ifdef _WIN32
    struct _stat64 info;
    length = wcslen(path);
#else
    struct stat info;
    length = strlen(path);
#endif
    ctx->output = arc_allocate((length + 1) * sizeof(arc_char));
    memcpy(ctx->output, path, (length + 1) * sizeof(arc_char));
    ctx->fd = arc_pack_open(path, O_WRONLY | O_CREAT | O_EXCL | ARC_PACK_BINARY, 0666);
    if (ctx->fd < 0) { code = errno; return arc_errno_error(&ctx->error, "create archive", code); }
    ctx->created = 1;
#ifdef _WIN32
    status = _fstat64(ctx->fd, &info);
#else
    status = fstat(ctx->fd, &info);
#endif
    if (status) { code = errno; return arc_errno_error(&ctx->error, "stat archive", code); }
#ifdef _WIN32
    if (!GetFileInformationByHandle((HANDLE)_get_osfhandle(ctx->fd), &ctx->output_identity))
        return arc_error(&ctx->error, "identify archive", 0, "win32", GetLastError(), "Cannot identify output archive");
#endif
    ctx->output_dev = info.st_dev;
    ctx->output_ino = info.st_ino;
    {
        ARC_ENTER(ctx);
        status = arc_native_result(&ctx->error, "open archive writer", ctx->writer,
            archive_write_open_fd(ctx->writer, ctx->fd));
        if (!status) ctx->writer_open = 1;
        ARC_LEAVE();
    }
    return status;
}

static int arc_pack_open_source(struct arc_pack_context *ctx, const arc_char *path)
{
    int status;
    ARC_ENTER(ctx);
    ctx->disk = archive_read_disk_new();
    if (!ctx->disk) abort();
    status = arc_native_result(&ctx->error, "configure source", ctx->disk,
        archive_read_disk_set_symlink_physical(ctx->disk));
    if (!status) status = arc_native_result(&ctx->error, "configure source metadata", ctx->disk,
        archive_read_disk_set_behavior(ctx->disk, ARCHIVE_READDISK_NO_ACL |
            ARCHIVE_READDISK_NO_XATTR | ARCHIVE_READDISK_NO_FFLAGS));
#ifdef _WIN32
    if (!status) status = arc_native_result(&ctx->error, "open source", ctx->disk,
        archive_read_disk_open_w(ctx->disk, path));
#else
    if (!status) status = arc_native_result(&ctx->error, "open source", ctx->disk,
        archive_read_disk_open(ctx->disk, path));
#endif
    if (!status) ctx->disk_open = 1;
    ARC_LEAVE();
    return status;
}

/* 0 = entry, 1 = end, -1 = error. Traversal decisions remain in Eiffel. */
static int arc_pack_next(struct arc_pack_context *ctx)
{
    int status;
    ARC_ENTER(ctx);
    ctx->entry_bytes = 0;
    status = archive_read_next_header2(ctx->disk, ctx->entry);
    if (status == ARCHIVE_EOF) status = 1;
    else {
        status = arc_native_result(&ctx->error, "read source metadata", ctx->disk, status);
        if (!status) {
            errno = 0;
            if (!archive_entry_pathname_utf8(ctx->entry)) {
                if (errno == ENOMEM) abort();
                status = arc_error(&ctx->error, "source pathname", 0, "none", 0, "Source pathname cannot be represented as UTF-8");
            }
        }
    }
    ARC_LEAVE();
    return status;
}

static const char *arc_pack_entry_path(struct arc_pack_context *ctx)
{
    const char *result;
    ARC_ENTER(ctx);
    result = archive_entry_pathname_utf8(ctx->entry);
    ARC_LEAVE();
    return result;
}

static int arc_pack_descend(struct arc_pack_context *ctx)
{
    int status;
    ARC_ENTER(ctx);
    status = arc_native_result(&ctx->error, "descend source directory", ctx->disk,
        archive_read_disk_descend(ctx->disk));
    ARC_LEAVE();
    return status;
}

/* Keep the temporary identity handle owned until Eiffel snapshots any error. */
static int arc_pack_check_output(struct arc_pack_context *ctx)
{
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info;
    const wchar_t *name;
    {
        ARC_ENTER(ctx);
        name = archive_entry_pathname_w(ctx->entry);
        ARC_LEAVE();
    }
    if (!name) return arc_error(&ctx->error, "identify source", 0, "none", 0, "Cannot represent source pathname");
    ctx->identity_handle = CreateFileW(name, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (ctx->identity_handle == INVALID_HANDLE_VALUE)
        return arc_error(&ctx->error, "identify source", 0, "win32", GetLastError(), "Cannot open source identity");
    if (!GetFileInformationByHandle(ctx->identity_handle, &info))
        return arc_error(&ctx->error, "identify source", 0, "win32", GetLastError(), "Cannot identify source");
    ctx->is_output = info.dwVolumeSerialNumber == ctx->output_identity.dwVolumeSerialNumber &&
        info.nFileIndexHigh == ctx->output_identity.nFileIndexHigh &&
        info.nFileIndexLow == ctx->output_identity.nFileIndexLow;
#else
    ctx->is_output = archive_entry_dev(ctx->entry) == ctx->output_dev &&
        archive_entry_ino64(ctx->entry) == ctx->output_ino;
#endif
    return 0;
}

static int arc_pack_close_identity(struct arc_pack_context *ctx)
{
#ifdef _WIN32
    if (ctx->identity_handle != INVALID_HANDLE_VALUE) {
        HANDLE handle = ctx->identity_handle;
        ctx->identity_handle = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle))
            return arc_error(&ctx->error, "close source identity", 0, "win32", GetLastError(), "Cannot release source identity");
    }
#else
    (void)ctx;
#endif
    return 0;
}

static int arc_pack_begin_entry(struct arc_pack_context *ctx, const char *name)
{
    int status;
    ARC_ENTER(ctx);
    archive_entry_set_pathname_utf8(ctx->entry, name);
    archive_entry_set_hardlink(ctx->entry, NULL);
    archive_entry_set_nlink(ctx->entry, 1);
    if (archive_entry_filetype(ctx->entry) != AE_IFREG) archive_entry_set_size(ctx->entry, 0);
    status = arc_native_result(&ctx->error, "write archive header", ctx->writer,
        archive_write_header(ctx->writer, ctx->entry));
    ARC_LEAVE();
    return status;
}

/* One source block; a short native write is an error, not a completed block. */
static int arc_pack_transfer(struct arc_pack_context *ctx)
{
    char buffer[65536];
    la_ssize_t count, written;
    int status = 0;
    ARC_ENTER(ctx);
    ctx->last_block_size = 0;
    count = archive_read_data(ctx->disk, buffer, sizeof(buffer));
    if (count < 0) {
        arc_native_result(&ctx->error, "read source data", ctx->disk, (int)count);
        status = -2;
    }
    else if (!count) {
        if (ctx->entry_bytes != archive_entry_size(ctx->entry))
            status = arc_error(&ctx->error, "read source data", 0, "none", 0, "Source size changed during packing") - 1;
        else status = 1;
    } else {
        written = archive_write_data(ctx->writer, buffer, (size_t)count);
        if (written < 0) status = arc_native_result(&ctx->error, "write archive data", ctx->writer, (int)written);
        else if (written != count) status = arc_error(&ctx->error, "write archive data", 0, "none", 0, "Short archive write");
        else { ctx->last_block_size = (uint64_t)count; ctx->entry_bytes += count; }
    }
    ARC_LEAVE();
    return status;
}

static int arc_pack_finish_entry(struct arc_pack_context *ctx)
{
    int status;
    ARC_ENTER(ctx);
    status = arc_native_result(&ctx->error, "finish archive entry", ctx->writer,
        archive_write_finish_entry(ctx->writer));
    ARC_LEAVE();
    return status;
}

/* Each cleanup step is separate so Eiffel can snapshot every failure. */
static int arc_pack_close_disk(struct arc_pack_context *ctx)
{
    int status = 0;
    if (ctx->disk_open) {
        ARC_ENTER(ctx);
        ctx->disk_open = 0;
        status = arc_native_result(&ctx->error, "close source", ctx->disk, archive_read_close(ctx->disk));
        ARC_LEAVE();
    }
    return status;
}

static int arc_pack_free_disk(struct arc_pack_context *ctx)
{
    int status = 0;
    if (ctx->disk) {
        ARC_ENTER(ctx);
        status = archive_read_free(ctx->disk);
        ctx->disk = NULL;
        if (status != ARCHIVE_OK) status = arc_error(&ctx->error, "free source", status, "none", 0, "Cannot release source reader");
        ARC_LEAVE();
    }
    return status;
}

static int arc_pack_close_writer(struct arc_pack_context *ctx)
{
    int status = 0;
    if (ctx->writer_open) {
        ARC_ENTER(ctx);
        ctx->writer_open = 0;
        status = arc_native_result(&ctx->error, "finalize archive", ctx->writer, archive_write_close(ctx->writer));
        ARC_LEAVE();
    }
    return status;
}

static int arc_pack_free_writer(struct arc_pack_context *ctx)
{
    int status = 0;
    if (ctx->writer) {
        ARC_ENTER(ctx);
        status = archive_write_free(ctx->writer);
        ctx->writer = NULL;
        if (status != ARCHIVE_OK) status = arc_error(&ctx->error, "free writer", status, "none", 0, "Cannot release archive writer");
        ARC_LEAVE();
    }
    return status;
}

static int arc_pack_close_output(struct arc_pack_context *ctx)
{
    int status, code;
    if (ctx->fd < 0) return 0;
    status = arc_pack_close(ctx->fd);
    code = errno;
    ctx->fd = -1;
    return status ? arc_errno_error(&ctx->error, "close archive file", code) : 0;
}

static int arc_pack_remove_output(struct arc_pack_context *ctx)
{
    int code;
    if (ctx->created) {
        ctx->created = 0;
        if (arc_pack_unlink(ctx->output)) {
            code = errno;
            return arc_errno_error(&ctx->error, "remove incomplete archive", code);
        }
    }
    return 0;
}

static void arc_pack_free(struct arc_pack_context *ctx)
{
    archive_entry_free(ctx->entry);
#ifndef _WIN32
    arc_free_locale(ctx->locale);
#endif
    free(ctx->output);
    free(ctx->error.message);
    free(ctx);
}
#endif
