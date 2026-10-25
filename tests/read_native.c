/* Exercise reader-only native status translation and caller-locale restoration. */
#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "probe_locale.h"

static int result, close_calls, free_calls, fail_allocation;
static int open_stage, open_result;

static struct archive *probe_new(void)
{
    assert(MB_CUR_MAX > 1);
    return (struct archive *)(uintptr_t)1;
}

static struct archive *probe_disk_new(void)
{
    assert(MB_CUR_MAX > 1);
    return (struct archive *)(uintptr_t)2;
}

static int probe_configure(struct archive *reader, int stage)
{
    assert(reader == (struct archive *)(uintptr_t)1);
    assert(MB_CUR_MAX > 1);
    return open_stage == stage ? open_result : ARCHIVE_OK;
}

static int probe_filters(struct archive *reader) { return probe_configure(reader, 1); }
static int probe_formats(struct archive *reader) { return probe_configure(reader, 2); }

static int probe_password(struct archive *reader, const char *password)
{
    assert(!strcmp(password, "secret"));
    return probe_configure(reader, 3);
}

static int probe_open(struct archive *reader, const char *path, size_t block_size)
{
    assert(!strcmp(path, "archive.zip") && block_size == 65536);
    return probe_configure(reader, 4);
}

static int probe_writer_options(struct archive *writer, int flags)
{
    assert(writer == (struct archive *)(uintptr_t)2);
    assert(MB_CUR_MAX > 1);
    assert(flags == (ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_SYMLINKS |
        ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_NO_OVERWRITE));
    return open_stage == 5 ? open_result : ARCHIVE_OK;
}

static int probe_next(struct archive *reader, struct archive_entry **entry)
{
    (void)reader;
    (void)entry;
    assert(MB_CUR_MAX > 1);
    return result;
}

static int probe_block(struct archive *reader, const void **buffer, size_t *size, la_int64_t *offset)
{
    (void)reader;
    (void)buffer;
    (void)size;
    (void)offset;
    assert(MB_CUR_MAX > 1);
    return result;
}

static int probe_skip(struct archive *reader)
{
    (void)reader;
    assert(MB_CUR_MAX > 1);
    return result;
}

static int probe_close(struct archive *reader)
{
    (void)reader;
    assert(MB_CUR_MAX > 1);
    ++close_calls;
    return result;
}

static int probe_free(struct archive *reader)
{
    (void)reader;
    assert(MB_CUR_MAX > 1);
    ++free_calls;
    return result;
}

static int probe_errno(struct archive *reader)
{
    (void)reader;
    return 123;
}

static const char *probe_error(struct archive *reader)
{
    (void)reader;
    return "injected failure\xff";
}

static void *probe_calloc(size_t count, size_t size)
{
    return fail_allocation ? NULL : calloc(count, size);
}

#define archive_read_new probe_new
#define archive_read_support_filter_all probe_filters
#define archive_read_support_format_all probe_formats
#define archive_read_add_passphrase probe_password
#define archive_read_open_filename probe_open
#define archive_write_disk_new probe_disk_new
#define archive_write_disk_set_options probe_writer_options
#define archive_write_close probe_close
#define archive_write_free probe_free
#define archive_read_next_header probe_next
#define archive_read_data_block probe_block
#define archive_read_data_skip probe_skip
#define archive_read_close probe_close
#define archive_read_free probe_free
#define archive_errno probe_errno
#define archive_error_string probe_error
#define calloc probe_calloc
#include "arc_unpack.h"
#undef calloc

