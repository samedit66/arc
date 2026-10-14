#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif
#include "progress.h"
#include "output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <langinfo.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#endif

struct Progress {
    const char *action;
    const char *path;
    int unicode;
#ifdef _WIN32
    HANDLE thread, stop;
    HANDLE console;
    DWORD original_mode;
#else
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int stopped;
#endif
};

/* Timed waiting is interruptible: finishing never waits for the next frame. */
static int wait_frame(Progress *p, unsigned milliseconds) {
#ifdef _WIN32
    return WaitForSingleObject(p->stop, milliseconds) != WAIT_TIMEOUT;
#else
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += (long)milliseconds * 1000000;
    deadline.tv_sec += deadline.tv_nsec / 1000000000;
    deadline.tv_nsec %= 1000000000;
    pthread_mutex_lock(&p->mutex);
    int result = 0;
    while (!p->stopped && !result)
        result = pthread_cond_timedwait(&p->condition, &p->mutex, &deadline);
    int stopped = p->stopped || (result && result != ETIMEDOUT);
    pthread_mutex_unlock(&p->mutex);
    return stopped;
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

static void render(Progress *p, unsigned frame) {
    int width = terminal_width(p);
    fputs("\r\033[2K", stderr);
    /* Reserve the final column to avoid terminal autowrap. Non-ASCII text is
       conservatively budgeted by UTF-8 bytes, never split inside a sequence. */
    int remaining = width - 1;
    if (remaining < 8)
        return;
    unsigned position = frame % 22;
    if (position > 11)
        position = 22 - position;
    if (remaining >= 20) {
        fputc('[', stderr);
        for (unsigned i = 0; i < 14; ++i)
            fputs(i >= position && i < position + 3 ? (p->unicode ? "⣿" : "#") : " ", stderr);
        fputs("] ", stderr);
        remaining -= 17;
    }
    const char *parts[] = {p->action, ": ", p->path};
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

#ifdef _WIN32
static DWORD WINAPI animate(LPVOID context) {
#else
static void *animate(void *context) {
#endif
    Progress *p = context;
    if (!wait_frame(p, 200)) {
        unsigned frame = 0;
        do {
            render(p, frame++);
        } while (!ferror(stderr) && !wait_frame(p, 100));
        fputs("\r\033[2K", stderr);
        fflush(stderr);
    }
    return 0;
}

Progress *progress_start(const char *action, const char *path) {
    int unicode = 0;
    if (!output_terminal(stderr, &unicode))
        return NULL;
    Progress *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->unicode = unicode;
    p->action = action;
    p->path = path;
#ifdef _WIN32
    p->console = GetStdHandle(STD_ERROR_HANDLE);
    if (!GetConsoleMode(p->console, &p->original_mode)) {
        free(p);
        return NULL;
    }
    if (!SetConsoleMode(p->console, p->original_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        free(p);
        return NULL;
    }
    p->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (p->stop)
        p->thread = CreateThread(NULL, 0, animate, p, 0, NULL);
    if (!p->thread) {
        if (p->stop)
            CloseHandle(p->stop);
        SetConsoleMode(p->console, p->original_mode);
        free(p);
        return NULL;
    }
#else
    if (pthread_mutex_init(&p->mutex, NULL)) {
        free(p);
        return NULL;
    }
    if (pthread_cond_init(&p->condition, NULL)) {
        pthread_mutex_destroy(&p->mutex);
        free(p);
        return NULL;
    }
    if (pthread_create(&p->thread, NULL, animate, p)) {
        pthread_cond_destroy(&p->condition);
        pthread_mutex_destroy(&p->mutex);
        free(p);
        return NULL;
    }
#endif
    return p;
}

void progress_stop(Progress *p) {
    if (!p)
        return;
#ifdef _WIN32
    SetEvent(p->stop);
    WaitForSingleObject(p->thread, INFINITE);
    CloseHandle(p->thread);
    CloseHandle(p->stop);
    SetConsoleMode(p->console, p->original_mode);
#else
    pthread_mutex_lock(&p->mutex);
    p->stopped = 1;
    pthread_cond_signal(&p->condition);
    pthread_mutex_unlock(&p->mutex);
    pthread_join(p->thread, NULL);
    pthread_cond_destroy(&p->condition);
    pthread_mutex_destroy(&p->mutex);
#endif
    free(p);
}
