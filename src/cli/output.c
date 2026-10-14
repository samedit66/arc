#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "output.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <langinfo.h>
#include <unistd.h>
#endif

void display_text(FILE *out, const char *text, int escape_backslash) {
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < 32 || *p == 127)
            fprintf(out, "\\x%02x", *p);
        else if (*p == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f)
            fprintf(out, "\\u00%02x", *++p);
        else if (escape_backslash && *p == '\\')
            fputs("\\\\", out);
        else
            fputc(*p, out);
    }
}

int output_terminal(FILE *out, int *unicode) {
    const char *term = getenv("TERM");
    *unicode = 0;
    if (term && !strcmp(term, "dumb"))
        return 0;
#ifdef _WIN32
    DWORD mode;
    if (!GetConsoleMode(GetStdHandle(out == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE), &mode))
        return 0;
    *unicode = GetConsoleOutputCP() == CP_UTF8;
    return 1;
#else
    const char *codeset = nl_langinfo(CODESET);
    *unicode = !strcmp(codeset, "UTF-8") || !strcmp(codeset, "UTF8");
    return isatty(fileno(out));
#endif
}

void message(MessageKind kind, const char *label, const char *format, ...) {
    FILE *out = kind == MESSAGE_SUCCESS ? stdout : stderr;
    const char *no_color = getenv("NO_COLOR");
    int unicode = 0;
    int terminal = output_terminal(out, &unicode);
#ifdef _WIN32
    HANDLE handle = GetStdHandle(out == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD original_mode = 0;
    int changed_mode = 0;
    if (terminal)
        GetConsoleMode(handle, &original_mode);
#endif
    int color = terminal && !(no_color && *no_color);
#ifdef _WIN32
    if (color && !(original_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        changed_mode = SetConsoleMode(handle, original_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        color = changed_mode;
    }
#endif
    if (terminal && unicode) {
        const char *emoji[] = {"✅ ", "❌ ", "⚠️ "};
        fputs(emoji[kind], out);
    }
    if (color) {
        const char *colors[] = {"\033[32m", "\033[31m", "\033[33m"};
        fputs(colors[kind], out);
    }
    fputs(label, out);
    if (color)
        fputs("\033[0m", out);
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    char *text = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (text) {
        vsnprintf(text, (size_t)length + 1, format, arguments);
        display_text(out, text, 0);
        free(text);
    } else
        fputs(" [message unavailable]", out);
    va_end(arguments);
    fputc('\n', out);
#ifdef _WIN32
    if (changed_mode) {
        fflush(out);
        SetConsoleMode(handle, original_mode);
    }
#endif
}
