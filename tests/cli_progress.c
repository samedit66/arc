#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cli/progress.h"
#include <locale.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    Progress *p = progress_start("Extracting", argc > 1 ? argv[1] : "sample.tar");
    unsigned milliseconds = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 550;
    ArcProgress snapshot = {.phase = ARC_PHASE_PROCESSING,
                            .path = argc > 1 ? argv[1] : "sample.tar"};
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 25) {
#ifdef _WIN32
        Sleep(25);
#else
        struct timespec delay = {0, 25000000};
        nanosleep(&delay, NULL);
#endif
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
    progress_stop(p);
    return argc > 4 ? 1 : 0;
}
