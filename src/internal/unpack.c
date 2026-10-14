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
#include "libarchive_adapter.h"
#include "platform_text.h"

typedef struct {
    char *name, *target, *resolved;
    ArchiveMetadata metadata;
    int metadata_set;
    ArcEntryType type;
    int created, resolving, resolved_kind, link_kind;
#ifdef _WIN32
    int resolved_opaque;
#endif
} ExtractItem;

typedef struct {
    ExtractItem *items;
    size_t count, capacity;
    ArchivePathIndex paths;
} ExtractIndex;

typedef struct {
    char *path;
    int matched;
} Selector;

typedef struct {
    Selector *items;
    size_t count;
} Selection;

static void free_selection(Selection *selection) {
    for (size_t i = 0; i < selection->count; ++i)
        free(selection->items[i].path);
    free(selection->items);
}

static ArcResult prepare_selection(const ArcExtractOptions *options, Selection *selection) {
    if (!options->path_count)
        return ar_okay();
    if (!options->paths)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing selected paths.");
    if (options->path_count > SIZE_MAX / sizeof(Selector))
        return ar_result(ARC_OVERFLOW, "Too many selected paths.");
    selection->items = calloc(options->path_count, sizeof *selection->items);
    if (!selection->items)
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate selection.");
    for (size_t i = 0; i < options->path_count; ++i) {
        char *name = NULL;
        ArcResult r = entry_name(options->paths[i], &name);
        if (ar_failed(r))
            return r;
        int duplicate = 0;
        for (size_t j = 0; j < selection->count; ++j)
            if (!strcmp(name, selection->items[j].path)) {
                duplicate = 1;
                break;
            }
        if (duplicate)
            free(name);
        else
            selection->items[selection->count++].path = name;
    }
    return ar_okay();
}

static int select_entry(Selection *selection, const char *name) {
    int selected = !selection->count;
    for (size_t i = 0; i < selection->count; ++i) {
        if (entry_path_selected(selection->items[i].path, name)) {
            selection->items[i].matched = 1;
            selected = 1;
        }
    }
    return selected;
}

static ArcResult find_item(ExtractIndex *index, const char *name, ExtractItem **out) {
    size_t record;
    ArcResult r = archive_paths_find(&index->paths, name, &record);
    if (!ar_failed(r))
        *out = record == SIZE_MAX ? NULL : &index->items[record];
    return r;
}

static void free_index(ExtractIndex *index) {
    for (size_t i = 0; i < index->count; ++i) {
        ExtractItem *p = &index->items[i];
        free(p->name);
        free(p->target);
        free(p->resolved);
    }
    free(index->items);
    archive_paths_free(&index->paths);
    memset(index, 0, sizeof *index);
}