/* Both clients must release partial opens and retain each diagnostic until the next step. */
static void verify_open_failures(locale_t original)
{
    int stage;
    const char *stages[] = {"configure filters", "configure formats", "configure password",
        "open archive", "configure writer"};
    for (stage = 1; stage <= 5; ++stage) {
        struct arc_unpack_context *ctx = arc_unpack_new();
        close_calls = free_calls = 0;
        open_stage = stage;
        open_result = ARCHIVE_FAILED;
        {
            int status = arc_read_open(&ctx->input, "archive.zip", "secret");
            if (!status) status = arc_unpack_open_writer(ctx);
            assert(status == -1);
        }
        assert(!strcmp(ctx->input.error.stage, stages[stage - 1]));
        assert(ctx->input.error.native_status == ARCHIVE_FAILED);
        assert(ctx->input.error.system_code == 123);
        assert(uselocale((locale_t)0) == original);

        result = ARCHIVE_WARN;
        assert(arc_unpack_close_writer(ctx) == (stage == 5 ? -1 : 0));
        if (stage == 5) assert(!strcmp(ctx->input.error.stage, "close writer"));
        result = ARCHIVE_FATAL;
        assert(arc_unpack_free_writer(ctx) == (stage == 5 ? -1 : 0));
        if (stage == 5) assert(!strcmp(ctx->input.error.stage, "free writer"));
        result = ARCHIVE_RETRY;
        assert(arc_read_close(&ctx->input) == (stage == 5 ? -1 : 0));
        if (stage == 5) assert(!strcmp(ctx->input.error.stage, "close reader"));
        result = ARCHIVE_FAILED;
        assert(arc_read_free_handle(&ctx->input) == -1);
        assert(!strcmp(ctx->input.error.stage, "free reader"));
        assert(close_calls == (stage == 5 ? 2 : 0));
        assert(free_calls == (stage == 5 ? 2 : 1));
        assert(!ctx->input.reader && !ctx->input.entry && !ctx->writer);
        assert(uselocale((locale_t)0) == original);
        arc_unpack_free(ctx);
        assert(uselocale((locale_t)0) == original);
    }
}

int main(int argc, char **argv)
{
    struct arc_read_context *ctx = arc_read_new();
    int statuses[] = {ARCHIVE_WARN, ARCHIVE_RETRY, ARCHIVE_FAILED, ARCHIVE_FATAL};
    size_t i;
    void *previous = arc_probe_begin_locale();
    locale_t original = uselocale((locale_t)0);
    assert(MB_CUR_MAX == 1);
    ctx->locale = arc_new_locale(&ctx->error);
    assert(ctx->locale);

    if (argc > 1 && !strcmp(argv[1], "allocation-oom")) {
        fail_allocation = 1;
        (void)arc_read_new();
        return 99;
    }
    if (argc > 1 && !strcmp(argv[1], "native-oom")) {
        (void)arc_error(&ctx->error, "injected", ARCHIVE_FATAL, "errno", ENOMEM, "Out of memory");
        return 99;
    }

    for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        ctx->reader = (struct archive *)(uintptr_t)1;
        ctx->reader_open = 1;
        result = statuses[i];
        assert(arc_read_next(ctx) == -1);
        assert(!strcmp(ctx->error.stage, "read entry header"));
        assert(ctx->error.native_status == result && ctx->error.system_code == 123);
        assert(!strcmp(ctx->error.message, "injected failure\xff"));
        assert(uselocale((locale_t)0) == original);
        assert(arc_read_block(ctx) == -1);
        assert(!strcmp(ctx->error.stage, "read data block"));
        assert(uselocale((locale_t)0) == original);
        assert(arc_read_skip(ctx) == -1);
        assert(!strcmp(ctx->error.stage, "skip entry data"));
        assert(uselocale((locale_t)0) == original);
        assert(arc_read_close(ctx) == -1);
        assert(!ctx->reader_open);
        assert(!strcmp(ctx->error.stage, "close reader"));
        assert(uselocale((locale_t)0) == original);
        assert(arc_read_free_handle(ctx) == -1);
        assert(!ctx->reader && !ctx->entry);
        assert(!strcmp(ctx->error.stage, "free reader"));
        assert(ctx->error.native_status == result);
        assert(uselocale((locale_t)0) == original);
    }
    assert(close_calls == 4 && free_calls == 4);
    assert(arc_read_close(ctx) == 0 && arc_read_free_handle(ctx) == 0);
    assert(close_calls == 4 && free_calls == 4);
    ctx->reader = (struct archive *)(uintptr_t)1;
    result = ARCHIVE_EOF;
    assert(arc_read_next(ctx) == 1 && arc_read_block(ctx) == 1);
    result = ARCHIVE_OK;
    assert(arc_read_next(ctx) == 0 && arc_read_block(ctx) == 0);
    assert(arc_read_free_handle(ctx) == 0);
    arc_read_free(ctx);
    assert(uselocale((locale_t)0) == original);
    verify_open_failures(original);
    puts("Shared reader partial-open, cleanup failure and locale checks passed");
    arc_probe_end_locale(previous);
    return 0;
}
