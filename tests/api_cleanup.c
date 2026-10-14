#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include "arc.h"
#include "filekit/filekit.h"
#include <archive.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int fail_close, fail_remove;

static int close_archive(struct archive *archive) {
    int status = archive_write_close(archive);
    if (fail_close) {
        archive_set_error(archive, EIO, "Injected finalization failure.");
        return ARCHIVE_FATAL;
    }
    return status;
}

static FilekitResult remove_output(const char *path) {
    if (fail_remove)
        return (FilekitResult){FILEKIT_IO_ERROR, "Injected removal failure."};
    return filekit_remove_file(path);
}

/* Exercise real writer ownership/cleanup, substituting only the failing effects. */
#define archive_write_close close_archive
#define filekit_remove_file remove_output
#include "internal/writer.c"
#undef archive_write_close
#undef filekit_remove_file

int main(void) {
    FILE *source = fopen("source", "wb");
    assert(source && fputs("payload", source) >= 0 && fclose(source) == 0);
    fail_close = 1;
    ArcResult result = arc_pack("finalize.tar", "source");
    assert(result.code == ARC_IO_ERROR && !result.partial_output);
    assert(strstr(result.message, "Injected finalization"));
    assert(!fopen("finalize.tar", "rb"));
    fail_remove = 1;
    result = arc_pack("leftover.tar", "source");
    assert(result.code == ARC_IO_ERROR && result.partial_output);
    assert(strstr(result.message, "Injected finalization"));
    assert(strstr(result.cleanup_message, "Injected removal"));
    fail_close = 0;
    const char *sources[] = {"source", "source"};
    result = arc_pack_ex("conflict.tar", sources, 2, NULL);
    assert(result.code == ARC_ALREADY_EXISTS && result.reason == ARC_REASON_ARCHIVE_CONFLICT);
    assert(result.partial_output && !strcmp(result.path, "source"));
    assert(strstr(result.cleanup_message, "Injected removal"));
    fail_remove = 0;
    assert(filekit_remove_file("leftover.tar").code == FILEKIT_OK);
    assert(filekit_remove_file("conflict.tar").code == FILEKIT_OK);
    puts("Pack finalization and cleanup failures passed.");
    return 0;
}
