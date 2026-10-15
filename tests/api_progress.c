#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#endif
#include "arc.h"
#include "fs/fs.h"
#include "internal/progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PAYLOAD_SIZE 700000u

typedef struct {
    ArcProgress last;
    unsigned calls, data_updates;
    uint64_t seen[3];
    int phases[3], unknown_size, original_path;
} Observation;

static void observe(const ArcProgress *p, void *context) {
    Observation *o = context;
    assert(p->phase >= ARC_PHASE_PREPARING && p->phase <= ARC_PHASE_FINALIZING);
    assert(!o->calls || p->phase >= o->last.phase);
    assert(p->content_bytes >= o->last.content_bytes);
    assert(p->entries_seen >= o->seen[p->phase]);
    if (p->content_bytes > o->last.content_bytes)
        ++o->data_updates;
    if (p->phase == ARC_PHASE_PREPARING)
        assert(!p->content_bytes);
    if (p->path) {
        assert(*p->path);
        if (!p->entry_size_known)
            o->unknown_size = 1;
        if (!strcmp(p->path, "source/data"))
            o->original_path = 1;
    }
    o->seen[p->phase] = p->entries_seen;
    o->phases[p->phase] = 1;
    o->last = *p;
    o->last.path = NULL; /* Borrowed strings must not escape the callback. */
    ++o->calls;
}

static ArcObserver observer(Observation *o) {
    return (ArcObserver){observe, o};
}

static void completed(const Observation *o, uint64_t bytes) {
    assert(o->calls && o->phases[0] && o->phases[1] && o->phases[2]);
    assert(o->last.phase == ARC_PHASE_FINALIZING);
    assert(o->last.content_bytes == bytes);
    assert(!o->last.entry_bytes && !o->last.entry_size_known);
}

static ArcResult count_entry(const ArcEntry *entry, void *context) {
    assert(entry->path);
    ++*(unsigned *)context;
    return (ArcResult){.code = ARC_OK};
}

static ArcResult check_progress_entry(const ArcEntry *entry, void *context) {
    const Observation *o = context;
    assert(o->last.phase == ARC_PHASE_PROCESSING);
    assert(o->last.entry_size_known == entry->size_known);
    if (entry->size_known)
        assert(o->last.entry_size == entry->size);
    return (ArcResult){.code = ARC_OK};
}

static ArcResult stop_entry(const ArcEntry *entry, void *context) {
    (void)entry;
    (void)context;
    return (ArcResult){.code = ARC_NOT_FOUND, .message = "visitor stopped"};
}

static void source_file(const char *path, size_t bytes) {
    FILE *f = fopen(path, "wb");
    assert(f);
    for (size_t i = 0; i < bytes; ++i)
        assert(fputc((int)(i % 251), f) != EOF);
    assert(fclose(f) == 0);
}

static void packing_options(void) {
    const char *source = "source/data";
    ArcPackOptions options = {.password = "secret"};
    ArcOptions encoding = {"zip", "none", "secret"};
    unsigned caps = 0;
    assert(arc_get_capabilities(&encoding, &caps).code == ARC_OK);
    if (caps & ARC_CAN_CREATE) {
        Observation o = {0};
        options.observer = observer(&o);
        assert(arc_pack_ex("encrypted.zip", &source, 1, &options).code == ARC_OK);
        completed(&o, PAYLOAD_SIZE);
        o = (Observation){0};
        ArcReadOptions reading = {.password = "secret", .observer = observer(&o)};
        assert(arc_verify_ex("encrypted.zip", &reading).code == ARC_OK);
        completed(&o, PAYLOAD_SIZE);
        reading.password = NULL;
        o = (Observation){0};
        assert(arc_verify_ex("encrypted.zip", &reading).code == ARC_PASSWORD_REQUIRED);
        reading.password = "wrong";
        o = (Observation){0};
        assert(arc_verify_ex("encrypted.zip", &reading).code != ARC_OK);
    }
    options = (ArcPackOptions){0};
    assert(arc_pack_ex("inferred.tar.gz", &source, 1, &options).code == ARC_OK);
    FILE *f = fopen("inferred.tar.gz", "rb");
    assert(f && fgetc(f) == 0x1f && fgetc(f) == 0x8b && fclose(f) == 0);
    options.compression = "none";
    assert(arc_pack_ex("plain.tar.gz", &source, 1, &options).code == ARC_OK);
    f = fopen("plain.tar.gz", "rb");
    assert(f && fgetc(f) != 0x1f && fclose(f) == 0);
    options = (ArcPackOptions){.format = "tar"};
    assert(arc_pack_ex("explicit.tar.gz", &source, 1, &options).code == ARC_OK);
    f = fopen("explicit.tar.gz", "rb");
    assert(f && fgetc(f) != 0x1f && fclose(f) == 0);
    assert(arc_pack_ex("explicit.data", &source, 1, &options).code == ARC_OK);
    options = (ArcPackOptions){0};
    assert(arc_pack_ex("unknown.data", &source, 1, &options).code == ARC_INVALID_ARGUMENT);
    options.format = "";
    assert(arc_pack_ex("invalid.zip", &source, 1, &options).code == ARC_INVALID_ARGUMENT);
    assert(!fopen("invalid.zip", "rb"));
    options = (ArcPackOptions){.format = "7z", .compression = "gzip"};
    assert(arc_pack_ex("invalid.7z", &source, 1, &options).code == ARC_UNSUPPORTED);
    assert(!fopen("invalid.7z", "rb"));
}

