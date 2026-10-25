#ifndef ARC_NATIVE_H
#define ARC_NATIVE_H

#include <archive.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif

/* Allocation failures are fatal, never normal operation results. */
static void *arc_allocate(size_t size)
{
    void *result = calloc(1, size);
    if (!result) abort();
    return result;
}

struct arc_native_error {
    const char *stage;
    const char *system_domain;
    int native_status;
    int system_code;
    char *message;
};

static int arc_error(struct arc_native_error *error, const char *stage,
                     int status, const char *domain, int code, const char *message)
{
    const char *text = message ? message : "No native diagnostic";
    char *copy;

    if ((!strcmp(domain, "errno") && code == ENOMEM)
#ifdef _WIN32
        || (!strcmp(domain, "win32") &&
            (code == ERROR_NOT_ENOUGH_MEMORY || code == ERROR_OUTOFMEMORY))
#endif
    ) abort();

    copy = (char *)arc_allocate(strlen(text) + 1);
    strcpy(copy, text);
    free(error->message);
    error->message = copy;
    error->stage = stage;
    error->native_status = status;
    error->system_domain = domain;
    error->system_code = code;
    return -1;
}

static int arc_native_result(struct arc_native_error *error, const char *stage,
                             struct archive *handle, int status)
{
    int code;
    if (status == ARCHIVE_OK) return 0;
    code = archive_errno(handle);
    return arc_error(error, stage, status, "errno", code, archive_error_string(handle));
}

/* Callers capture errno before any diagnostic or cleanup call can replace it. */
static int arc_errno_error(struct arc_native_error *error, const char *stage, int code)
{
    return arc_error(error, stage, 0, "errno", code, strerror(code));
}

#ifndef _WIN32
/* Libarchive converts entry names using LC_CTYPE; each session owns this locale. */
static locale_t arc_new_locale(struct arc_native_error *error)
{
    int code;
    locale_t locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
    if (!locale) {
        code = errno;
        if (code == ENOMEM) abort();
        locale = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
        if (!locale) {
            code = errno;
            arc_errno_error(error, "create UTF-8 locale", code);
        }
    }
    return locale;
}

static void arc_free_locale(locale_t locale)
{
    if (locale) freelocale(locale);
}

/* Locale is active only inside native primitives, never in client Eiffel code. */
static locale_t arc_enter_locale(locale_t locale)
{
    locale_t previous = uselocale(locale);
    if (!previous) abort(); /* Cannot maintain thread state. */
    return previous;
}

static void arc_leave_locale(locale_t previous)
{
    if (!uselocale(previous)) abort();
}
#define ARC_ENTER(ctx) locale_t previous = arc_enter_locale((ctx)->locale)
#define ARC_LEAVE() arc_leave_locale(previous)
#else
#define ARC_ENTER(ctx) ((void)(ctx))
#define ARC_LEAVE() ((void)0)
#endif

#endif