static ArcResult append_item(ExtractIndex *index, const char *name, ArcEntryType type,
                             ExtractItem **out) {
    if (index->count == index->capacity) {
        size_t n = index->capacity ? index->capacity * 2 : 16;
        if (n < index->capacity || n > SIZE_MAX / sizeof(ExtractItem))
            return ar_result(ARC_OVERFLOW, "Too many archive entries.");
        ExtractItem *p = realloc(index->items, n * sizeof *p);
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
    ExtractItem *p = &index->items[index->count++];
    memset(p, 0, sizeof *p);
    p->name = copy;
    p->type = type;
    *out = p;
    return ar_okay();
}

static ArcResult apply_entry_metadata(const char *path, ArchiveMetadata metadata, int directory) {
#ifndef _WIN32
    if (directory) {
        ArcResult r = ar_from_fs(arc_fs_restrict_permissions(path, metadata.mode));
        if (ar_failed(r))
            return r;
    }
#else
    (void)directory;
#endif
    if (metadata.mtime_set)
        return ar_from_fs(arc_fs_set_mtime(path, metadata.mtime, metadata.mtime_nsec));
    return ar_okay();
}

static ArcResult actual_directory(const char *path) {
    ArcFsInfo i = {0};
    ArcResult r = ar_from_fs(arc_fs_info(path, &i));
    if (ar_failed(r))
        return r;
    if (!i.exists || !i.directory || i.symbolic)
        return ar_result(ARC_INVALID_PATH, "Not an actual directory: '%s'.", path);
    return ar_okay();
}

static ArcResult safe_root(const char *path) {
    char *copy = NULL;
    ArcResult r = ar_copy_string(path, &copy);
    if (ar_failed(r))
        return r;
    platform_normalize_separators(copy);
    for (char *p = copy; *p; ++p) {
        if (*p != '/' || p == copy)
            continue;
        if (platform_root_separator(copy, p))
            continue;
        char saved = *p;
        *p = 0;
        r = actual_directory(copy);
        *p = saved;
        if (ar_failed(r))
            break;
    }
    free(copy);
    return r;
}

static ArcResult directory(ExtractIndex *index, const char *root, const char *name) {
    if (!*name)
        return actual_directory(root);
    char *prefix = NULL;
    ArcResult r = ar_copy_string(name, &prefix);
    if (ar_failed(r))
        return r;
    for (char *end = prefix; !ar_failed(r); ++end) {
        if (*end && *end != '/')
            continue;
        char saved = *end;
        *end = 0;
        ExtractItem *p = NULL;
        r = find_item(index, prefix, &p);
        if (!ar_failed(r) && p && p->type != ARC_ENTRY_DIRECTORY)
            r = ar_diagnostic(
                ar_result(ARC_ALREADY_EXISTS, "An archive entry is used as a directory."),
                ARC_REASON_ARCHIVE_CONFLICT, prefix);
        if (!ar_failed(r) && !p) {
            char *full = NULL;
            ArcFsInfo before = {0};
            r = ar_join(root, prefix, &full);
            if (!ar_failed(r))
                r = ar_from_fs(arc_fs_info(full, &before));
            int created = 0;
            if (!ar_failed(r) && !before.exists) {
                r = ar_from_fs(arc_fs_create_directory_exclusive(full, 0777));
                created = !ar_failed(r);
            }
            if (!ar_failed(r))
                r = actual_directory(full);
            if (!ar_failed(r))
                r = append_item(index, prefix, ARC_ENTRY_DIRECTORY, &p);
            if (!ar_failed(r))
                p->created = created;
            else
                r.partial_output |= created;
            free(full);
        }
        *end = saved;
        if (!saved)
            break;
    }
    free(prefix);
    return r;
}

static ArcResult parents(ExtractIndex *index, const char *root, const char *name) {
    char *parent = NULL;
    ArcResult r = ar_copy_string(name, &parent);
    if (ar_failed(r))
        return r;
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = 0;
        r = directory(index, root, parent);
    }
    free(parent);
    return r;
}

static int metadata_equal(ArchiveMetadata a, ArchiveMetadata b) {
    return a.mode == b.mode && a.mtime_set == b.mtime_set &&
           (!a.mtime_set || (a.mtime == b.mtime && a.mtime_nsec == b.mtime_nsec));
}