static void cpio_record(FILE *f, const char *name, const char *data, unsigned inode,
                        unsigned links) {
    assert(fprintf(f, "070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o", 0u, inode, 0100644u, 0u,
                   0u, links, 0u, 0u, (unsigned)strlen(name) + 1, (unsigned)strlen(data)) > 0);
    assert(fwrite(name, 1, strlen(name) + 1, f) == strlen(name) + 1);
    assert(fwrite(data, 1, strlen(data), f) == strlen(data));
}

static void reader_variants(void) {
    Observation o = {0};
    ArcReadOptions reading = {.observer = observer(&o)};
    FILE *f = fopen("links.cpio", "wb");
    assert(f);
    cpio_record(f, "a", "data", 1, 2);
    cpio_record(f, "b", "", 1, 2);
    cpio_record(f, "TRAILER!!!", "", 0, 1);
    assert(fclose(f) == 0);
    assert(arc_verify_ex("links.cpio", &reading).code == ARC_OK);
    completed(&o, 4);
    o = (Observation){0};
    unsigned count = 0;
    assert(arc_list_ex("links.cpio", &reading, count_entry, &count).code == ARC_UNSUPPORTED);

    ArcWriter *writer = NULL;
    assert(arc_create("empty.tar", &writer).code == ARC_OK);
    assert(arc_close(&writer).code == ARC_OK);
    o = (Observation){0};
    assert(arc_verify_ex("empty.tar", &reading).code == ARC_OK);
    completed(&o, 0);
    assert(!o.seen[ARC_PHASE_PROCESSING]);

    ArcOptions encoding = {"zip", "gzip", NULL};
    unsigned caps;
    assert(arc_get_capabilities(&encoding, &caps).code == ARC_OK);
    if ((caps & (ARC_CAN_CREATE | ARC_CAN_LIST)) == (ARC_CAN_CREATE | ARC_CAN_LIST)) {
        assert(arc_create_ex("stream.zip.gz", &encoding, &writer).code == ARC_OK);
        assert(arc_add_text(writer, "data", "hello").code == ARC_OK);
        assert(arc_close(&writer).code == ARC_OK);
        o = (Observation){0};
        assert(arc_list_ex("stream.zip.gz", &reading, check_progress_entry, &o).code == ARC_OK);
        completed(&o, 0);
        /* Backend versions may know ZIP sizes even through an outer filter. */
        assert(o.seen[ARC_PHASE_PROCESSING] == 1);
        o = (Observation){0};
        assert(arc_verify_ex("stream.zip.gz", &reading).code == ARC_OK);
        completed(&o, 5);
    }

    FILE *input = fopen("root.tar", "rb");
    f = fopen("truncated.tar", "wb");
    assert(input && f);
    for (unsigned i = 0; i < 1300; ++i) {
        int c = fgetc(input);
        assert(c != EOF && fputc(c, f) != EOF);
    }
    assert(fclose(input) == 0 && fclose(f) == 0);
    o = (Observation){0};
    assert(arc_verify_ex("truncated.tar", &reading).code == ARC_INVALID_ARCHIVE);
    assert(o.last.content_bytes < PAYLOAD_SIZE);

    Observation other = {0};
    ArcReadOptions second = {.observer = observer(&other)};
    ArcReader *first = NULL, *next = NULL;
    o = (Observation){0};
    assert(arc_reader_open_ex("root.tar", &reading, &first).code == ARC_OK);
    unsigned calls = o.calls;
    assert(arc_reader_open_ex("root.tar", &second, &next).code == ARC_OK);
    assert(o.calls == calls && other.calls);
    assert(arc_reader_close(&first).code == ARC_OK);
    assert(other.last.phase == ARC_PHASE_PROCESSING);
    assert(arc_reader_close(&next).code == ARC_OK);
    completed(&o, 0);
    completed(&other, 0);
}

