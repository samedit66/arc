#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "arc.h"
#include "internal/arc_internal.h"
#include "internal/platform_text.h"
#include "internal/support.h"

ArcResult arc_get_capabilities(const ArcOptions *o, unsigned int *out) {
    if (!o || !o->format || !o->compression || !out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing archive configuration.");
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    unsigned flags = 0;
    r = locale_end(&scope, arc_get_capabilities_impl(o, &flags));
    if (!ar_failed(r))
        *out = flags;
    return r;
}

ArcResult arc_create(const char *path, ArcWriter **out) {
    if (!out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing writer output.");
    *out = NULL;
    if (!path || !*path)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing archive path.");
    const ArchiveSuffix *suffix = ar_suffix_of(path, 1);
    if (!suffix)
        return ar_result(ARC_INVALID_ARGUMENT, "Cannot infer output format; use arc_create_ex.");
    ArcOptions options = {suffix->format, suffix->compression, NULL};
    return arc_create_ex(path, &options, out);
}

ArcResult arc_create_ex(const char *path, const ArcOptions *o, ArcWriter **out) {
    if (!out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing writer output.");
    *out = NULL;
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    ArcWriter *writer = NULL;
    r = locale_end(&scope, arc_create_impl(path, o, &writer));
    if (ar_failed(r) && writer) {
        ArcResult cleanup = release_writer(&writer, 1);
        r = ar_cleanup(r, cleanup);
    }
    if (!ar_failed(r))
        *out = writer;
    return r;
}

#define WRITER_OPERATION(expression)                                                               \
    do {                                                                                           \
        if (!writer)                                                                               \
            return ar_result(ARC_INVALID_ARGUMENT, "Missing writer.");                             \
        LocaleScope scope;                                                                         \
        ArcResult r = locale_begin(&scope);                                                        \
        if (ar_failed(r))                                                                          \
            return writer_failure(writer, r);                                                      \
        return writer_failure(writer, locale_end(&scope, (expression)));                           \
    } while (0)

ArcResult arc_begin_file(ArcWriter *writer, const char *name, uint64_t size) {
    WRITER_OPERATION(arc_begin_file_impl(writer, name, size));
}

ArcResult arc_write_chunk(ArcWriter *writer, const void *data, size_t size) {
    WRITER_OPERATION(arc_write_chunk_impl(writer, data, size));
}

ArcResult arc_end_file(ArcWriter *writer) {
    WRITER_OPERATION(arc_end_file_impl(writer));
}

ArcResult arc_add_text(ArcWriter *writer, const char *name, const char *text) {
    WRITER_OPERATION(arc_add_text_impl(writer, name, text));
}

ArcResult arc_add_file(ArcWriter *writer, const char *source, const char *name) {
    WRITER_OPERATION(arc_add_file_impl(writer, source, name));
}

ArcResult arc_add_path(ArcWriter *writer, const char *source, const char *name) {
    WRITER_OPERATION(arc_add_path_impl(writer, source, name));
}

#undef WRITER_OPERATION

static ArcResult close_with_locale(ArcWriter **writer, int discard) {
    if (!writer || !*writer)
        return release_writer(writer, discard);
    ArcResult previous = discard ? ar_okay() : writer_previous_failure(*writer);
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r)) {
        /* Consume the handle even on failure; backend fatal errors are excluded. */
        ArcResult cleanup = release_writer(writer, discard);
        return ar_failed(previous) ? ar_cleanup(cleanup, r) : ar_cleanup(r, cleanup);
    }
    return locale_end(&scope, release_writer(writer, discard));
}

ArcResult arc_close(ArcWriter **writer) {
    return close_with_locale(writer, 0);
}

ArcResult arc_discard(ArcWriter **writer) {
    return close_with_locale(writer, 1);
}

ArcResult arc_list(const char *path, ArcVisitor visitor, void *context) {
    return arc_list_ex(path, NULL, visitor, context);
}

ArcResult arc_list_ex(const char *path, const ArcReadOptions *options, ArcVisitor visitor,
                      void *context) {
    if (!visitor)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing archive visitor.");
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    r = arc_list_impl(path, options, visitor, context);
    ArcResult restored = locale_end(&scope, r);
    return ar_failed(r) ? r : restored;
}

ArcResult arc_get_size(const char *path, uint64_t *bytes) {
    return arc_get_size_impl(path, bytes);
}

ArcResult arc_inspect(const char *path, ArcInfo *info) {
    return arc_inspect_ex(path, NULL, info);
}

ArcResult arc_inspect_ex(const char *path, const ArcReadOptions *options, ArcInfo *info) {
    if (!info)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing statistics output.");
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    ArcInfo temporary = {0};
    r = locale_end(&scope, arc_inspect_impl(path, options, &temporary));
    if (!ar_failed(r))
        *info = temporary;
    return r;
}

int arc_ok(ArcResult result) {
    return result.code == ARC_OK;
}

ArcResult arc_unpack(const char *path, const char *root) {
    ArcUnpackOptions options = {.destination_mode = ARC_DESTINATION_NEW};
    return arc_unpack_ex(path, root, &options);
}

ArcResult arc_unpack_ex(const char *path, const char *root, const ArcUnpackOptions *o) {
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    return locale_end(&scope, arc_unpack_impl(path, root, o));
}

ArcResult arc_verify(const char *path) {
    return arc_verify_ex(path, NULL);
}

ArcResult arc_verify_ex(const char *path, const ArcReadOptions *options) {
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    return locale_end(&scope, arc_verify_impl(path, options));
}

ArcResult arc_reader_open(const char *path, ArcReader **out) {
    return arc_reader_open_ex(path, NULL, out);
}

ArcResult arc_reader_open_ex(const char *path, const ArcReadOptions *options, ArcReader **out) {
    if (!out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing reader output.");
    *out = NULL;
    if (!path || !*path || !ar_valid_utf8(path))
        return ar_result(ARC_INVALID_ARGUMENT, "Missing or invalid archive path.");
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    ArcReader *reader = NULL;
    r = locale_end(&scope, arc_reader_open_impl(path, options, &reader));
    if (ar_failed(r) && reader) {
        ArcResult cleanup = arc_reader_close_impl(&reader);
        r = ar_cleanup(r, cleanup);
    }
    if (!ar_failed(r))
        *out = reader;
    return r;
}

ArcResult arc_reader_next(ArcReader *reader, ArcEntry *entry, int *end) {
    if (!entry || !end)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing entry or EOF output.");
    ArcResult r = sequential_reader_ready(reader, 0);
    if (ar_failed(r))
        return r;
    LocaleScope scope;
    r = locale_begin(&scope);
    if (ar_failed(r))
        return sequential_reader_failure(reader, r);
    ArcEntry temporary = {0};
    int archive_end = 0;
    r = arc_reader_next_impl(reader, &temporary, &archive_end);
    ArcResult restored = locale_end(&scope, r);
    if (ar_failed(r))
        return sequential_reader_failure(reader, r);
    if (ar_failed(restored))
        return sequential_reader_failure(reader, restored);
    *entry = temporary;
    *end = archive_end;
    return r;
}

ArcResult arc_reader_read(ArcReader *reader, void *buffer, size_t capacity, size_t *received) {
    if (!buffer || !capacity || !received)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing read buffer, capacity or output.");
    ArcResult r = sequential_reader_ready(reader, 1);
    if (ar_failed(r))
        return r;
    LocaleScope scope;
    r = locale_begin(&scope);
    if (ar_failed(r))
        return sequential_reader_failure(reader, r);
    size_t temporary = 0;
    r = arc_reader_read_impl(reader, buffer, capacity, &temporary);
    ArcResult restored = locale_end(&scope, r);
    if (ar_failed(r))
        return sequential_reader_failure(reader, r);
    if (ar_failed(restored))
        return sequential_reader_failure(reader, restored);
    *received = temporary;
    return r;
}

ArcResult arc_reader_skip(ArcReader *reader) {
    ArcResult r = sequential_reader_ready(reader, 1);
    if (ar_failed(r))
        return r;
    LocaleScope scope;
    r = locale_begin(&scope);
    if (ar_failed(r))
        return sequential_reader_failure(reader, r);
    r = arc_reader_skip_impl(reader);
    ArcResult restored = locale_end(&scope, r);
    return sequential_reader_failure(reader, ar_failed(r) ? r : restored);
}

ArcResult arc_reader_close(ArcReader **reader) {
    if (!reader)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing reader pointer.");
    if (!*reader)
        return ar_okay();
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r)) {
        ArcResult cleanup = arc_reader_close_impl(reader);
        return ar_failed(cleanup) ? cleanup : r;
    }
    r = arc_reader_close_impl(reader);
    ArcResult restored = locale_end(&scope, r);
    return ar_failed(r) ? r : restored;
}