static ArcResult hard_links(ExtractIndex *index, const char *root, int selective) {
    size_t *stack = NULL;
    for (size_t i = 0; i < index->count; ++i)
        if (index->items[i].type == ARC_ENTRY_HARD_LINK) {
            if (index->count > SIZE_MAX / sizeof *stack)
                return ar_result(ARC_OVERFLOW, "Too many hard links.");
            stack = malloc(index->count * sizeof *stack);
            if (!stack)
                return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate hard-link stack.");
            break;
        }
    ArcResult r = ar_okay();
    for (size_t i = 0; i < index->count && !ar_failed(r); ++i) {
        ExtractItem *p = &index->items[i];
        if (p->type != ARC_ENTRY_HARD_LINK || p->created)
            continue;
        size_t depth = 0;
        while (p->type == ARC_ENTRY_HARD_LINK && !p->created && !ar_failed(r)) {
            if (p->resolving) {
                r = ar_result(ARC_INVALID_ARCHIVE, "Cyclic hard link '%s'.", p->name);
                break;
            }
            p->resolving = 1;
            stack[depth++] = (size_t)(p - index->items);
            ExtractItem *target = NULL;
            r = find_item(index, p->target, &target);
            if (!ar_failed(r) && !target)
                r = ar_result(selective ? ARC_SELECTION_ERROR : ARC_INVALID_ARCHIVE,
                              "Missing or unselected ordinary hard-link target '%s'.", p->target);
            if (!ar_failed(r) && target->type != ARC_ENTRY_FILE &&
                target->type != ARC_ENTRY_HARD_LINK)
                r = ar_result(ARC_INVALID_ARCHIVE,
                              "Hard-link target is not an ordinary file: '%s'.", p->target);
            if (!ar_failed(r))
                p = target;
        }
        while (depth) {
            p = &index->items[stack[--depth]];
            if (!ar_failed(r)) {
                char *full = NULL, *source = NULL;
                ExtractItem *target = NULL;
                r = find_item(index, p->target, &target);
                if (!ar_failed(r) && !metadata_equal(p->metadata, target->metadata))
                    r = ar_result(ARC_INVALID_ARCHIVE, "Conflicting hard-link metadata '%s'.",
                                  p->name);
                if (!ar_failed(r))
                    r = ar_join(root, p->name, &full);
                if (!ar_failed(r))
                    r = ar_join(root, p->target, &source);
                if (!ar_failed(r))
                    r = ar_from_fs(arc_fs_create_hard_link(source, full));
                if (r.code == ARC_ALREADY_EXISTS)
                    r = ar_diagnostic(r, ARC_REASON_ENTRY_EXISTS, full);
                if (!ar_failed(r))
                    p->created = 1;
                free(full);
                free(source);
            }
            p->resolving = 0;
        }
    }
    free(stack);
    return r;
}

#ifdef _WIN32
static void pop_component(char *path) {
    char *s = strrchr(path, '/');
    if (s)
        *s = 0;
    else
        *path = 0;
}

typedef struct {
    size_t record, waiting;
    char *current, *text, *cursor;
    int kind, dangling, opaque;
} SymbolicFrame;

static ArcResult push_symbolic(ExtractIndex *index, size_t record, SymbolicFrame **frames,
                               size_t *depth, size_t *capacity) {
    ExtractItem *p = &index->items[record];
    if (p->resolving)
        return ar_result(ARC_UNSUPPORTED, "Unknown Windows link target type '%s'.", p->name);
    if (!p->target || !*p->target || !ar_valid_utf8(p->target) || p->target[0] == '/')
        return ar_result(ARC_UNSUPPORTED, "Unknown Windows link target type '%s'.", p->name);
    if (!platform_link_target_compatible(p->target))
        return ar_result(ARC_INVALID_PATH, "Incompatible Windows link target '%s'.", p->target);
    if (*depth == *capacity) {
        size_t n = *capacity ? *capacity * 2 : 16;
        if (n < *capacity || n > SIZE_MAX / sizeof **frames)
            return ar_result(ARC_OVERFLOW, "Too many symbolic links.");
        SymbolicFrame *grown = realloc(*frames, n * sizeof *grown);
        if (!grown)
            return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate symbolic-link stack.");
        *frames = grown;
        *capacity = n;
    }
    SymbolicFrame f = {.record = record, .waiting = SIZE_MAX, .kind = 2};
    ArcResult r = ar_copy_string(p->name, &f.current);
    if (!ar_failed(r))
        r = ar_copy_string(p->target, &f.text);
    if (ar_failed(r)) {
        free(f.current);
        free(f.text);
        return r;
    }
    pop_component(f.current);
    f.cursor = f.text;
    (*frames)[(*depth)++] = f;
    p->resolving = 1;
    return ar_okay();
}

static ArcResult symbolic_step(SymbolicFrame *f, char *candidate, int kind) {
    if (kind == 1 && f->cursor) {
        free(candidate);
        return ar_result(ARC_UNSUPPORTED, "Unknown Windows link target type.");
    }
    free(f->current);
    f->current = candidate;
    f->kind = kind;
    if (!kind)
        f->dangling = 1;
    return ar_okay();
}

