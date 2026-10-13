#include "arc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    ArcOptions options = {"tar", "none", NULL};
    unsigned caps = 0;
    assert(arc_get_capabilities(&options, &caps).code == ARC_OK);
    assert(caps & ARC_CAN_CREATE);
    unsigned previous = caps;
    options.format = "unknown";
    assert(arc_get_capabilities(&options, &caps).code == ARC_UNSUPPORTED);
    assert(caps == previous);
    ArcWriter *writer = NULL;
    assert(arc_create("data.tar", &writer).code == ARC_OK);
    assert(arc_begin_file(writer, "data", 4).code == ARC_OK);
    const unsigned char data[] = {0, 1, 2, 3};
    assert(arc_write_chunk(writer, data, 2).code == ARC_OK);
    assert(arc_write_chunk(writer, data + 2, 2).code == ARC_OK);
    assert(arc_end_file(writer).code == ARC_OK);
    assert(arc_close(&writer).code == ARC_OK && !writer);
    assert(arc_close(&writer).code == ARC_OK);
    ArcReader *reader = NULL;
    assert(arc_reader_open("data.tar", &reader).code == ARC_OK);
    size_t received = 99;
    unsigned char buffer[8];
    assert(arc_reader_read(reader, buffer, sizeof buffer, &received).code == ARC_INVALID_STATE);
    assert(received == 99);
    ArcEntry entry;
    int end = 99;
    assert(arc_reader_next(reader, &entry, NULL).code == ARC_INVALID_ARGUMENT);
    assert(end == 99);
    assert(arc_reader_next(reader, &entry, &end).code == ARC_OK && !end);
    assert(!strcmp(entry.path, "data") && entry.size == 4);
    assert(arc_reader_read(reader, NULL, 1, &received).code == ARC_INVALID_ARGUMENT);
    assert(received == 99);
    assert(arc_reader_read(reader, buffer, 2, &received).code == ARC_OK && received == 2);
    assert(!memcmp(buffer, data, 2));
    assert(arc_reader_read(reader, buffer, sizeof buffer, &received).code == ARC_OK &&
           received == 2);
    assert(!memcmp(buffer, data + 2, 2));
    assert(arc_reader_read(reader, buffer, sizeof buffer, &received).code == ARC_OK && !received);
    assert(arc_reader_skip(reader).code == ARC_OK);
    assert(arc_reader_next(reader, &entry, &end).code == ARC_OK && end && !entry.path);
    assert(arc_reader_next(reader, &entry, &end).code == ARC_OK && end);
    assert(arc_reader_close(&reader).code == ARC_OK && !reader);
    assert(arc_reader_close(&reader).code == ARC_OK);
    assert(arc_reader_open("data.tar", &reader).code == ARC_OK);
    assert(arc_reader_next(reader, &entry, &end).code == ARC_OK && !end);
    assert(arc_reader_skip(reader).code == ARC_OK);
    assert(arc_reader_next(reader, &entry, &end).code == ARC_OK && end);
    assert(arc_reader_close(&reader).code == ARC_OK);
    assert(arc_create("failed.tar", &writer).code == ARC_OK);
    assert(arc_begin_file(writer, "short", 2).code == ARC_OK);
    assert(arc_write_chunk(writer, data, 3).code == ARC_SIZE_MISMATCH);
    assert(arc_write_chunk(writer, data, 1).code == ARC_INVALID_STATE);
    assert(arc_close(&writer).code == ARC_SIZE_MISMATCH && !writer);
    assert(arc_create("discard.tar", &writer).code == ARC_OK);
    assert(arc_begin_file(writer, "short", 2).code == ARC_OK);
    assert(arc_end_file(writer).code == ARC_SIZE_MISMATCH);
    assert(arc_discard(&writer).code == ARC_OK && !writer);
    assert(arc_reader_open("discard.tar", &reader).code != ARC_OK && !reader);
    puts("Public arc API checks passed.");
    return 0;
}