int main(void) {
    assert(arc_fs_create_directory_exclusive("source", 0777).code == ARC_FS_OK);
    source_file("source/data", PAYLOAD_SIZE);
    source_file("source/empty", 0);
    uint64_t records = 3;
#ifndef _WIN32
    assert(link("source/data", "source/alias") == 0);
    ++records;
#endif
    const char *source = "source";
    Observation o = {0};
    ArcPackOptions pack = {.keep_root = 1, .observer = observer(&o)};
    assert(arc_pack_ex("root.tar", &source, 1, &pack).code == ARC_OK);
    completed(&o, PAYLOAD_SIZE);
    assert(o.seen[ARC_PHASE_PROCESSING] == records && o.data_updates > 1);

    o = (Observation){0};
    ArcUnpackOptions unpack = {.strip_root = 1, .observer = observer(&o)};
    assert(arc_unpack_ex("root.tar", "stripped", &unpack).code == ARC_OK);
    completed(&o, PAYLOAD_SIZE);
    assert(o.seen[ARC_PHASE_PREPARING] == records);
    assert(o.seen[ARC_PHASE_PROCESSING] == records && o.original_path);
    assert(o.data_updates > 1);
    assert(arc_unpack_ex("root.tar", "defaults", NULL).code == ARC_OK);
    assert(arc_unpack_ex("root.tar", "defaults", NULL).code == ARC_ALREADY_EXISTS);

    const char *selected = "source/empty";
    o = (Observation){0};
    unpack = (ArcUnpackOptions){.paths = &selected, .path_count = 1, .observer = observer(&o)};
    assert(arc_unpack_ex("root.tar", "selected", &unpack).code == ARC_OK);
    completed(&o, 0);
    assert(o.seen[ARC_PHASE_PROCESSING] == records);
    selected = "missing";
    o = (Observation){0};
    ArcResult r = arc_unpack_ex("root.tar", "missing", &unpack);
    assert(r.code == ARC_NOT_FOUND && r.partial_output);

    o = (Observation){0};
    ArcReadOptions reading = {.observer = observer(&o)};
    assert(arc_verify_ex("root.tar", &reading).code == ARC_OK);
    completed(&o, PAYLOAD_SIZE);
    assert(o.seen[ARC_PHASE_PROCESSING] == records && o.data_updates > 1);
    o = (Observation){0};
    unsigned count = 0;
    assert(arc_list_ex("root.tar", &reading, count_entry, &count).code == ARC_OK);
    completed(&o, 0);
    assert(count == records && o.seen[ARC_PHASE_PROCESSING] == records);
    o = (Observation){0};
    r = arc_list_ex("root.tar", &reading, stop_entry, NULL);
    assert(r.code == ARC_NOT_FOUND && !strcmp(r.message, "visitor stopped"));
    completed(&o, 0);
    o = (Observation){0};
    ArcInfo info;
    assert(arc_inspect_ex("root.tar", &reading, &info).code == ARC_OK);
    completed(&o, 0);
    assert(info.content_bytes == PAYLOAD_SIZE);

    o = (Observation){0};
    ArcReader *reader = NULL;
    assert(arc_reader_open_ex("root.tar", &reading, &reader).code == ARC_OK);
    reading.observer = (ArcObserver){0}; /* The reader copied its observer. */
    ArcEntry entry;
    int end;
    do {
        assert(arc_reader_next(reader, &entry, &end).code == ARC_OK && !end);
    } while (entry.type != ARC_ENTRY_FILE || !entry.size);
    unsigned char buffer[17];
    size_t received;
    assert(arc_reader_read(reader, buffer, sizeof buffer, &received).code == ARC_OK);
    assert(received == sizeof buffer);
    assert(arc_reader_skip(reader).code == ARC_OK);
    assert(o.last.content_bytes == sizeof buffer);
    assert(arc_reader_close(&reader).code == ARC_OK && !reader);
    completed(&o, sizeof buffer);

    const char *sources[] = {"source/data", "source/empty"};
    o = (Observation){0};
    pack = (ArcPackOptions){.observer = observer(&o)};
    assert(arc_pack_ex("multiple.tar", sources, 2, &pack).code == ARC_OK);
    completed(&o, PAYLOAD_SIZE);
    assert(o.seen[ARC_PHASE_PROCESSING] == 2);
    reader_variants();
    packing_options();
    /* Counter overflow must not wrap or publish a misleading snapshot. */
    o = (Observation){0};
    ProgressState state = {.observer = observer(&o)};
    state.snapshot.entries_seen = UINT64_MAX;
    assert(ar_progress_entry(&state, "entry", 0, 1).code == ARC_OVERFLOW && !o.calls);
    state.snapshot.content_bytes = UINT64_MAX;
    assert(ar_progress_bytes(&state, 1).code == ARC_OVERFLOW && !o.calls);
    state.snapshot.content_bytes = 0;
    state.snapshot.entry_bytes = UINT64_MAX;
    assert(ar_progress_bytes(&state, 1).code == ARC_OVERFLOW && !o.calls);
    o = (Observation){0};
    state = (ProgressState){.observer = observer(&o)};
    ar_progress_phase(&state, ARC_PHASE_PROCESSING);
    assert(ar_progress_entry(&state, "unknown", 0, 0).code == ARC_OK);
    assert(ar_progress_bytes(&state, 5).code == ARC_OK);
    assert(o.unknown_size && !o.last.entry_size_known && o.last.entry_bytes == 5);
    puts("Progress observation and packing defaults passed.");
    return 0;
}
