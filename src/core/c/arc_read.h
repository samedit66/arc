#ifndef ARC_READ_H
#define ARC_READ_H

#include "arc_native.h"
#include <archive_entry.h>

/* Reader-only resource owner. No extraction writer or destination exists. */
struct arc_read_context {
    struct archive *reader;
    struct archive_entry *entry;
    int reader_open;
    uint64_t block_size;
    la_int64_t block_offset, sparse_offset, sparse_length;
    struct arc_native_error error;
#ifndef _WIN32
    locale_t locale;
#endif
};

static struct arc_read_context *arc_read_new(void)
{
    return (struct arc_read_context *)arc_allocate(sizeof(struct arc_read_context));
}

static int arc_read_open(struct arc_read_context *ctx, const void *path, const char *password)
{
    int status;
#ifndef _WIN32
    ctx->locale = arc_new_locale(&ctx->error);
    if (!ctx->locale) return -1;
#endif
    {
        ARC_ENTER(ctx);
        ctx->reader = archive_read_new();
        if (!ctx->reader) abort();
        status = arc_native_result(&ctx->error, "configure filters", ctx->reader,
            archive_read_support_filter_all(ctx->reader));
        if (!status) status = arc_native_result(&ctx->error, "configure formats", ctx->reader,
            archive_read_support_format_all(ctx->reader));
        if (!status && password) status = arc_native_result(&ctx->error, "configure password", ctx->reader,
            archive_read_add_passphrase(ctx->reader, password));
#ifdef _WIN32
        if (!status) status = arc_native_result(&ctx->error, "open archive", ctx->reader,
            archive_read_open_filename_w(ctx->reader, (const wchar_t *)path, 65536));
#else
        if (!status) status = arc_native_result(&ctx->error, "open archive", ctx->reader,
            archive_read_open_filename(ctx->reader, (const char *)path, 65536));
#endif
        if (!status) ctx->reader_open = 1;
        ARC_LEAVE();
    }
    return status;
}

/* 0 = entry/block, 1 = normal EOF, -1 = failure (including WARN/RETRY). */
static int arc_read_next(struct arc_read_context *ctx)
{
    int status;
    ARC_ENTER(ctx);
    status = archive_read_next_header(ctx->reader, &ctx->entry);
    status = status == ARCHIVE_EOF ? 1 :
        arc_native_result(&ctx->error, "read entry header", ctx->reader, status);
    ARC_LEAVE();
    return status;
}

static int arc_read_skip(struct arc_read_context *ctx)
{
    int status;
    ARC_ENTER(ctx);
    status = arc_native_result(&ctx->error, "skip entry data", ctx->reader,
        archive_read_data_skip(ctx->reader));
    ARC_LEAVE();
    return status;
}

/* Caller selects this reader's locale; the block is borrowed until the next read. */
static int arc_read_block_in_locale(struct arc_read_context *ctx,
                                    const void **buffer, size_t *size, la_int64_t *offset)
{
    int status;
    *buffer = NULL;
    *size = 0;
    *offset = 0;
    ctx->block_size = 0;
    ctx->block_offset = 0;
    status = archive_read_data_block(ctx->reader, buffer, size, offset);
    if (status == ARCHIVE_OK) {
        ctx->block_size = (uint64_t)*size;
        ctx->block_offset = *offset;
    }
    return status == ARCHIVE_EOF ? 1 :
        arc_native_result(&ctx->error, "read data block", ctx->reader, status);
}

static int arc_read_block(struct arc_read_context *ctx)
{
    const void *buffer;
    size_t size;
    la_int64_t offset;
    int status;
    ARC_ENTER(ctx);
    status = arc_read_block_in_locale(ctx, &buffer, &size, &offset);
    ARC_LEAVE();
    return status;
}

/* Borrow sparse-map values only while the current header remains valid. */
static int arc_read_sparse_next(struct arc_read_context *ctx)
{
    int status;
    ARC_ENTER(ctx);
    status = arc_native_result(&ctx->error, "read sparse extent", ctx->reader,
        archive_entry_sparse_next(ctx->entry, &ctx->sparse_offset, &ctx->sparse_length));
    ARC_LEAVE();
    return status;
}

/* Copy pointers before advancing. Prefer UTF-8, preserve raw bytes on conversion failure. */
static const char *arc_read_name(struct arc_read_context *ctx, int target)
{
    const char *result;
    ARC_ENTER(ctx);
    if (target) {
        if (archive_entry_hardlink_is_set(ctx->entry)) {
            result = archive_entry_hardlink_utf8(ctx->entry);
            if (!result) result = archive_entry_hardlink(ctx->entry);
        } else {
            result = archive_entry_symlink_utf8(ctx->entry);
            if (!result) result = archive_entry_symlink(ctx->entry);
        }
    } else {
        result = archive_entry_pathname_utf8(ctx->entry);
        if (!result) result = archive_entry_pathname(ctx->entry);
    }
    ARC_LEAVE();
    return result;
}

static int arc_read_kind(struct arc_read_context *ctx)
{
    if (archive_entry_hardlink_is_set(ctx->entry)) return 4;
    switch (archive_entry_filetype(ctx->entry)) {
        case AE_IFREG: return 1;
        case AE_IFDIR: return 2;
        case AE_IFLNK: return 3;
        default: return 5;
    }
}

static int arc_read_close(struct arc_read_context *ctx)
{
    int status = 0;
    if (ctx->reader_open) {
        ARC_ENTER(ctx);
        status = arc_native_result(&ctx->error, "close reader", ctx->reader,
            archive_read_close(ctx->reader));
        ctx->reader_open = 0;
        ARC_LEAVE();
    }
    return status;
}

static int arc_read_free_handle(struct arc_read_context *ctx)
{
    int status = ARCHIVE_OK;
    if (ctx->reader) {
        ARC_ENTER(ctx);
        status = archive_read_free(ctx->reader);
        ctx->reader = NULL;
        ctx->entry = NULL;
        ARC_LEAVE();
    }
    if (status != ARCHIVE_OK)
        return arc_error(&ctx->error, "free reader", status, "none", 0, "Native release failed");
    return 0;
}

/* Release context storage only after both reader and any borrowing writer are gone. */
static void arc_read_dispose(struct arc_read_context *ctx)
{
#ifndef _WIN32
    arc_free_locale(ctx->locale);
#endif
    free(ctx->error.message);
}

static void arc_read_free(struct arc_read_context *ctx)
{
    arc_read_dispose(ctx);
    free(ctx);
}

#endif
