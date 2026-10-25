#ifndef ARC_PROBE_LOCALE_H
#define ARC_PROBE_LOCALE_H

#include <stdlib.h>
#ifndef _WIN32
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif

/* Select a distinct caller locale so a leaked extraction locale is observable. */
static void *arc_probe_begin_locale(void)
{
#ifndef _WIN32
    locale_t caller = newlocale(LC_CTYPE_MASK, "C", (locale_t)0);
    locale_t previous;
    if (!caller) abort();
    previous = uselocale(caller);
    if (!previous) abort();
    return (void *)previous;
#else
    return NULL;
#endif
}

static void *arc_probe_current_locale(void)
{
#ifndef _WIN32
    locale_t current = uselocale((locale_t)0);
    if (!current) abort();
    return (void *)current;
#else
    return NULL;
#endif
}

static void arc_probe_end_locale(void *previous)
{
#ifndef _WIN32
    locale_t caller = uselocale((locale_t)previous);
    if (!caller) abort();
    freelocale(caller);
#else
    (void)previous;
#endif
}

#endif
