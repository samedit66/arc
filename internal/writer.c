#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "support.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "arc_internal.h"
#include "archive_paths.h"
#include "../filekit/filekit.h"
#include "libarchive_adapter.h"
#include "platform_text.h"
#include <time.h>

typedef struct {
    char *name;
    FilekitInfo identity;
    ArcEntryType type;
    size_t identity_left, identity_right;
    int identity_height;
} WriterItem;

typedef struct {
    WriterItem *items;
    size_t count, capacity;
    ArchivePathIndex paths;
    size_t identity_root; /* Record numbers plus one; zero is absent. */
} WriterIndex;

struct Arc {
    struct archive *backend;
    char *path;
    int fd, entry_open;
    uint64_t remaining;
    unsigned capabilities;
    FilekitInfo output;
    WriterIndex index;
    ArcResult failure;
};

static void free_index(WriterIndex *index) {
    for (size_t i = 0; i < index->count; ++i) {
        WriterItem *p = &index->items[i];
        free(p->name);
    }
    free(index->items);
    archive_paths_free(&index->paths);
    memset(index, 0, sizeof *index);
}

static ArcResult append_item(WriterIndex *index, const char *name, ArcEntryType type,
                             WriterItem **out) {
    if (index->count == index->capacity) {
        size_t n = index->capacity ? index->capacity * 2 : 16;
        if (n < index->capacity || n > SIZE_MAX / sizeof(WriterItem))
            return ar_result(ARC_OVERFLOW, "Too many archive entries.");
        WriterItem *p = realloc(index->items, n * sizeof *p);
        if (!p)
            return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate entry index.");
        index->items = p;
        index->capacity = n;
    }
    char *copy = NULL;
    ArcResult r = ar_copy_string(name, &copy);
    if (ar_failed(r))
        return r;
    r = archive_paths_insert(&index->paths, name, type, index->count);
    if (ar_failed(r)) {
        free(copy);
        return r;
    }
    WriterItem *p = &index->items[index->count++];
    memset(p, 0, sizeof *p);
    p->name = copy;
    p->type = type;
    *out = p;
    return ar_okay();
}

/* The identity tree uses stable record numbers even when items reallocates. */
static int identity_height(WriterIndex *index, size_t record) {
    return record ? index->items[record - 1].identity_height : 0;
}

static void identity_update(WriterIndex *index, size_t record) {
    WriterItem *p = &index->items[record - 1];
    int left = identity_height(index, p->identity_left),
        right = identity_height(index, p->identity_right);
    p->identity_height = 1 + (left > right ? left : right);
}

static size_t identity_rotate(WriterIndex *index, size_t record, int left) {
    WriterItem *p = &index->items[record - 1];
    size_t top = left ? p->identity_right : p->identity_left;
    WriterItem *q = &index->items[top - 1];
    if (left) {
        p->identity_right = q->identity_left;
        q->identity_left = record;
    } else {
        p->identity_left = q->identity_right;
        q->identity_right = record;
    }
    identity_update(index, record);
    identity_update(index, top);
    return top;
}

static size_t identity_insert(WriterIndex *index, size_t root, size_t record) {
    if (!root) {
        index->items[record - 1].identity_height = 1;
        return record;
    }
    WriterItem *p = &index->items[root - 1];
    int order =
        filekit_compare_identity(index->items[record - 1].identity.identity, p->identity.identity);
    if (order < 0)
        p->identity_left = identity_insert(index, p->identity_left, record);
    else
        p->identity_right = identity_insert(index, p->identity_right, record);
    identity_update(index, root);
    int balance =
        identity_height(index, p->identity_left) - identity_height(index, p->identity_right);
    if (balance > 1) {
        WriterItem *q = &index->items[p->identity_left - 1];
        if (identity_height(index, q->identity_left) < identity_height(index, q->identity_right))
            p->identity_left = identity_rotate(index, p->identity_left, 1);
        return identity_rotate(index, root, 0);
    }
    if (balance < -1) {
        WriterItem *q = &index->items[p->identity_right - 1];
        if (identity_height(index, q->identity_right) < identity_height(index, q->identity_left))
            p->identity_right = identity_rotate(index, p->identity_right, 0);
        return identity_rotate(index, root, 1);
    }
    return root;
}