static ArcResult resolve_symbolic(ExtractIndex *index, ExtractItem *p) {
    if (p->resolved)
        return ar_okay();
    SymbolicFrame *frames = NULL;
    size_t depth = 0, capacity = 0;
    ArcResult r = push_symbolic(index, (size_t)(p - index->items), &frames, &depth, &capacity);
    while (depth && !ar_failed(r)) {
        SymbolicFrame *f = &frames[depth - 1];
        if (f->waiting != SIZE_MAX) {
            ExtractItem *child = &index->items[f->waiting];
            if (child->resolved_opaque && f->cursor) {
                r = ar_result(ARC_UNSUPPORTED, "Unknown Windows link target type '%s'.", p->name);
                break;
            }
            f->opaque = child->resolved_opaque;
            char *candidate = NULL;
            r = ar_copy_string(child->resolved, &candidate);
            if (!ar_failed(r))
                r = symbolic_step(f, candidate, child->resolved_kind);
            f->waiting = SIZE_MAX;
            continue;
        }
        if (!f->cursor) {
            ExtractItem *item = &index->items[f->record];
            item->resolved = f->current;
            item->resolved_kind = f->dangling ? 0 : f->kind;
            item->resolved_opaque = f->opaque;
            item->resolving = 0;
            free(f->text);
            --depth;
            continue;
        }
        char *segment = f->cursor;
        char *tail = strchr(segment, '/');
        if (tail)
            *tail++ = 0;
        f->cursor = tail;
        if (!*segment || !strcmp(segment, "."))
            continue;
        if (!strcmp(segment, "..")) {
            if (!*f->current) {
                r = ar_result(ARC_UNSUPPORTED, "Unknown Windows link target type '%s'.", p->name);
                break;
            }
            pop_component(f->current);
            f->kind = 2;
            continue;
        }
        char *candidate = NULL, *checked = NULL;
        r = ar_join(f->current, segment, &candidate);
        if (!ar_failed(r))
            r = entry_name(candidate, &checked);
        free(checked);
        ExtractItem *child = NULL;
        if (!ar_failed(r))
            r = find_item(index, candidate, &child);
        if (ar_failed(r)) {
            free(candidate);
            break;
        }
        if (child && child->type == ARC_ENTRY_SYMBOLIC_LINK) {
            if (!f->cursor && (child->link_kind == AE_SYMLINK_TYPE_FILE ||
                               child->link_kind == AE_SYMLINK_TYPE_DIRECTORY)) {
                /* Metadata supplies a kind, not a resolved path for later traversal. */
                f->opaque = 1;
                r = symbolic_step(f, candidate,
                                  child->link_kind == AE_SYMLINK_TYPE_DIRECTORY
                                      ? ARC_FS_LINK_DIRECTORY
                                      : ARC_FS_LINK_FILE);
                continue;
            }
            free(candidate);
            f->waiting = (size_t)(child - index->items);
            if (!child->resolved)
                r = push_symbolic(index, f->waiting, &frames, &depth, &capacity);
            continue; /* push may realloc frames; reacquire the frame next iteration. */
        }
        /* Infer the native link kind only from selected archive records. */
        int kind = !*candidate || (child && child->type == ARC_ENTRY_DIRECTORY)
                       ? ARC_FS_LINK_DIRECTORY
                   : child && (child->type == ARC_ENTRY_FILE || child->type == ARC_ENTRY_HARD_LINK)
                       ? ARC_FS_LINK_FILE
                       : ARC_FS_LINK_UNKNOWN;
        r = symbolic_step(f, candidate, kind);
    }
    while (depth) {
        SymbolicFrame *f = &frames[--depth];
        index->items[f->record].resolving = 0;
        free(f->current);
        free(f->text);
    }
    free(frames);
    return r;
}

#endif

