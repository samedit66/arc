#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "support.h"
#include "arc_internal.h"
#include "platform_text.h"
#include <stdlib.h>
#include <string.h>

static int separator(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static const char *basename_of(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (separator(*p))
            name = p + 1;
    return name;
}

static ArcResult source_name(const char *source, int directory, char **out) {
    const char *name = basename_of(source);
    char *absolute = NULL;
    ArcResult r = ar_okay();
    if (directory && (!strcmp(name, ".") || !strcmp(name, ".."))) {
#ifdef _WIN32
        r = ar_from_fs(arc_fs_absolute_path(source, &absolute));
#else
        absolute = realpath(source, NULL);
        if (!absolute)
            r = ar_system_error("Cannot resolve source", source);
#endif
        if (!ar_failed(r))
            name = basename_of(absolute);
    }
    if (!ar_failed(r) && !*name)
        r = ar_result(ARC_INVALID_PATH, "Source root has no archive basename.");
    if (!ar_failed(r))
        r = ar_copy_string(name, out);
    free(absolute);
    return r;
}

ArcResult arc_pack(const char *path, const char *source) {
    return arc_pack_ex(path, &source, 1, NULL);
}

/* Pack otherwise enters locale scopes through the public writer operations. */
static ArcResult pack_phase(ProgressState *progress, ArcProgressPhase phase) {
    if (!progress->observer.callback)
        return ar_okay();
    LocaleScope scope;
    ArcResult r = locale_begin(&scope);
    if (ar_failed(r))
        return r;
    ar_progress_phase(progress, phase);
    return locale_end(&scope, ar_okay());
}

ArcResult arc_pack_ex(const char *path, const char *const *sources, size_t count,
                      const ArcPackOptions *options) {
    if (!path || !*path || !sources || !count)
        return ar_result(ARC_INVALID_ARGUMENT, "Missing archive or sources.");
    int keep_root = options && options->keep_root;
    if (keep_root && count != 1)
        return ar_result(ARC_INVALID_ARGUMENT, "Keeping the root requires one directory.");
    const ArchiveSuffix *suffix = ar_suffix_of(path, 1);
    const char *format = options ? options->format : NULL;
    const char *compression = options ? options->compression : NULL;
    if ((format && !*format) || (compression && !*compression))
        return ar_result(ARC_INVALID_ARGUMENT, "Empty archive encoding identifier.");
    if (!format && !suffix)
        return ar_result(ARC_INVALID_ARGUMENT, "Cannot infer output format; specify format.");
    ArcOptions encoding = {format ? format : suffix->format,
                           compression ? compression
                           : format    ? "none"
                                       : suffix->compression,
                           options ? options->password : NULL};
    ProgressState progress = {.observer = options ? options->observer : (ArcObserver){0}};
    ArcWriter *writer = NULL;
    ArcResult r = pack_phase(&progress, ARC_PHASE_PREPARING);
    if (!ar_failed(r))
        r = pack_phase(&progress, ARC_PHASE_PROCESSING);
    for (size_t i = 0; i < count && !ar_failed(r); ++i) {
        if (!sources[i] || !*sources[i]) {
            r = ar_result(ARC_INVALID_ARGUMENT, "Missing source path.");
            break;
        }
        char *source = NULL, *name = NULL;
        r = ar_copy_string(sources[i], &source);
        if (ar_failed(r))
            break;
        size_t n = strlen(source);
        while (n > 1 && separator(source[n - 1])) {
#ifdef _WIN32
            if (n == 3 && source[1] == ':')
                break;
#endif
            source[--n] = 0;
        }
        ArcFsInfo info = {0};
        r = ar_from_fs(arc_fs_info(source, &info));
        if (!ar_failed(r) && !info.exists)
            r = ar_result(ARC_NOT_FOUND, "Source not found: '%s'.", source);
        int directory = info.directory && !info.symbolic;
        if (!ar_failed(r) && keep_root && !directory)
            r = ar_result(ARC_INVALID_ARGUMENT, "Keeping the root requires one directory.");
        if (!ar_failed(r)) {
            if (count == 1 && directory && !keep_root)
                r = ar_copy_string("", &name);
            else
                r = source_name(source, directory, &name);
        }
        if (!ar_failed(r) && !writer) {
            r = arc_create_ex(path, &encoding, &writer);
            if (!ar_failed(r))
                writer_observe(writer, &progress);
        }
        if (!ar_failed(r))
            r = arc_add_path(writer, source, name);
        free(name);
        free(source);
    }
    if (!ar_failed(r))
        r = pack_phase(&progress, ARC_PHASE_FINALIZING);
    if (!ar_failed(r))
        return arc_finish_pack(&writer);
    if (writer)
        r = ar_cleanup(r, arc_discard(&writer));
    return r;
}
