#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cli/progress.h"
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static uint64_t test_milliseconds = 1000;

/* Exercise the real renderer with controlled time, regardless of runner load. */
#ifdef _WIN32
static ULONGLONG test_tick_count(void) {
    return test_milliseconds;
}

#define GetTickCount64 test_tick_count
#else
static int test_clock_gettime(clockid_t clock, struct timespec *now) {
    (void)clock;
    now->tv_sec = (time_t)(test_milliseconds / 1000);
    now->tv_nsec = (long)(test_milliseconds % 1000) * 1000000;
    return 0;
}

#define clock_gettime test_clock_gettime
#endif
#include "cli/progress.c"
#ifdef _WIN32
#undef GetTickCount64
#else
#undef clock_gettime
#endif

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    Progress *p = progress_start("Extracting", argc > 1 ? argv[1] : "sample.tar");
    unsigned milliseconds = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 550;
    ArcProgress snapshot = {.phase = ARC_PHASE_PROCESSING,
                            .path = argc > 1 ? argv[1] : "sample.tar"};
    if (argc > 3 && !strcmp(argv[3], "boundaries")) {
        const unsigned times[] = {0, 199, 200, 200, 299, 300, 399, 400};
        for (unsigned i = 0; i < sizeof times / sizeof times[0]; ++i) {
            test_milliseconds = 1000 + times[i];
            snapshot.entries_seen = i + 1;
            progress_update(&snapshot, p);
        }
    } else {
        for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 25) {
            test_milliseconds = 1000 + elapsed;
            if (argc > 3) {
                ArcProgressPhase phase = elapsed < 250   ? ARC_PHASE_PREPARING
                                         : elapsed < 400 ? ARC_PHASE_PROCESSING
                                                         : ARC_PHASE_FINALIZING;
                if (phase != snapshot.phase)
                    snapshot.entries_seen = 0;
                snapshot.phase = phase;
            }
            snapshot.entries_seen++;
            if (snapshot.phase == ARC_PHASE_PROCESSING)
                snapshot.content_bytes += 1048576;
            progress_update(&snapshot, p);
        }
    }
    progress_stop(p);
    return argc > 4 ? 1 : 0;
}