static ArcResult extract_entry(ExtractIndex *index, const char *root, struct archive *a,
                               struct archive_entry *e) {
    ArcEntryType type = entry_type(e);
    if (type == ARC_ENTRY_OTHER)
        return ar_result(ARC_UNSUPPORTED, "Special archive entries are unsupported.");
    char *raw = NULL, *name = NULL, *full = NULL;
    ArcResult r = entry_text(e, 0, &raw);
    /* A directory record for the omitted archive root has no output object. */
    if (!ar_failed(r) && type == ARC_ENTRY_DIRECTORY && (!strcmp(raw, ".") || !strcmp(raw, "./"))) {
        free(raw);
        return ar_okay();
    }
    if (!ar_failed(r))
        r = entry_name(raw, &name);
    if (!ar_failed(r))
        r = parents(index, root, name);
    if (!ar_failed(r))
        r = ar_join(root, name, &full);
    ExtractItem *p = NULL;
    if (!ar_failed(r) && type == ARC_ENTRY_DIRECTORY) {
        r = directory(index, root, name);
        if (!ar_failed(r))
            r = find_item(index, name, &p);
        if (!ar_failed(r) && p->metadata_set &&
            !metadata_equal(p->metadata, backend_file_metadata(e)))
            r = ar_result(ARC_INVALID_ARCHIVE, "Conflicting directory metadata '%s'.", name);
    } else if (!ar_failed(r)) {
        ArcFsInfo before = {0};
        /* Classify conflicts within the archive before inspecting disk objects. */
        r = append_item(index, name, type, &p);
        if (!ar_failed(r))
            r = ar_from_fs(arc_fs_info(full, &before));
        if (!ar_failed(r) && before.exists)
            r = ar_diagnostic(
                ar_result(ARC_ALREADY_EXISTS, "An entry already exists at the destination."),
                ARC_REASON_ENTRY_EXISTS, full);
    }
    if (!ar_failed(r) && !p->metadata_set) {
        p->metadata = backend_file_metadata(e);
        p->metadata_set = 1;
    }
    if (!ar_failed(r) && (type == ARC_ENTRY_SYMBOLIC_LINK || type == ARC_ENTRY_HARD_LINK)) {
        char *target = NULL;
        r = entry_text(e, type == ARC_ENTRY_SYMBOLIC_LINK ? 1 : 2, &target);
        if (!ar_failed(r) && type == ARC_ENTRY_HARD_LINK) {
            r = entry_name(target, &p->target);
            free(target);
        } else if (!ar_failed(r)) {
            p->target = target;
            p->link_kind = archive_entry_symlink_type(e);
        }
    }
    if (!ar_failed(r) && type == ARC_ENTRY_FILE) {
        int fd = -1;
        r = ar_from_fs(
            arc_fs_create_file_exclusive(full, (unsigned)archive_entry_perm(e) & 0777, &fd));
        if (r.code == ARC_ALREADY_EXISTS)
            r = ar_diagnostic(r, ARC_REASON_ENTRY_EXISTS, full);
        if (!ar_failed(r)) {
            p->created = 1;
            unsigned char buffer[65536];
            la_ssize_t n = 0;
            uint64_t total = 0;
            while ((n = archive_read_data(a, buffer, sizeof buffer)) > 0) {
                if (UINT64_MAX - total < (uint64_t)n) {
                    r = ar_result(ARC_OVERFLOW, "Extracted file size overflow.");
                    break;
                }
                total += (uint64_t)n;
                r = ar_from_fs(arc_fs_write_all(fd, buffer, (size_t)n));
                if (ar_failed(r))
                    break;
            }
            if (!ar_failed(r) && n < 0)
                r = backend_error(a, ARC_INVALID_ARCHIVE);
            if (!ar_failed(r) && archive_entry_size_is_set(e) &&
                (archive_entry_size(e) < 0 || total != (uint64_t)archive_entry_size(e)))
                r = ar_result(ARC_INVALID_ARCHIVE, "Extracted size mismatch '%s'.", name);
            r = ar_cleanup(r, ar_from_fs(arc_fs_close(fd, full)));
            if (!ar_failed(r))
                r = apply_entry_metadata(full, backend_file_metadata(e), 0);
        }
    }
    free(raw);
    free(name);
    free(full);
    return r;
}

