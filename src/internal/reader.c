#include "support.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "arc_internal.h"
#include "libarchive_adapter.h"

/* The sequential reader owns only the current entry text, never previous records. */
struct ArcReader {
    struct archive *backend;
    char *name, *target;
    ArcEntry entry;
    uint64_t bytes_read;
    int has_entry, body_done, archive_end;
    int size_set;
    la_int64_t declared_size;
    ArcResult failure;
};

ArcResult sequential_reader_ready(ArcReader *reader, int require_entry) {
    if (!reader)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing reader.");
    if (ar_failed(reader->failure))
        return ar_result(ARC_INVALID_STATE, "Reader has failed: %.200s", reader->failure.message);
    if (require_entry && !reader->has_entry)
        return ar_result(ARC_INVALID_STATE, "Reader has no current entry.");
    return ar_okay();
}

ArcResult sequential_reader_failure(ArcReader *reader, ArcResult r) {
    if (reader && ar_failed(r) && !ar_failed(reader->failure))
        reader->failure = r;
    return r;
}

static void clear_reader_entry(ArcReader *reader) {
    free(reader->name);
    free(reader->target);
    reader->name = reader->target = NULL;
    memset(&reader->entry, 0, sizeof reader->entry);
    reader->has_entry = reader->body_done = reader->size_set = 0;
    reader->bytes_read = 0;
}

ArcResult arc_reader_open_impl(const char *path, const ArcReadOptions *options, ArcReader **out) {
    ArcReader *reader = calloc(1, sizeof *reader);
    if (!reader)
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate reader.");
    ArcResult r = reader_open(path, options ? options->password : NULL, &reader->backend);
    if (ar_failed(r)) {
        free(reader);
        return r;
    }
    *out = reader;
    return ar_okay();
}

ArcResult arc_reader_skip_impl(ArcReader *reader) {
    if (reader->body_done)
        return ar_okay();
    if (archive_read_data_skip(reader->backend) != ARCHIVE_OK)
        return sequential_reader_failure(reader,
                                         backend_error(reader->backend, ARC_INVALID_ARCHIVE));
    reader->body_done = 1;
    return ar_okay();
}

ArcResult arc_reader_next_impl(ArcReader *reader, ArcEntry *entry, int *end) {
    if (reader->archive_end) {
        memset(entry, 0, sizeof *entry);
        *end = 1;
        return ar_okay();
    }
    ArcResult r = ar_okay();
    if (reader->has_entry)
        r = arc_reader_skip_impl(reader);
    /* A valid next call ends the previous metadata lifetime even if skipping fails. */
    clear_reader_entry(reader);
    if (ar_failed(r))
        return r;
    struct archive_entry *e = NULL;
    int archive_end;
    r = read_header(reader->backend, &e, &archive_end);
    if (ar_failed(r))
        return sequential_reader_failure(reader, r);
    if (archive_end) {
        reader->archive_end = 1;
        memset(entry, 0, sizeof *entry);
        *end = 1;
        return ar_okay();
    }
    r = normalized_entry(reader->backend, e);
    if (!ar_failed(r))
        r = entry_text(e, 0, &reader->name);
    ArcEntryType type = entry_type(e);
    if (!ar_failed(r) && (type == ARC_ENTRY_SYMBOLIC_LINK || type == ARC_ENTRY_HARD_LINK))
        r = entry_text(e, type == ARC_ENTRY_SYMBOLIC_LINK ? 1 : 2, &reader->target);
    if (ar_failed(r))
        return sequential_reader_failure(reader, r);
    reader->size_set = archive_entry_size_is_set(e);
    reader->declared_size = archive_entry_size(e);
    int known = reader->size_set && reader->declared_size >= 0;
    reader->entry = (ArcEntry){reader->name, type, known ? (uint64_t)reader->declared_size : 0,
                               known, reader->target};
    reader->has_entry = 1;
    *entry = reader->entry;
    *end = 0;
    return ar_okay();
}

ArcResult arc_reader_read_impl(ArcReader *reader, void *buffer, size_t capacity, size_t *received) {
    if (reader->body_done || reader->entry.type != ARC_ENTRY_FILE) {
        *received = 0;
        return ar_okay();
    }
    size_t limit = capacity > (size_t)PTRDIFF_MAX ? (size_t)PTRDIFF_MAX : capacity;
    la_ssize_t n = archive_read_data(reader->backend, buffer, limit);
    ArcResult r = ar_okay();
    if (n < 0)
        r = backend_error(reader->backend, ARC_INVALID_ARCHIVE);
    else if (UINT64_MAX - reader->bytes_read < (uint64_t)n)
        r = ar_result(ARC_OVERFLOW, "Read entry size overflow.");
    else {
        reader->bytes_read += (uint64_t)n;
        if (!n) {
            if (reader->size_set && (reader->declared_size < 0 ||
                                     reader->bytes_read != (uint64_t)reader->declared_size))
                r = ar_result(ARC_INVALID_ARCHIVE, "Read entry size mismatch.");
            else
                reader->body_done = 1;
        }
    }
    if (ar_failed(r)) {
        r = ar_result(r.code, "Entry '%.65s': %.160s", reader->name, r.message);
        return sequential_reader_failure(reader, r);
    }
    *received = (size_t)n;
    return ar_okay();
}

