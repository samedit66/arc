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
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec delay = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000};
    nanosleep(&delay, NULL);
#endif
    progress_stop(p);
    return 0;
}