/* Preflight all names, not only selected entries: stripping has one stable root. */
static ArcResult strip_root_name(const char *path, const char *password, char **out) {
    struct archive *a = NULL;
    ArcResult r = reader_open(path, password, &a);
    char *common = NULL;
    while (!ar_failed(r)) {
        struct archive_entry *e = NULL;
        int end;
        r = read_header(a, &e, &end);
        if (ar_failed(r) || end)
            break;
        char *raw = NULL, *name = NULL;
        r = entry_text(e, 0, &raw);
        int omitted = !ar_failed(r) && entry_type(e) == ARC_ENTRY_DIRECTORY &&
                      (!strcmp(raw, ".") || !strcmp(raw, "./"));
        if (!ar_failed(r) && !omitted)
            r = entry_name(raw, &name);
        if (!ar_failed(r) && name) {
            char *slash = strchr(name, '/');
            if (!slash && entry_type(e) != ARC_ENTRY_DIRECTORY)
                r = ar_diagnostic(
                    ar_result(ARC_INVALID_ARCHIVE, "Root-level entries cannot be stripped."),
                    ARC_REASON_STRIP_ROOT, NULL);
            if (slash)
                *slash = 0;
            if (!ar_failed(r) && common && strcmp(common, name))
                r = ar_diagnostic(
                    ar_result(ARC_INVALID_ARCHIVE, "Archive has more than one root directory."),
                    ARC_REASON_STRIP_ROOT, NULL);
            if (!ar_failed(r) && !common)
                r = ar_copy_string(name, &common);
        }
        free(raw);
        free(name);
        if (!ar_failed(r) && archive_read_data_skip(a) != ARCHIVE_OK)
            r = backend_error(a, ARC_INVALID_ARCHIVE);
    }
    r = reader_release(a, r);
    if (!ar_failed(r) && !common)
        r = ar_diagnostic(ar_result(ARC_INVALID_ARCHIVE, "Archive has no root directory to strip."),
                          ARC_REASON_STRIP_ROOT, NULL);
    if (ar_failed(r)) {
        free(common);
    } else
        *out = common;
    return r;
}

static ArcResult strip_entry(struct archive_entry *e, const char *common, int *omit) {
    char *raw = NULL, *name = NULL;
    ArcResult r = entry_text(e, 0, &raw);
    *omit = !ar_failed(r) && entry_type(e) == ARC_ENTRY_DIRECTORY &&
            (!strcmp(raw, ".") || !strcmp(raw, "./"));
    if (!ar_failed(r) && !*omit)
        r = entry_name(raw, &name);
    size_t n = strlen(common);
    if (!ar_failed(r) && name) {
        if (!strcmp(name, common) && entry_type(e) == ARC_ENTRY_DIRECTORY)
            *omit = 1;
        else if (strncmp(name, common, n) || name[n] != '/')
            r = ar_result(ARC_INVALID_ARCHIVE, "Archive root changed after preflight.");
        else
            archive_entry_set_pathname_utf8(e, name + n + 1);
    }
    free(raw);
    free(name);
    if (!ar_failed(r) && !*omit && entry_type(e) == ARC_ENTRY_HARD_LINK) {
        raw = name = NULL;
        r = entry_text(e, 2, &raw);
        if (!ar_failed(r))
            r = entry_name(raw, &name);
        if (!ar_failed(r)) {
            if (strncmp(name, common, n) || name[n] != '/')
                r = ar_result(ARC_INVALID_PATH, "Hard link leaves the stripped root.");
            else
                archive_entry_set_hardlink_utf8(e, name + n + 1);
        }
        free(raw);
        free(name);
    }
    return r;
}

