#include "arc.h"
#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *current_format, *current_filter;

static void expect_result(ArcResult result, ArcCode expected, const char *operation) {
    if (result.code != expected) {
        fprintf(stderr, "%s/%s %s: code %d, expected %d: %s\n", current_format, current_filter,
                operation, result.code, expected, result.message);
        abort();
    }
}

static ArcResult entry(const ArcEntry *e, void *context) {
    int *count = context;
    if (e->type == ARC_ENTRY_FILE) {
        assert(!strcmp(e->path, "value") || !strcmp(e->path, "value2"));
        ++*count;
    }
    return (ArcResult){.code = ARC_OK, .message = {0}};
}

static void check_warc_reader(void) {
    const char record[] =
        "WARC/1.0\r\nWARC-Type: resource\r\nWARC-Target-URI: file://value\r\n"
        "WARC-Date: 2026-01-01T00:00:00Z\r\nContent-Length: 7\r\n\r\npayload\r\n\r\n"
        "WARC/1.0\r\nWARC-Type: resource\r\nWARC-Target-URI: file://value2\r\n"
        "WARC-Date: 2026-01-01T00:00:00Z\r\nContent-Length: 7\r\n\r\npayload\r\n\r\n";
    FILE *file = fopen("fixture.warc", "wb");
    assert(file && fwrite(record, 1, sizeof record - 1, file) == sizeof record - 1);
    assert(fclose(file) == 0);
    current_format = "warc";
    current_filter = "none";
    ArcCode expected = archive_version_number() < 3008009 ? ARC_UNSUPPORTED : ARC_OK;
    ArcOptions options = {"warc", "none", NULL};
    unsigned caps = 0;
    expect_result(arc_get_capabilities(&options, &caps), expected, "capabilities");
    expect_result(arc_verify("fixture.warc"), expected, "fixture verify");
    int count = 0;
    expect_result(arc_list("fixture.warc", entry, &count), expected, "fixture list");
    assert(count == (expected == ARC_OK ? 2 : 0));
    ArcInfo info = {0};
    expect_result(arc_inspect("fixture.warc", &info), expected, "fixture inspect");
    if (expected == ARC_OK)
        assert(info.file_count == 2 && info.content_bytes == 14);
    expect_result(arc_unpack("fixture.warc", "fixture-out"), expected, "fixture unpack");
}

static void check_xar_reader(void) {
    /* Generate a fixture independently of Arc's capability gate. */
    struct archive *writer = archive_write_new();
    assert(writer);
    if (archive_write_set_format_xar(writer) != ARCHIVE_OK) {
        assert(archive_write_free(writer) == ARCHIVE_OK);
        return;
    }
    assert(archive_write_open_filename(writer, "fixture.xar") == ARCHIVE_OK);
    const char *names[] = {"value", "value2"};
    for (size_t i = 0; i < 2; ++i) {
        struct archive_entry *e = archive_entry_new();
        assert(e);
        archive_entry_set_pathname(e, names[i]);
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, 7);
        assert(archive_write_header(writer, e) == ARCHIVE_OK);
        assert(archive_write_data(writer, "payload", 7) == 7);
        archive_entry_free(e);
    }
    assert(archive_write_close(writer) == ARCHIVE_OK);
    assert(archive_write_free(writer) == ARCHIVE_OK);
    current_format = "xar";
    current_filter = "none";
    ArcCode expected = archive_version_number() < 3008002 ? ARC_UNSUPPORTED : ARC_OK;
    ArcOptions options = {"xar", "none", NULL};
    unsigned caps = 0;
    expect_result(arc_get_capabilities(&options, &caps), expected, "capabilities");
    expect_result(arc_verify("fixture.xar"), expected, "fixture verify");
    int count = 0;
    expect_result(arc_list("fixture.xar", entry, &count), expected, "fixture list");
    assert(count == (expected == ARC_OK ? 2 : 0));
    ArcInfo info = {0};
    expect_result(arc_inspect("fixture.xar", &info), expected, "fixture inspect");
    if (expected == ARC_OK)
        assert(info.file_count == 2 && info.content_bytes == 14);
    expect_result(arc_unpack("fixture.xar", "fixture-xar-out"), expected, "fixture unpack");
}

