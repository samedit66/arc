/* Fault injection at the public libarchive boundary, not inside production code. */
#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <locale.h>
#include <stdlib.h>
#include "probe_locale.h"
#ifdef __APPLE__
#include <xlocale.h>
#endif

static int read_result, write_result, finish_result, close_result, free_result;
static int write_calls, close_calls, free_calls;
static const char payload[] = "block";

static int probe_read(struct archive *reader, const void **buffer, size_t *size, la_int64_t *offset)
{
#ifndef _WIN32
    assert(MB_CUR_MAX > 1); /* The caller uses C; the native operation must use UTF-8. */
#endif
    (void)reader;
    *buffer = payload;
    *size = sizeof(payload);
    *offset = 1048576;
    return read_result;
}

static la_ssize_t probe_write(struct archive *writer, const void *buffer, size_t size, la_int64_t offset)
{
    (void)writer;
    assert(buffer == payload);
    assert(size == sizeof(payload));
    assert(offset == 1048576);
    ++write_calls;
    return write_result;
}

static int probe_finish(struct archive *writer)
{
    (void)writer;
    return finish_result;
}

static int probe_close(struct archive *handle)
{
    (void)handle;
    ++close_calls;
    return close_result;
}

static int probe_free(struct archive *handle)
{
    (void)handle;
    ++free_calls;
    return free_result;
}

static int probe_errno(struct archive *handle)
{
    (void)handle;
    return 123;
}

static const char *probe_error(struct archive *handle)
{
    (void)handle;
    return "injected failure\xff";
}

#define archive_read_data_block probe_read
#define archive_write_data_block probe_write
#define archive_write_finish_entry probe_finish
#define archive_write_close probe_close
#define archive_read_close probe_close
#define archive_write_free probe_free
#define archive_read_free probe_free
#define archive_errno probe_errno
#define archive_error_string probe_error
#define calloc probe_calloc
static void *probe_calloc(size_t, size_t);
#include "arc_unpack.h"
#undef calloc

static int fail_allocation;
static void *probe_calloc(size_t count, size_t size)
{
    if (fail_allocation) return NULL;
    return calloc(count, size);
}

int main(int argc, char **argv)
{
    struct arc_unpack_context ctx = {0};
    int statuses[] = {ARCHIVE_WARN, ARCHIVE_RETRY, ARCHIVE_FAILED, ARCHIVE_FATAL};
    size_t i;
#ifndef _WIN32
    void *previous = arc_probe_begin_locale();
    locale_t original = uselocale((locale_t)0);
    assert(MB_CUR_MAX == 1);
    ctx.input.locale = arc_new_locale(&ctx.input.error);
    assert(ctx.input.locale);
#endif

    if (argc > 1 && !strcmp(argv[1], "allocation-oom")) {
        fail_allocation = 1;
        (void)arc_unpack_new();
        return 99;
    }
    if (argc > 1 && !strcmp(argv[1], "native-oom")) {
        (void)arc_error(&ctx.input.error, "injected", ARCHIVE_FATAL, "errno", ENOMEM, "Out of memory");
        return 99;
    }

    ctx.input.reader = (struct archive *)(uintptr_t)1;
    ctx.writer = (struct archive *)(uintptr_t)2;
    ctx.input.reader_open = 1;

    for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        read_result = statuses[i];
        write_calls = 0;
        assert(arc_unpack_transfer_block(&ctx) == -1);
        assert(write_calls == 0);
        assert(ctx.last_block_size == 0);
        assert(ctx.input.block_size == 0 && ctx.input.block_offset == 0);
        assert(!strcmp(ctx.input.error.stage, "read data block"));
        assert(ctx.input.error.system_code == 123);
        assert(ctx.input.error.native_status == statuses[i]);
        assert(!strcmp(ctx.input.error.system_domain, "errno"));
        assert(!strcmp(ctx.input.error.message, "injected failure\xff"));
#ifndef _WIN32
        assert(uselocale((locale_t)0) == original);
#endif

        read_result = ARCHIVE_OK;
        write_result = statuses[i];
        assert(arc_unpack_transfer_block(&ctx) == -2);
        assert(write_calls == 1);
        assert(ctx.last_block_size == 0);
        assert(!strcmp(ctx.input.error.stage, "write data block"));
#ifndef _WIN32
        assert(uselocale((locale_t)0) == original);
#endif
    }

    write_calls = 0;
    read_result = ARCHIVE_EOF;
    assert(arc_unpack_transfer_block(&ctx) == 1);
    assert(write_calls == 0);

    read_result = write_result = ARCHIVE_OK;
    assert(arc_unpack_transfer_block(&ctx) == 0);
    assert(ctx.last_block_size == sizeof(payload));
    assert(ctx.input.block_size == sizeof(payload) && ctx.input.block_offset == 1048576);
#ifndef _WIN32
    assert(uselocale((locale_t)0) == original);
#endif

    finish_result = ARCHIVE_WARN;
    assert(arc_unpack_finish_entry(&ctx) == -1);
    assert(!strcmp(ctx.input.error.stage, "finish entry"));

    close_result = ARCHIVE_FAILED;
    assert(arc_unpack_close_writer(&ctx) == -1);
    assert(!strcmp(ctx.input.error.stage, "close writer"));
    assert(arc_read_close(&ctx.input) == -1);
    assert(!strcmp(ctx.input.error.stage, "close reader"));
    assert(close_calls == 2);

    free_result = ARCHIVE_FATAL;
    assert(arc_unpack_free_writer(&ctx) == -1);
    assert(!strcmp(ctx.input.error.stage, "free writer"));
    assert(arc_read_free_handle(&ctx.input) == -1);
    assert(!strcmp(ctx.input.error.stage, "free reader"));
    assert(!ctx.input.reader && !ctx.writer);
    assert(arc_unpack_free_writer(&ctx) == 0);
    assert(arc_read_free_handle(&ctx.input) == 0);
    assert(free_calls == 2);

    assert(arc_error(&ctx.input.error, "windows", 0, "win32", 5, "access denied") == -1);
    assert(!strcmp(ctx.input.error.system_domain, "win32"));
    assert(ctx.input.error.system_code == 5);

    assert(arc_errno_error(&ctx.input.error, "saved errno", EACCES) == -1);
    assert(ctx.input.error.system_code == EACCES);
    assert(!strcmp(ctx.input.error.system_domain, "errno"));
    assert(!strcmp(ctx.input.error.message, strerror(EACCES)));
    free(ctx.input.error.message);
#ifndef _WIN32
    assert(uselocale((locale_t)0) == original);
    arc_free_locale(ctx.input.locale);
    arc_probe_end_locale(previous);
#endif
    puts("Native diagnostic fields, failure propagation and scoped locale passed");
    return 0;
}
