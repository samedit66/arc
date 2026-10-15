#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "progress.h"
#include "output.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#endif

struct Progress {
    const char *action, *path;
    uint64_t started, updated;
    int shown;
#ifdef _WIN32
    HANDLE console;
    DWORD original_mode;
#endif
};

static uint64_t milliseconds(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

static int terminal_width(Progress *p) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(p->console, &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
#else
    (void)p;
    struct winsize size;
    if (!ioctl(STDERR_FILENO, TIOCGWINSZ, &size) && size.ws_col)
        return size.ws_col;
#endif
    return 80;
}

static void render(Progress *p, const ArcProgress *snapshot) {
    int remaining = terminal_width(p) - 1;
    fputs("\r\033[2K", stderr);
    if (remaining < 8)
        return;
    const char *action = snapshot->phase == ARC_PHASE_PREPARING    ? "Preparing"
                         : snapshot->phase == ARC_PHASE_FINALIZING ? "Finalizing"
                                                                   : p->action;
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    unsigned unit = 0;
    double bytes = (double)snapshot->content_bytes;
    while (bytes >= 1024 && unit < 6) {
        bytes /= 1024;
        ++unit;
    }
    char status[160];
    snprintf(status, sizeof status, "%s: %" PRIu64 " entries | %.1f %s | ", action,
             snapshot->entries_seen, bytes, units[unit]);
    const char *parts[] = {status, "", snapshot->path ? snapshot->path : p->path};
    for (unsigned part = 0; part < 3; ++part) {
        const unsigned char *s = (const unsigned char *)parts[part];
        while (*s && remaining > 0) {
            unsigned length = *s < 0x80 ? 1 : *s < 0xe0 ? 2 : *s < 0xf0 ? 3 : 4;
            if (*s == 0xc2 && s[1] >= 0x80 && s[1] <= 0x9f) {
                if (remaining < 6)
                    break;
                fprintf(stderr, "\\u00%02x", s[1]);
                s += 2;
                remaining -= 6;
            } else if (*s < 32 || *s == 127) {
                if (remaining < 4)
                    break;
                fprintf(stderr, "\\x%02x", *s++);
                remaining -= 4;
            } else {
                if ((int)length > remaining)
                    break;
                unsigned n = 0;
                while (n < length && s[n])
                    ++n;
                if (n != length)
                    break;
                fwrite(s, 1, length, stderr);
                s += length;
                remaining -= (int)length;
            }
        }
        if (*s)
            break;
    }
    fflush(stderr);
}

Progress *progress_start(const char *action, const char *path) {
    int unicode;
    if (!output_terminal(stderr, &unicode))
        return NULL;
    Progress *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->action = action;
    p->path = path;
    p->started = milliseconds();
#ifdef _WIN32
    p->console = GetStdHandle(STD_ERROR_HANDLE);
    if (!GetConsoleMode(p->console, &p->original_mode) ||
        !SetConsoleMode(p->console, p->original_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        free(p);
        return NULL;
    }
#endif
    return p;
}

void progress_update(const ArcProgress *snapshot, void *context) {
    Progress *p = context;
    if (!p || ferror(stderr))
        return;
    uint64_t now = milliseconds();
    if (now - p->started < 200 || (p->shown && now - p->updated < 100))
        return;
    render(p, snapshot);
    p->shown = 1;
    p->updated = now;
}

ArcObserver progress_observer(Progress *progress) {
    return (ArcObserver){progress ? progress_update : NULL, progress};
}

void progress_stop(Progress *p) {
    if (!p)
        return;
    if (p->shown) {
        fputs("\r\033[2K", stderr);
        fflush(stderr);
    }
#ifdef _WIN32
    SetConsoleMode(p->console, p->original_mode);
#endif
    free(p);
}