int main(void) {
    check_warc_reader();
    check_xar_reader();
    const char *formats[] = {"tar", "zip", "7z", "cpio", "ar", "iso9660", "xar", "warc"};
    const char *filters[] = {"none", "gzip", "bzip2", "xz", "zstd"};
    for (size_t i = 0; i < sizeof formats / sizeof *formats; ++i) {
        for (size_t j = 0; j < sizeof filters / sizeof *filters; ++j) {
            current_format = formats[i];
            current_filter = filters[j];
            ArcOptions options = {formats[i], filters[j], NULL};
            unsigned caps = 0xabcdef;
            ArcResult r = arc_get_capabilities(&options, &caps);
            char path[64], root[64];
            snprintf(path, sizeof path, "matrix-%s-%s.arc", formats[i], filters[j]);
            snprintf(root, sizeof root, "matrix-%s-%s-out", formats[i], filters[j]);
            ArcWriter *writer = NULL;
            if (r.code == ARC_UNSUPPORTED || !(caps & ARC_CAN_CREATE)) {
                assert(r.code == ARC_OK || r.code == ARC_UNSUPPORTED);
                if (r.code != ARC_OK)
                    assert(caps == 0xabcdef);
                assert(arc_create_ex(path, &options, &writer).code == ARC_UNSUPPORTED && !writer);
                FILE *absent = fopen(path, "rb");
                assert(!absent);
                continue;
            }
            assert(r.code == ARC_OK);
            expect_result(arc_create_ex(path, &options, &writer), ARC_OK, "create");
            expect_result(arc_begin_file(writer, "value", 7), ARC_OK, "begin chunk entry");
            expect_result(arc_write_chunk(writer, "pay", 3), ARC_OK, "first chunk");
            expect_result(arc_write_chunk(writer, "load", 4), ARC_OK, "second chunk");
            expect_result(arc_end_file(writer), ARC_OK, "end chunk entry");
            expect_result(arc_add_text(writer, "value2", "payload"), ARC_OK, "add second text");
            expect_result(arc_close(&writer), ARC_OK, "close");
            if (caps & ARC_CAN_LIST) {
                int count = 0;
                expect_result(arc_list(path, entry, &count), ARC_OK, "list");
                assert(count == 2);
            }
            if (caps & ARC_CAN_VERIFY)
                expect_result(arc_verify(path), ARC_OK, "verify");
            ArcInfo info = {0};
            if (caps & ARC_CAN_INSPECT) {
                expect_result(arc_inspect(path, &info), ARC_OK, "inspect");
                assert(info.file_count == 2 && info.content_bytes == 14);
            } else if (!strcmp(formats[i], "zip")) {
                r = arc_inspect(path, &info);
                assert(r.code == ARC_OK || r.code == ARC_UNSUPPORTED);
                if (r.code == ARC_OK)
                    assert(info.file_count == 2 && info.content_bytes == 14);
            }
            if (caps & ARC_CAN_UNPACK) {
                expect_result(arc_unpack(path, root), ARC_OK, "unpack");
                char extracted[128];
                snprintf(extracted, sizeof extracted, "%s/value", root);
                FILE *file = fopen(extracted, "rb");
                assert(file);
                char data[8] = {0};
                assert(fread(data, 1, sizeof data, file) == 7 && !strcmp(data, "payload"));
                assert(fclose(file) == 0);
            }
        }
    }
    ArcOptions ar = {"ar", "none", NULL};
    ArcWriter *writer = NULL;
    assert(arc_create_ex("flat.ar", &ar, &writer).code == ARC_OK);
    assert(arc_add_text(writer, "first/value", "first").code == ARC_UNSUPPORTED);
    assert(arc_close(&writer).code == ARC_UNSUPPORTED && !writer);
    assert(arc_create_ex("discard.ar", &ar, &writer).code == ARC_OK);
    assert(arc_begin_file(writer, "second/value", 0).code == ARC_UNSUPPORTED);
    assert(arc_discard(&writer).code == ARC_OK && !writer);
    FILE *source = fopen("source", "wb");
    assert(source && fputs("source", source) >= 0 && fclose(source) == 0);
    assert(arc_create_ex("file.ar", &ar, &writer).code == ARC_OK);
    assert(arc_add_file(writer, "source", "third/value").code == ARC_UNSUPPORTED);
    assert(arc_discard(&writer).code == ARC_OK);
    assert(arc_create_ex("path.ar", &ar, &writer).code == ARC_OK);
    assert(arc_add_path(writer, "source", "fourth/value").code == ARC_UNSUPPORTED);
    assert(arc_discard(&writer).code == ARC_OK);
    puts("Format/filter public API matrix passed.");
    return 0;
}