ArcResult arc_unpack_impl(const char *path, const char *root, const ArcExtractOptions *options) {
    if (!root || !*root || !options ||
        (options->destination_mode != ARC_DESTINATION_NEW &&
         options->destination_mode != ARC_DESTINATION_MERGE))
        return ar_result(ARC_INVALID_ARGUMENT, "Missing or invalid extraction options.");
    Selection selection = {0};
    ArcResult r = prepare_selection(options, &selection);
    char *common = NULL;
    if (!ar_failed(r) && options->strip_root)
        r = strip_root_name(path, options->password, &common);
    struct archive *a = NULL;
    if (!ar_failed(r))
        r = reader_open(path, options->password, &a);
    if (ar_failed(r)) {
        free_selection(&selection);
        free(common);
        return r;
    }
    int root_created = 0;
    r = safe_root(root);
    if (!ar_failed(r)) {
        ArcFsInfo info = {0};
        r = ar_from_fs(arc_fs_info(root, &info));
        if (!ar_failed(r) && (options->destination_mode == ARC_DESTINATION_NEW || !info.exists)) {
            r = ar_from_fs(arc_fs_create_directory_exclusive(root, 0777));
            root_created = !ar_failed(r);
        }
        if (r.code == ARC_ALREADY_EXISTS)
            r = ar_diagnostic(r, ARC_REASON_DESTINATION_EXISTS, root);
    }
    if (!ar_failed(r))
        r = actual_directory(root);
    ExtractIndex index = {0};
    while (!ar_failed(r)) {
        struct archive_entry *e;
        int end;
        r = read_header(a, &e, &end);
        if (ar_failed(r) || end)
            break;
        int selected = 1;
        if (selection.count) {
            char *name = NULL;
            r = entry_text(e, 0, &name);
            if (!ar_failed(r))
                selected = select_entry(&selection, name);
            free(name);
        }
        if (!ar_failed(r) && selected)
            r = normalized_entry(a, e);
        int omit = 0;
        if (!ar_failed(r) && selected && common)
            r = strip_entry(e, common, &omit);
        if (!ar_failed(r) && selected && !omit)
            r = extract_entry(&index, root, a, e);
        if (!ar_failed(r) && archive_read_data_skip(a) != ARCHIVE_OK)
            r = backend_error(a, ARC_INVALID_ARCHIVE);
    }
    r = reader_release(a, r);
    for (size_t i = 0; i < selection.count && !ar_failed(r); ++i)
        if (!selection.items[i].matched)
            r = ar_result(ARC_NOT_FOUND, "Selected path not found: '%s'.", selection.items[i].path);
    if (!ar_failed(r))
        r = hard_links(&index, root, selection.count != 0);
    for (size_t i = 0; i < index.count && !ar_failed(r); ++i) {
        ExtractItem *p = &index.items[i];
        if (p->type != ARC_ENTRY_SYMBOLIC_LINK)
            continue;
        if (!p->target || !*p->target || !ar_valid_utf8(p->target))
            r = ar_result(ARC_INVALID_PATH, "Missing or invalid symbolic-link target.");
        else if (!platform_link_target_compatible(p->target))
            r = ar_result(ARC_INVALID_PATH, "Incompatible Windows link target '%s'.", p->target);
#ifdef _WIN32
        else if (p->link_kind != AE_SYMLINK_TYPE_DIRECTORY && p->link_kind != AE_SYMLINK_TYPE_FILE)
            r = resolve_symbolic(&index, p);
#endif
    }
    for (size_t i = 0; i < index.count && !ar_failed(r); ++i) {
        ExtractItem *p = &index.items[i];
        if (p->type != ARC_ENTRY_SYMBOLIC_LINK)
            continue;
        char *full = NULL;
        r = ar_join(root, p->name, &full);
        if (!ar_failed(r))
            r = ar_from_fs(arc_fs_create_symbolic_link(
                full, p->target,
                p->link_kind == AE_SYMLINK_TYPE_DIRECTORY ? ARC_FS_LINK_DIRECTORY
                : p->link_kind == AE_SYMLINK_TYPE_FILE    ? ARC_FS_LINK_FILE
                                                          : (ArcFsLinkKind)p->resolved_kind));
        if (r.code == ARC_ALREADY_EXISTS)
            r = ar_diagnostic(r, ARC_REASON_ENTRY_EXISTS, full);
        if (!ar_failed(r))
            p->created = 1;
        free(full);
    }
    /* Children were indexed after parents; finalize new directories in reverse. */
    for (size_t i = index.count; i && !ar_failed(r); --i) {
        ExtractItem *p = &index.items[i - 1];
        if (p->type != ARC_ENTRY_DIRECTORY || !p->created || !p->metadata_set)
            continue;
        char *full = NULL;
        r = ar_join(root, p->name, &full);
        if (!ar_failed(r))
            r = apply_entry_metadata(full, p->metadata, 1);
        free(full);
    }
    if (ar_failed(r)) {
        r.partial_output |= root_created;
        for (size_t i = 0; i < index.count; ++i)
            r.partial_output |= index.items[i].created;
    }
    free_index(&index);
    free_selection(&selection);
    free(common);
    return r;
}