static WriterItem *identity_find(WriterIndex *index, FilekitIdentity identity) {
    size_t record = index->identity_root;
    while (record) {
        WriterItem *p = &index->items[record - 1];
        int order = filekit_compare_identity(identity, p->identity.identity);
        if (!order)
            return p;
        record = order < 0 ? p->identity_left : p->identity_right;
    }
    return NULL;
}

ArcResult writer_previous_failure(const Arc *writer) {
    return writer->failure;
}

ArcResult writer_failure(Arc *w, ArcResult r) {
    if (w && ar_failed(r) && !ar_failed(w->failure))
        w->failure = r;
    return r;
}

static ArcResult writer_ready(Arc *w, int entry) {
    if (!w)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing writer.");
    if (ar_failed(w->failure))
        return ar_result(ARC_INVALID_STATE, "Writer has failed: %s", w->failure.message);
    if (w->entry_open != entry)
        return writer_failure(w, ar_result(ARC_INVALID_STATE, entry ? "No open file entry."
                                                                    : "An entry is unfinished."));
    return ar_okay();
}

ArcResult arc_create_impl(const char *path, const ArcOptions *options, Arc **out) {
    if (!out)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing writer output.");
    *out = NULL;
    if (!path || !*path)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing archive path.");
    unsigned caps = 0;
    ArcResult r = arc_get_capabilities_impl(options, &caps);
    if (ar_failed(r))
        return r;
    if (!(caps & ARC_CAN_CREATE))
        return ar_result(ARC_UNSUPPORTED, "Archive creation is unavailable.");
    Arc *w = calloc(1, sizeof *w);
    if (!w)
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate writer.");
    w->fd = -1;
    w->capabilities = caps;
    w->backend = archive_write_new();
    if (!w->backend) {
        free(w);
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate backend.");
    }
    r = ar_from_filekit(filekit_absolute_path(path, &w->path));
    if (!ar_failed(r))
        r = backend_configure_writer(w->backend, options);
    if (!ar_failed(r))
        r = ar_from_filekit(filekit_create_file_exclusive(w->path, 0666, &w->fd));
    if (!ar_failed(r))
        r = ar_from_filekit(filekit_info(w->path, &w->output));
    if (!ar_failed(r) && archive_write_open_fd(w->backend, w->fd) != ARCHIVE_OK)
        r = backend_error(w->backend, ARC_IO_ERROR);
    if (ar_failed(r)) {
        if (archive_write_free(w->backend) != ARCHIVE_OK)
            r = ar_cleanup(r, ar_result(ARC_IO_ERROR, "Backend cleanup failed."));
        if (w->fd >= 0) {
            r = ar_cleanup(r, ar_from_filekit(filekit_close(w->fd, w->path)));
            ArcResult cleanup = ar_from_filekit(filekit_remove_file(w->path));
            if (ar_failed(cleanup))
                cleanup =
                    ar_result(cleanup.code, "Incomplete output remains: %.200s", cleanup.message);
            r = ar_cleanup(r, cleanup);
        }
        free(w->path);
        free(w);
        return r;
    }
    *out = w;
    return ar_okay();
}

static ArcResult start_entry(Arc *w, struct archive_entry *entry, ArcEntryType type,
                             FilekitInfo identity) {
    const char *name = archive_entry_pathname_utf8(entry);
    if (!name)
        return writer_failure(w,
                              ar_result(ARC_INVALID_PATH, "Backend cannot represent entry name."));
    WriterItem *p = NULL;
    ArcResult r = append_item(&w->index, name, type, &p);
    if (ar_failed(r))
        return writer_failure(w, r);
    p->identity = identity;
    if (type == ARC_ENTRY_FILE && identity.exists)
        w->index.identity_root = identity_insert(&w->index, w->index.identity_root, w->index.count);
    if (archive_write_header(w->backend, entry) != ARCHIVE_OK)
        return writer_failure(w, backend_error(w->backend, ARC_IO_ERROR));
    w->entry_open = 1;
    w->remaining = type == ARC_ENTRY_FILE ? (uint64_t)archive_entry_size(entry) : 0;
    return ar_okay();
}

