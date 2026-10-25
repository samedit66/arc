#ifndef ARC_UNPACK_H
#define ARC_UNPACK_H

#include "arc_read.h"
#include <archive_entry.h>
#include <sys/stat.h>
#include "arc_path.h"

struct arc_unpack_context {
    struct arc_read_context input; /* Also owns locale and shared operation diagnostic. */
    struct archive *writer;
    arc_char *destination;
    uint64_t last_block_size;
};

static struct arc_unpack_context *arc_unpack_new(void)
{
    return (struct arc_unpack_context *)arc_allocate(sizeof(struct arc_unpack_context));
}

static int arc_unpack_open_writer(struct arc_unpack_context *ctx)
{
    int status;
    ARC_ENTER(&ctx->input);
    ctx->writer = archive_write_disk_new();
    if (!ctx->writer) abort();
    status = arc_native_result(&ctx->input.error, "configure writer", ctx->writer,
        archive_write_disk_set_options(ctx->writer, ARCHIVE_EXTRACT_TIME |
            ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
            ARCHIVE_EXTRACT_NO_OVERWRITE));
    ARC_LEAVE();
    return status;
}

/* Create exclusively, then resolve the filesystem path for secure writer checks. */
static int arc_unpack_create_destination(struct arc_unpack_context *ctx, const arc_char *path)
{
    int system_code;
#ifdef _WIN32
    HANDLE directory;
    DWORD size, length, code;
    if (_wmkdir(path)) {
        system_code = errno;
        return arc_errno_error(&ctx->input.error, "create destination", system_code);
    }
    directory = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (directory == INVALID_HANDLE_VALUE) {
        code = GetLastError();
        return arc_error(&ctx->input.error, "resolve destination", 0, "win32", (int)code, "Cannot open destination directory");
    }

    size = GetFinalPathNameByHandleW(directory, NULL, 0, 0);
    code = size ? ERROR_SUCCESS : GetLastError();
    if (size) {
        ctx->destination = (arc_char *)arc_allocate((size_t)size * sizeof(arc_char));
        length = GetFinalPathNameByHandleW(directory, ctx->destination, size, 0);
        if (!length) code = GetLastError();
        else if (length >= size) code = ERROR_INSUFFICIENT_BUFFER;
    }
    if (!CloseHandle(directory)) abort(); /* Invalid handle: internal defect. */
    if (code) return arc_error(&ctx->input.error, "resolve destination", 0, "win32", (int)code, "Cannot resolve destination directory");
#else
    if (mkdir(path, 0777)) {
        system_code = errno;
        return arc_errno_error(&ctx->input.error, "create destination", system_code);
    }
    ctx->destination = realpath(path, NULL);
    if (!ctx->destination) {
        system_code = errno;
        return arc_errno_error(&ctx->input.error, "resolve destination", system_code);
    }
#endif
    return 0;
}

static const arc_char *arc_unpack_entry_name(struct arc_unpack_context *ctx, int hardlink)
{
    const arc_char *name;
    ARC_ENTER(&ctx->input);
    errno = 0;
    name = hardlink ? arc_hardlink(ctx->input.entry) : arc_pathname(ctx->input.entry);
    if (!name && errno == ENOMEM) abort();
    ARC_LEAVE();
    return name;
}

static void arc_unpack_set_paths(struct arc_unpack_context *ctx, const arc_char *path, const arc_char *target)
{
    const arc_char *stored;
    ARC_ENTER(&ctx->input);
    arc_copy_pathname(ctx->input.entry, path);
    stored = arc_pathname(ctx->input.entry);
    if (!stored || arc_compare(stored, path)) abort();

    if (target) {
        arc_copy_hardlink(ctx->input.entry, target);
        stored = arc_hardlink(ctx->input.entry);
        if (!stored || arc_compare(stored, target)) abort();
    }
    ARC_LEAVE();
}

static int arc_unpack_begin_entry(struct arc_unpack_context *ctx)
{
    int status;
    /* The accessor may convert text; filesystem checks below consume native bytes. */
    const arc_char *name = arc_unpack_entry_name(ctx, 0);
    if (!name) abort(); /* set_paths has already established a native pathname. */
    /* NO_OVERWRITE may silently skip a collision. Reject it explicitly. */
#ifdef _WIN32
    DWORD attributes = GetFileAttributesW(name);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (archive_entry_filetype(ctx->input.entry) != AE_IFDIR ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return arc_error(&ctx->input.error, "entry collision", 0, "errno", EEXIST, "Destination entry already exists");
    } else {
        DWORD code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
            return arc_error(&ctx->input.error, "inspect destination", 0, "win32", (int)code, "Cannot inspect destination entry");
    }
#else
    struct stat info;
    if (!lstat(name, &info)) {
        if (archive_entry_filetype(ctx->input.entry) != AE_IFDIR || !S_ISDIR(info.st_mode))
            return arc_error(&ctx->input.error, "entry collision", 0, "errno", EEXIST, "Destination entry already exists");
    } else {
        int code = errno;
        if (code != ENOENT) return arc_errno_error(&ctx->input.error, "inspect destination", code);
    }
#endif
    {
        ARC_ENTER(&ctx->input);
        status = arc_native_result(&ctx->input.error, "write entry header", ctx->writer,
            archive_write_header(ctx->writer, ctx->input.entry));
        ARC_LEAVE();
    }
    return status;
}

static int arc_unpack_transfer_block(struct arc_unpack_context *ctx)
{
    const void *buffer = NULL;
    size_t size = 0;
    la_int64_t offset = 0;
    int status;
    ARC_ENTER(&ctx->input);
    ctx->last_block_size = 0;
    status = arc_read_block_in_locale(&ctx->input, &buffer, &size, &offset);
    if (!status) {
        if (arc_native_result(&ctx->input.error, "write data block", ctx->writer,
            (int)archive_write_data_block(ctx->writer, buffer, size, offset))) status = -2;
        else ctx->last_block_size = (uint64_t)size;
    }
    ARC_LEAVE();
    return status;
}

static int arc_unpack_finish_entry(struct arc_unpack_context *ctx)
{
    int status;
    ARC_ENTER(&ctx->input);
    status = arc_native_result(&ctx->input.error, "finish entry", ctx->writer, archive_write_finish_entry(ctx->writer));
    ARC_LEAVE();
    return status;
}

/* One cleanup action per call: Eiffel snapshots each diagnostic before the next. */
static int arc_unpack_close_writer(struct arc_unpack_context *ctx)
{
    int status;
    if (!ctx->writer) return 0;
    {
        ARC_ENTER(&ctx->input);
        status = arc_native_result(&ctx->input.error, "close writer", ctx->writer,
            archive_write_close(ctx->writer));
        ARC_LEAVE();
    }
    return status;
}

static int arc_unpack_free_writer(struct arc_unpack_context *ctx)
{
    int status;
    if (!ctx->writer) return 0;
    {
        ARC_ENTER(&ctx->input);
        status = archive_write_free(ctx->writer);
        ARC_LEAVE();
    }
    ctx->writer = NULL;
    if (status != ARCHIVE_OK)
        return arc_error(&ctx->input.error, "free writer", status, "none", 0, "Native release failed");
    return 0;
}

static void arc_unpack_free(struct arc_unpack_context *ctx)
{
    arc_read_dispose(&ctx->input);
    free(ctx->destination);
    free(ctx);
}

#endif
