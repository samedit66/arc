/* Exercise writer failures and scoped locale restoration at the native boundary. */
#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "probe_locale.h"

static int fail_read, fail_write, fail_finish, fail_close, fail_free, fail_remove;

static la_ssize_t probe_read(struct archive *reader, void *buffer, size_t size)
{
    assert(MB_CUR_MAX > 1);
    if (fail_read) {
        archive_set_error(reader, EIO, "injected source read failure");
        return -1;
    }
    return archive_read_data(reader, buffer, size);
}

static la_ssize_t probe_write(struct archive *writer, const void *buffer, size_t size)
{
    assert(MB_CUR_MAX > 1);
    if (fail_write) {
        archive_set_error(writer, EIO, "injected archive write failure");
        return -1;
    }
    return archive_write_data(writer, buffer, size);
}

static int probe_finish(struct archive *writer)
{
    if (fail_finish) {
        archive_set_error(writer, EIO, "injected finish failure");
        return ARCHIVE_FATAL;
    }
    return archive_write_finish_entry(writer);
}

static int probe_close(struct archive *writer)
{
    int status = archive_write_close(writer);
    if (fail_close) {
        archive_set_error(writer, EIO, "injected close failure");
        return ARCHIVE_FATAL;
    }
    return status;
}

static int probe_free(struct archive *writer)
{
    int status = archive_write_free(writer);
    return fail_free ? ARCHIVE_FATAL : status;
}

static int probe_remove(const char *path)
{
    if (fail_remove) { errno = EACCES; return -1; }
    return unlink(path);
}

#define archive_read_data probe_read
#define archive_write_data probe_write
#define archive_write_finish_entry probe_finish
#define archive_write_close probe_close
#define archive_write_free probe_free
#define unlink probe_remove
#include "arc_pack.h"
#undef unlink

static void release(struct arc_pack_context *ctx)
{
    assert(arc_pack_close_disk(ctx) == 0);
    assert(arc_pack_free_disk(ctx) == 0);
    assert(arc_pack_close_writer(ctx) == (fail_close ? -1 : 0));
    if (fail_close) assert(!strcmp(ctx->error.stage, "finalize archive"));
    assert(arc_pack_free_writer(ctx) == (fail_free ? -1 : 0));
    if (fail_free) assert(!strcmp(ctx->error.stage, "free writer"));
    assert(arc_pack_close_output(ctx) == 0);
    assert(arc_pack_remove_output(ctx) == (fail_remove ? -1 : 0));
    if (fail_remove) assert(!strcmp(ctx->error.stage, "remove incomplete archive"));
    arc_pack_free(ctx);
}

int main(void)
{
    char input[] = "/tmp/arc-pack-input-XXXXXX";
    char output[] = "/tmp/arc-pack-output-XXXXXX";
    int input_fd = mkstemp(input), output_fd = mkstemp(output);
    void *previous = arc_probe_begin_locale();
    void *caller = arc_probe_current_locale();
    struct arc_pack_context *ctx;
    int scenario;
    assert(input_fd >= 0 && output_fd >= 0);
    assert(write(input_fd, "content", 7) == 7);
    assert(close(input_fd) == 0 && close(output_fd) == 0);
    assert(unlink(output) == 0);

    for (scenario = 0; scenario < 7; ++scenario) {
        fail_read = fail_write = fail_finish = fail_close = fail_free = fail_remove = 0;
        ctx = arc_pack_new();
        assert(arc_pack_configure(ctx, "zip", "none", NULL) == 0);
        assert(arc_pack_create_output(ctx, output) == 0);
        assert(arc_pack_open_source(ctx, input) == 0);
        assert(arc_pack_next(ctx) == 0);
        assert(arc_pack_begin_entry(ctx, "file") == 0);
        fail_read = scenario == 1;
        fail_write = scenario == 2;
        fail_finish = scenario == 3;
        fail_close = scenario == 4;
        fail_free = scenario == 5;
        fail_remove = scenario == 6;
        if (fail_read || fail_write) {
            assert(arc_pack_transfer(ctx) == (fail_read ? -2 : -1));
            assert(ctx->last_block_size == 0);
            assert(ctx->error.system_code == EIO);
        } else {
            assert(arc_pack_transfer(ctx) == 0 && ctx->last_block_size == 7);
            assert(arc_pack_transfer(ctx) == 1);
            assert(arc_pack_finish_entry(ctx) == (fail_finish ? -1 : 0));
        }
        assert(arc_probe_current_locale() == caller);
        release(ctx);
        assert(arc_probe_current_locale() == caller);
        if (fail_remove) assert(unlink(output) == 0);
        assert(access(output, F_OK) != 0);
    }
    assert(unlink(input) == 0);
    arc_probe_end_locale(previous);
    return 0;
}