ArcResult arc_begin_file_impl(Arc *w, const char *path, uint64_t size) {
    ArcResult r = writer_ready(w, 0);
    if (ar_failed(r))
        return r;
    if (size > INT64_MAX)
        return writer_failure(w, ar_result(ARC_OVERFLOW, "Entry size exceeds backend range."));
    char *name = NULL;
    r = entry_name(path, &name);
    if (ar_failed(r))
        return writer_failure(w, r);
    struct archive_entry *e = archive_entry_new();
    if (!e) {
        free(name);
        return writer_failure(w, ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate entry."));
    }
    archive_entry_set_pathname_utf8(e, name);
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_mtime(e, time(NULL), 0);
    archive_entry_set_size(e, (la_int64_t)size);
    r = start_entry(w, e, ARC_ENTRY_FILE, (FilekitInfo){0});
    archive_entry_free(e);
    free(name);
    return r;
}

ArcResult arc_write_chunk_impl(Arc *w, const void *data, size_t n) {
    ArcResult r = writer_ready(w, 1);
    if (ar_failed(r))
        return r;
    if (!data && n)
        return writer_failure(w, ar_result(ARC_INVALID_ARGUMENT, "Missing chunk data."));
    if ((uint64_t)n > w->remaining)
        return writer_failure(w,
                              ar_result(ARC_SIZE_MISMATCH, "Chunk exceeds declared entry size."));
    const unsigned char *p = data;
    size_t left = n;
    while (left) {
        la_ssize_t done = archive_write_data(w->backend, p, left);
        if (done <= 0)
            return writer_failure(w, backend_error(w->backend, ARC_IO_ERROR));
        p += done;
        left -= (size_t)done;
    }
    w->remaining -= n;
    return ar_okay();
}

ArcResult arc_end_file_impl(Arc *w) {
    ArcResult r = writer_ready(w, 1);
    if (ar_failed(r))
        return r;
    if (w->remaining)
        return writer_failure(w, ar_result(ARC_SIZE_MISMATCH, "Entry is shorter than declared."));
    if (archive_write_finish_entry(w->backend) != ARCHIVE_OK)
        return writer_failure(w, backend_error(w->backend, ARC_IO_ERROR));
    w->entry_open = 0;
    return ar_okay();
}

ArcResult arc_add_text_impl(Arc *w, const char *path, const char *text) {
    if (!text)
        return writer_failure(w, ar_result(ARC_INVALID_ARGUMENT, "Missing text."));
    ArcResult r = arc_begin_file_impl(w, path, strlen(text));
    if (!ar_failed(r))
        r = arc_write_chunk_impl(w, text, strlen(text));
    if (!ar_failed(r))
        r = arc_end_file_impl(w);
    return r;
}

static ArcResult pack_entry(Arc *w, struct archive *disk, struct archive_entry *e,
                            const char *source, const char *stored) {
    FilekitInfo identity;
    ArcResult r = ar_from_filekit(filekit_info(source, &identity));
    if (ar_failed(r))
        return writer_failure(w, r);
    if (!identity.exists)
        return writer_failure(w, ar_result(ARC_IO_ERROR, "Source disappeared: '%s'.", source));
    char *name = NULL, *alias = NULL;
    r = entry_name(stored, &name);
    if (ar_failed(r))
        return writer_failure(w, r);
    ArcEntryType type = identity.symbolic    ? ARC_ENTRY_SYMBOLIC_LINK
                        : identity.directory ? ARC_ENTRY_DIRECTORY
                                             : ARC_ENTRY_FILE;
    if (type == ARC_ENTRY_DIRECTORY && !(w->capabilities & ARC_CAN_WRITE_DIRECTORIES))
        r = ar_result(ARC_UNSUPPORTED, "Format cannot store directory '%s'.", name);
    if (type == ARC_ENTRY_SYMBOLIC_LINK && !(w->capabilities & ARC_CAN_WRITE_SYMBOLIC_LINKS))
        r = ar_result(ARC_UNSUPPORTED, "Format cannot store symbolic link '%s'.", name);
    if (!ar_failed(r) && type == ARC_ENTRY_FILE) {
        WriterItem *previous = identity_find(&w->index, identity.identity);
        if (previous) {
            if (!(w->capabilities & ARC_CAN_WRITE_HARD_LINKS))
                r = ar_result(ARC_UNSUPPORTED, "Format cannot preserve hard link '%s'.", name);
            else
                r = ar_copy_string(previous->name, &alias);
        }
        if (alias)
            type = ARC_ENTRY_HARD_LINK;
    }
    if (ar_failed(r)) {
        free(name);
        free(alias);
        return writer_failure(w, r);
    }
    if (type == ARC_ENTRY_SYMBOLIC_LINK) {
        char *target = NULL;
        r = entry_text(e, 1, &target);
        free(target);
        if (ar_failed(r)) {
            free(name);
            free(alias);
            return writer_failure(w, r);
        }
    }
    clean_metadata(e);
    archive_entry_set_pathname_utf8(e, name);
    archive_entry_set_hardlink(e, NULL);
    if (alias) {
        archive_entry_set_hardlink_utf8(e, alias);
        archive_entry_set_size(e, 0);
    } else if (type == ARC_ENTRY_FILE) {
        if (identity.size > INT64_MAX)
            r = ar_result(ARC_OVERFLOW, "Source file is too large.");
        else
            archive_entry_set_size(e, (la_int64_t)identity.size);
    } else
        archive_entry_set_size(e, 0);
    if (!ar_failed(r))
        r = start_entry(w, e, type, identity);
    free(name);
    free(alias);
    if (!ar_failed(r) && type == ARC_ENTRY_FILE) {
        unsigned char buffer[65536];
        la_ssize_t n;
        while ((n = archive_read_data(disk, buffer, sizeof buffer)) > 0) {
            r = arc_write_chunk_impl(w, buffer, (size_t)n);
            if (ar_failed(r))
                break;
        }
        if (n < 0 && !ar_failed(r))
            r = writer_failure(w, backend_error(disk, ARC_IO_ERROR));
    }
    if (!ar_failed(r))
        r = arc_end_file_impl(w);
    return writer_failure(w, r);
}

static ArcResult add_source(Arc *w, const char *source, const char *stored, int file_only) {
    ArcResult r = writer_ready(w, 0);
    if (ar_failed(r))
        return r;
    if (!source || !*source || !stored)
        return writer_failure(w, ar_result(ARC_INVALID_ARGUMENT, "Missing source or entry path."));
    char *prefix = NULL;
    r = ar_copy_string(source, &prefix);
    if (ar_failed(r))
        return writer_failure(w, r);
    platform_normalize_separators(prefix);
    size_t plen = strlen(prefix);
    while (plen > 1 && prefix[plen - 1] == '/') {
        if (platform_drive_separator(prefix, prefix + plen - 1))
            break;
        prefix[--plen] = 0;
    }
    FilekitInfo root = {0};
    r = ar_from_filekit(filekit_info(prefix, &root));
    if (!ar_failed(r) && !root.exists)
        r = ar_result(ARC_IO_ERROR, "Source does not exist: '%s'.", prefix);
    if (!ar_failed(r) && filekit_same_identity(root, w->output))
        r = ar_result(ARC_INVALID_ARGUMENT, "Source is the output archive.");
    if (!ar_failed(r) && file_only && (root.directory || root.symbolic))
        r = ar_result(ARC_UNSUPPORTED, "Source is not an ordinary file.");
    if (!ar_failed(r) && !*stored && (!root.directory || root.symbolic))
        r = ar_result(ARC_INVALID_ARGUMENT, "Empty entry mapping requires an actual directory.");
    if (ar_failed(r)) {
        free(prefix);
        return writer_failure(w, r);
    }
    struct archive *disk = archive_read_disk_new();
    if (!disk) {
        free(prefix);
        return writer_failure(w,
                              ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate filesystem reader."));
    }
    archive_read_disk_set_symlink_physical(disk);
    archive_read_disk_set_behavior(disk, ARCHIVE_READDISK_NO_ACL | ARCHIVE_READDISK_NO_XATTR |
                                             ARCHIVE_READDISK_NO_FFLAGS);
    r = disk_open(disk, prefix);
    int first = 1;
    while (!ar_failed(r)) {
        struct archive_entry *e;
        int status = archive_read_next_header(disk, &e);
        if (status == ARCHIVE_EOF)
            break;
        if (status != ARCHIVE_OK) {
            r = backend_error(disk, ARC_IO_ERROR);
            break;
        }
        char *path = NULL, *name = NULL;
        r = entry_text(e, 0, &path);
        if (ar_failed(r))
            break;
        platform_normalize_separators(path);
        const char *suffix = path;
        if (first)
            suffix = "";
        else if (!strncmp(path, prefix, plen) && (prefix[plen - 1] == '/' || path[plen] == '/'))
            suffix = path + plen + (prefix[plen - 1] != '/');
        else
            r = ar_result(ARC_INVALID_PATH, "Filesystem traversal escaped source '%s'.", prefix);
        FilekitInfo current = {0};
        if (!ar_failed(r))
            r = ar_from_filekit(filekit_info(path, &current));
        int excluded = !ar_failed(r) && filekit_same_identity(current, w->output);
        if (!ar_failed(r) && !excluded && !(first && !*stored)) {
            r = ar_join(stored, suffix, &name);
            if (!ar_failed(r))
                r = pack_entry(w, disk, e, path, name);
        }
        if (!ar_failed(r) && !file_only && !excluded && current.directory && !current.symbolic &&
            archive_read_disk_can_descend(disk) && archive_read_disk_descend(disk) != ARCHIVE_OK)
            r = backend_error(disk, ARC_IO_ERROR);
        free(path);
        free(name);
        first = 0;
        if (file_only)
            break;
    }
    int status = archive_read_free(disk);
    if (status != ARCHIVE_OK)
        r = ar_cleanup(r, ar_result(ARC_IO_ERROR, "Filesystem reader cleanup failed."));
    free(prefix);
    return writer_failure(w, r);
}

ArcResult arc_add_file_impl(Arc *w, const char *source, const char *name) {
    return add_source(w, source, name, 1);
}

ArcResult arc_add_path_impl(Arc *w, const char *source, const char *name) {
    return add_source(w, source, name, 0);
}

ArcResult release_writer(Arc **owned, int discard) {
    if (!owned)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing writer pointer.");
    Arc *w = *owned;
    if (!w)
        return ar_okay();
    *owned = NULL;
    ArcResult r = discard ? ar_okay() : w->failure;
    if (!discard && !ar_failed(r) && w->entry_open)
        r = ar_result(ARC_INVALID_STATE, "Cannot close an unfinished entry.");
    int status = archive_write_close(w->backend);
    if (status != ARCHIVE_OK)
        r = ar_cleanup(r, backend_error(w->backend, ARC_IO_ERROR));
    status = archive_write_free(w->backend);
    if (status != ARCHIVE_OK)
        r = ar_cleanup(r, ar_result(ARC_IO_ERROR, "Backend cleanup failed."));
    r = ar_cleanup(r, ar_from_filekit(filekit_close(w->fd, w->path)));
    if (discard) {
        ArcResult cleanup = ar_from_filekit(filekit_remove_file(w->path));
        if (ar_failed(cleanup))
            cleanup = ar_result(cleanup.code, "Incomplete output remains: %.200s", cleanup.message);
        r = ar_cleanup(r, cleanup);
    }
    free_index(&w->index);
    free(w->path);
    free(w);
    return r;
}
