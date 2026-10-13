#include "arc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ArcResult entry(const ArcEntry *e, void *context) {
    int *count = context;
    if (e->type == ARC_ENTRY_FILE) {
        assert(!strcmp(e->path, "value"));
        ++*count;
    }
    return (ArcResult){ARC_OK, {0}};
}

int main(void) {
    const char *formats[] = {"tar", "zip", "7z", "cpio", "ar", "iso9660", "xar", "warc"};
    const char *filters[] = {"none", "gzip", "bzip2", "xz", "zstd"};
    for (size_t i = 0; i < sizeof formats / sizeof *formats; ++i) {
        for (size_t j = 0; j < sizeof filters / sizeof *filters; ++j) {
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
            assert(arc_create_ex(path, &options, &writer).code == ARC_OK);
            assert(arc_add_text(writer, "value", "payload").code == ARC_OK);
            assert(arc_close(&writer).code == ARC_OK);
            if (caps & ARC_CAN_LIST) {
                int count = 0;
                assert(arc_list(path, entry, &count).code == ARC_OK && count == 1);
            }
            if (caps & ARC_CAN_VERIFY)
                assert(arc_verify(path).code == ARC_OK);
            ArcInfo info = {0};
            if (caps & ARC_CAN_INSPECT) {
                assert(arc_inspect(path, &info).code == ARC_OK);
                assert(info.file_count == 1 && info.content_bytes == 7);
            } else if (!strcmp(formats[i], "zip"))
                assert(arc_inspect(path, &info).code == ARC_UNSUPPORTED);
            if (caps & ARC_CAN_UNPACK) {
                ArcUnpackOptions unpack = {ARC_DESTINATION_NEW, NULL};
                assert(arc_unpack(path, root, &unpack).code == ARC_OK);
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