ArcResult arc_reader_close_impl(ArcReader **owned) {
    if (!*owned)
        return ar_okay();
    ArcReader *reader = *owned;
    *owned = NULL;
    ArcResult cleanup = reader_release(reader->backend, ar_okay());
    ArcResult r = ar_failed(reader->failure) ? reader->failure : cleanup;
    clear_reader_entry(reader);
    free(reader);
    return r;
}

ArcResult arc_list_impl(const char *path, const ArcReadOptions *options, ArcVisitor visitor,
                        void *context) {
    if (!visitor)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing archive visitor.");
    ArcReader *reader = NULL;
    ArcResult r = arc_reader_open_impl(path, options, &reader);
    if (ar_failed(r))
        return r;
    while (!ar_failed(r)) {
        ArcEntry entry;
        int end;
        r = arc_reader_next_impl(reader, &entry, &end);
        if (ar_failed(r) || end)
            break;
        r = visitor(&entry, context);
    }
    ArcResult cleanup = arc_reader_close_impl(&reader);
    /* Visitor results take precedence over cleanup and are returned unchanged. */
    return ar_failed(r) ? r : cleanup;
}

ArcResult arc_get_size_impl(const char *path, uint64_t *out) {
    if (!path || !out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing size argument.");
    ArcFsInfo i = {0};
    ArcResult r = ar_from_fs(arc_fs_info(path, &i));
    if (ar_failed(r))
        return r;
    if (!i.exists || i.directory || i.symbolic)
        return ar_result(ARC_IO_ERROR, "Archive path is not an ordinary file: '%s'.", path);
    *out = i.size;
    return ar_okay();
}

static ArcResult accumulate(const ArcEntry *e, void *context) {
    ArcInfo *i = context;
    uint64_t *counter = NULL;
    switch (e->type) {
    case ARC_ENTRY_FILE:
        if (!e->size_known)
            return ar_result(ARC_UNSUPPORTED, "Unknown file size '%s'.", e->path);
        if (UINT64_MAX - i->content_bytes < e->size)
            return ar_result(ARC_OVERFLOW, "Content size overflow.");
        i->content_bytes += e->size;
        counter = &i->file_count;
        break;
    case ARC_ENTRY_DIRECTORY:
        counter = &i->directory_count;
        break;
    case ARC_ENTRY_SYMBOLIC_LINK:
        counter = &i->symbolic_link_count;
        break;
    case ARC_ENTRY_HARD_LINK:
        counter = &i->hard_link_count;
        break;
    default:
        return ar_result(ARC_UNSUPPORTED, "Unsupported entry type '%s'.", e->path);
    }
    if (*counter == UINT64_MAX)
        return ar_result(ARC_OVERFLOW, "Entry count overflow.");
    ++*counter;
    return ar_okay();
}

ArcResult arc_inspect_impl(const char *path, const ArcReadOptions *options, ArcInfo *out) {
    if (!out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing statistics output.");
    ArcInfo info = {0};
    ArcResult r = arc_get_size_impl(path, &info.archive_bytes);
    if (!ar_failed(r))
        r = arc_list_impl(path, options, accumulate, &info);
    if (!ar_failed(r))
        *out = info;
    return r;
}

ArcResult arc_verify_impl(const char *path, const ArcReadOptions *options) {
    struct archive *a = NULL;
    ArcResult r = reader_open(path, options ? options->password : NULL, &a);
    if (ar_failed(r))
        return r;
    while (!ar_failed(r)) {
        struct archive_entry *e = NULL;
        int end;
        r = read_header(a, &e, &end);
        if (ar_failed(r) || end)
            break;
        r = ar_okay();
        uint64_t total = 0;
        unsigned char buffer[65536];
        while (!ar_failed(r)) {
            la_ssize_t n = archive_read_data(a, buffer, sizeof buffer);
            if (n < 0) {
                r = backend_error(a, ARC_INVALID_ARCHIVE);
                break;
            }
            if (!n)
                break;
            if (UINT64_MAX - total < (uint64_t)n) {
                r = ar_result(ARC_OVERFLOW, "Verified entry size overflow.");
                break;
            }
            total += (uint64_t)n;
        }
        if (!ar_failed(r) && entry_type(e) == ARC_ENTRY_FILE && archive_entry_size_is_set(e) &&
            (archive_entry_size(e) < 0 || total != (uint64_t)archive_entry_size(e)))
            r = ar_result(ARC_INVALID_ARCHIVE, "Verified entry size mismatch.");
        if (ar_failed(r)) {
            const char *name = archive_entry_pathname_utf8(e);
            r = ar_result(r.code, "Entry '%.65s': %.160s", name ? name : "<unnamed>", r.message);
        }
    }
    return reader_release(a, r);
}
