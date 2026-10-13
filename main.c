#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "arc.h"
#include "internal/support.h"
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef ARC_VERSION
#define ARC_VERSION "dev"
#endif
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <wchar.h>
#else
#include <langinfo.h>
#include <unistd.h>
#endif

/* Presentation belongs to the CLI; NO_COLOR disables ANSI color, not emoji. */
typedef enum { MESSAGE_SUCCESS, MESSAGE_ERROR, MESSAGE_WARNING } MessageKind;

static void display_text(FILE *out, const char *text, int escape_backslash) {
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

static void message(MessageKind kind, const char *label, const char *format, ...) {
    FILE *out = kind == MESSAGE_SUCCESS ? stdout : stderr;
    const char *term = getenv("TERM");
    const char *no_color = getenv("NO_COLOR");
    int terminal = 0, unicode = 0;
#ifdef _WIN32
    HANDLE handle = GetStdHandle(out == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD original_mode = 0;
    int changed_mode = 0;
    if (GetConsoleMode(handle, &original_mode)) {
        terminal = 1;
        unicode = GetConsoleOutputCP() == CP_UTF8;
    }
#else
    terminal = isatty(fileno(out));
    const char *codeset = nl_langinfo(CODESET);
    unicode = !strcmp(codeset, "UTF-8") || !strcmp(codeset, "UTF8");
#endif
    if (term && !strcmp(term, "dumb"))
        terminal = 0;
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

static int report(ArcResult r) {
    if (r.code == ARC_OK)
        return 0;
    message(MESSAGE_ERROR, "Error:", " %s", r.message);
    return 1;
}

static int usage(const char *command, int error) {
    FILE *out = error ? stderr : stdout;
    if (error)
        message(MESSAGE_ERROR, "Error:", " invalid arguments%s%s.", command ? " for " : "",
                command ? command : "");
    if (!command || !strcmp(command, "pack"))
        fprintf(out, "Usage: arc pack [--format FORMAT] [--keep-root] [--password VALUE] ARCHIVE "
                     "[SOURCE...]\n");
    if (!command || !strcmp(command, "unpack"))
        fprintf(out,
                "Usage: arc unpack [--entry PATH]... [--password VALUE] ARCHIVE [DESTINATION]\n");
    if (!command || !strcmp(command, "list"))
        fprintf(out, "Usage: arc list [--password VALUE] ARCHIVE\n");
    if (!command || !strcmp(command, "info"))
        fprintf(out, "Usage: arc info [--password VALUE] ARCHIVE\n");
    if (!command || !strcmp(command, "verify"))
        fprintf(out, "Usage: arc verify [--password VALUE] ARCHIVE\n");
    if (!command)
        fprintf(out, "Usage: arc --help\nUsage: arc --version\n");
    fprintf(out,
            "\nPack infers the format from the output suffix. A single directory\n"
            "contributes its contents; --keep-root retains the directory itself.\n"
            "Without sources, pack uses the directory named after the archive in the current\n"
            "directory. Unpack defaults to a new directory named after the archive.\n"
            "An explicit destination may merge directories. Files are never overwritten.\n"
            "--entry selects an exact archive path and its subtree; paths are preserved.\n"
            "List prints entries; info prints sizes and counts.\n"
            "Verify reads all content and checks integrity where the format permits.\n"
            "Verification does not establish safe extraction or detect every TAR data change.\n"
            "--password supplies a password; pack writes ZIP AES-256 (names stay visible).\n"
            "Use -- before paths beginning with '-'. Options precede operands.\n");
    return error ? 2 : 0;
}

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

static char *duplicate(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p)
        memcpy(p, s, n + 1);
    return p;
}
#ifdef _WIN32
static wchar_t *to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n)
        return NULL;
    wchar_t *w = malloc((size_t)n * sizeof *w);
    if (w)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    return w;
}

static wchar_t *to_wide_path(const char *s) {
    wchar_t *w = to_wide(s);
    if (!w)
        return NULL;
    for (wchar_t *p = w; *p; ++p)
        if (*p == L'/')
            *p = L'\\';
    DWORD n = GetFullPathNameW(w, 0, NULL, NULL);
    wchar_t *absolute = n ? malloc((size_t)n * sizeof *absolute) : NULL;
    wchar_t *full = n ? malloc(((size_t)n + 8) * sizeof *full) : NULL;
    if (!absolute || !full) {
        free(w);
        free(absolute);
        free(full);
        return NULL;
    }
    DWORD written = GetFullPathNameW(w, n, absolute, NULL);
    free(w);
    if (!written || written >= n) {
        free(absolute);
        free(full);
        return NULL;
    }
    if (!wcsncmp(absolute, L"\\\\?\\", 4))
        wcscpy(full, absolute);
    else if (!wcsncmp(absolute, L"\\\\", 2)) {
        wcscpy(full, L"\\\\?\\UNC\\");
        wcscat(full, absolute + 2);
    } else {
        wcscpy(full, L"\\\\?\\");
        wcscat(full, absolute);
    }
    free(absolute);
    return full;
}

static char *from_wide(const wchar_t *s) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL, 0, NULL, NULL);
    if (!n)
        return NULL;
    char *p = malloc((size_t)n);
    if (p)
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, p, n, NULL, NULL);
    return p;
}
#endif
static int source_directory(const char *path, int *directory) {
#ifdef _WIN32
    wchar_t *w = to_wide_path(path);
    if (!w) {
        message(MESSAGE_ERROR, "Error:", " cannot convert source path.");
        return 1;
    }
    DWORD attributes = GetFileAttributesW(w);
    DWORD native_error = GetLastError();
    free(w);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        message(MESSAGE_ERROR, "Error:", " cannot inspect '%s': Windows error %lu.", path,
                (unsigned long)native_error);
        return 1;
    }
    *directory =
        !!(attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat s;
    if (lstat(path, &s) < 0) {
        message(MESSAGE_ERROR, "Error:", " cannot inspect '%s': %s.", path, strerror(errno));
        return 1;
    }
    *directory = S_ISDIR(s.st_mode);
#endif
    return 0;
}

static char *source_name(const char *path, int directory) {
    char *copy = duplicate(path);
    if (!copy)
        return NULL;
    size_t n = strlen(copy);
    while (n > 1 && separator(copy[n - 1]))
        copy[--n] = 0;
    const char *base = basename_of(copy);
    char *name = NULL;
    if (directory && (!strcmp(base, ".") || !strcmp(base, ".."))) {
#ifdef _WIN32
        wchar_t *w = to_wide(copy);
        DWORD size = w ? GetFullPathNameW(w, 0, NULL, NULL) : 0;
        wchar_t *full = size ? malloc((size_t)size * sizeof *full) : NULL;
        if (full && GetFullPathNameW(w, size, full, NULL)) {
            char *utf8 = from_wide(full);
            if (utf8) {
                name = duplicate(basename_of(utf8));
                free(utf8);
            }
        }
        free(w);
        free(full);
#else
        char *full = realpath(copy, NULL);
        if (full) {
            name = duplicate(basename_of(full));
            free(full);
        }
#endif
    } else
        name = duplicate(base);
    free(copy);
    return name;
}

static int pack_sources(const char *output, const ArcOptions *options, int keep_root, int sources,
                        char **args) {
    for (int j = 0; j < sources; ++j) {
        size_t length = strlen(args[j]);
        while (length > 1 && separator(args[j][length - 1])) {
#ifdef _WIN32
            if (length == 3 && args[j][1] == ':')
                break;
#endif
            args[j][--length] = 0;
        }
    }
    int i = 0;
    int first_directory;
    if (source_directory(args[i], &first_directory))
        return 1;
    if (keep_root && (sources != 1 || !first_directory))
        return usage("pack", 1);
    ArcWriter *writer = NULL;
    ArcResult r = arc_create_ex(output, options, &writer);
    if (report(r))
        return 1;
    for (; i < sources; ++i) {
        int is_directory;
        if (source_directory(args[i], &is_directory)) {
            ArcResult cleanup = arc_discard(&writer);
            report(cleanup);
            return 1;
        }
        char *name = sources == 1 && is_directory && !keep_root
                         ? duplicate("")
                         : source_name(args[i], is_directory);
        if (!name) {
            r = (ArcResult){ARC_OUT_OF_MEMORY, "Cannot determine source name."};
        } else if (!*name && !(sources == 1 && is_directory && !keep_root)) {
            r = (ArcResult){ARC_INVALID_PATH, "Source root has no archive basename."};
        } else
            r = arc_add_path(writer, args[i], name);
        free(name);
        if (r.code != ARC_OK) {
            report(r);
            ArcResult cleanup = arc_discard(&writer);
            report(cleanup);
            return 1;
        }
    }
    r = arc_close(&writer);
    if (report(r)) {
        message(MESSAGE_WARNING, "Incomplete archive may remain:", " %s", output);
        return 1;
    }
    message(MESSAGE_SUCCESS, "Created", " %s", output);
    return 0;
}

static int pack(int count, char **args) {
    const char *format = NULL, *password = NULL;
    int keep_root = 0, i = 0;
    while (i < count && args[i][0] == '-') {
        if (!strcmp(args[i], "--")) {
            ++i;
            break;
        }
        if (!strcmp(args[i], "--help"))
            return usage("pack", 0);
        if (!strcmp(args[i], "--keep-root") && !keep_root) {
            keep_root = 1;
            ++i;
            continue;
        }
        if (!strcmp(args[i], "--password") && !password && i + 1 < count) {
            password = args[i + 1];
            i += 2;
            continue;
        }
        if (!strcmp(args[i], "--format") && !format && i + 1 < count) {
            format = args[i + 1];
            i += 2;
            continue;
        }
        return usage("pack", 1);
    }
    if (count - i < 1)
        return usage("pack", 1);
    const char *output = args[i++];
    int sources = count - i;
    const ArchiveSuffix *suffix = ar_suffix_of(output, 1);
    if (!format && !suffix) {
        message(MESSAGE_ERROR, "Error:", " cannot infer output format; specify --format.");
        return 2;
    }
    if (format && suffix && strcmp(format, suffix->format)) {
        message(MESSAGE_ERROR, "Error:", " --format conflicts with the output suffix.");
        return 2;
    }
    ArcOptions options = {format ? format : suffix->format, suffix ? suffix->compression : "none",
                          password};
    if (sources)
        return pack_sources(output, &options, keep_root, sources, args + i);

    const char *base = basename_of(output);
    char *source = duplicate(base);
    if (!source)
        return report((ArcResult){ARC_OUT_OF_MEMORY, "Cannot determine source directory."});
    if (suffix)
        source[strlen(source) - strlen(suffix->suffix)] = 0;
    if (!*source || !strcmp(source, ".") || !strcmp(source, "..")) {
        free(source);
        message(MESSAGE_ERROR,
                "Error:", " cannot derive a source directory; specify sources explicitly.");
        return 2;
    }
    int directory;
    if (source_directory(source, &directory)) {
        fprintf(stderr, "Specify sources explicitly.\n");
        free(source);
        return 1;
    }
    if (!directory) {
        message(MESSAGE_ERROR, "Error:", " '%s' is not a directory; specify sources explicitly.",
                source);
        free(source);
        return 1;
    }
    int result = pack_sources(output, &options, keep_root, 1, &source);
    free(source);
    return result;
}

static void escaped(const char *s) {
    display_text(stdout, s, 1);
}

static ArcResult print_entry(const ArcEntry *e, void *context) {
    (void)context;
    const char *names[] = {"file", "directory", "symlink", "hardlink", "other"};
    printf("%-9s ", names[e->type]);
    if (e->size_known)
        printf("%12" PRIu64 " ", e->size);
    else
        printf("%12s ", "?");
    escaped(e->path);
    if (e->link_target) {
        printf(" -> ");
        escaped(e->link_target);
    }
    putchar('\n');
    if (ferror(stdout))
        return (ArcResult){ARC_IO_ERROR, "Cannot write entry listing."};
    return (ArcResult){ARC_OK, {0}};
}

static int valid_destination_name(const char *name) {
    if (!*name || !strcmp(name, ".") || !strcmp(name, ".."))
        return 0;
#ifdef _WIN32
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p < 32 || strchr("<>:\"/\\|?*", *p))
            return 0;
    size_t n = strlen(name);
    if (name[n - 1] == '.' || name[n - 1] == ' ')
        return 0;
    char *stem = duplicate(name);
    if (!stem)
        return 0;
    char *dot = strchr(stem, '.');
    if (dot)
        *dot = 0;
    /* Windows reserves device names regardless of case or extension. */
    int reserved = !_stricmp(stem, "con") || !_stricmp(stem, "prn") || !_stricmp(stem, "aux") ||
                   !_stricmp(stem, "nul");
    if (strlen(stem) >= 4 && (!_strnicmp(stem, "com", 3) || !_strnicmp(stem, "lpt", 3))) {
        const unsigned char *p = (const unsigned char *)stem + 3;
        /* Windows also reserves COM/LPT followed by superscript ¹, ² or ³.
         * Their UTF-8 encodings are C2 B9, C2 B2 and C2 B3. */
        reserved |= (p[0] >= '1' && p[0] <= '9' && !p[1]) ||
                    (p[0] == 0xc2 && (p[1] == 0xb9 || p[1] == 0xb2 || p[1] == 0xb3) && !p[2]);
    }
    free(stem);
    if (reserved)
        return 0;
#endif
    return 1;
}

static char *destination_name(const char *archive) {
    const char *base = basename_of(archive);
    const ArchiveSuffix *suffix = ar_suffix_of(base, 0);
    char *name = duplicate(base);
    if (!name)
        return NULL;
    if (suffix)
        name[strlen(name) - strlen(suffix->suffix)] = 0;
    if (!suffix || !valid_destination_name(name)) {
        free(name);
        size_t n = strlen(base);
        if (n > SIZE_MAX - sizeof ".extracted")
            return NULL;
        name = malloc(n + sizeof ".extracted");
        if (name) {
            memcpy(name, base, n);
            memcpy(name + n, ".extracted", sizeof ".extracted");
        }
    }
    return name;
}

static int unpack(int count, char **args) {
    if (!count)
        return usage("unpack", 1);
    const char **selected = calloc((size_t)count, sizeof *selected);
    if (!selected) {
        message(MESSAGE_ERROR, "Error:", " cannot allocate selected paths.");
        return 1;
    }
    const char *password = NULL;
    size_t selected_count = 0;
    int i = 0;
    while (i < count) {
        if (!strcmp(args[i], "--help")) {
            free(selected);
            return usage("unpack", 0);
        }
        if (!strcmp(args[i], "--")) {
            ++i;
            break;
        }
        if (!strcmp(args[i], "--password") && !password && i + 1 < count) {
            password = args[i + 1];
            i += 2;
        } else if (!strcmp(args[i], "--entry")) {
            if (++i == count) {
                free(selected);
                return usage("unpack", 1);
            }
            selected[selected_count++] = args[i++];
        } else if (args[i][0] == '-') {
            free(selected);
            return usage("unpack", 1);
        } else
            break;
    }
    int operands = count - i;
    if (operands < 1 || operands > 2) {
        free(selected);
        return usage("unpack", 1);
    }
    const char *path = args[i];
    char *generated = operands == 1 ? destination_name(path) : NULL;
    if (operands == 1 && (!generated || !valid_destination_name(generated))) {
        free(generated);
        free(selected);
        message(MESSAGE_ERROR, "Error:", " cannot derive a destination; specify it explicitly.");
        return 1;
    }
    const char *destination = operands == 2 ? args[i + 1] : generated;
    ArcExtractOptions options = {operands == 1 ? ARC_DESTINATION_NEW : ARC_DESTINATION_MERGE,
                                 selected, selected_count, password};
    ArcResult r = arc_unpack_ex(path, destination, &options);
    if (!report(r))
        message(MESSAGE_SUCCESS, "Extracted to", " %s", destination);
    free(generated);
    free(selected);
    return r.code == ARC_OK ? 0 : 1;
}

static int application(int argc, char **argv) {
    setlocale(LC_ALL, "");
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        printf("arc %s\n", ARC_VERSION);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--help"))
        return usage(NULL, 0);
    if (argc < 2)
        return usage(NULL, 1);
    const char *command = argv[1];
    if (!strcmp(command, "pack"))
        return pack(argc - 2, argv + 2);
    if (!strcmp(command, "unpack"))
        return unpack(argc - 2, argv + 2);
    if (strcmp(command, "verify") && strcmp(command, "list") && strcmp(command, "info"))
        return usage(NULL, 1);
    int i = 2;
    ArcReadOptions options = {0};
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "--help"))
            return usage(command, 0);
        if (!strcmp(argv[i], "--")) {
            ++i;
            break;
        }
        if (!strcmp(argv[i], "--password") && !options.password && i + 1 < argc) {
            options.password = argv[i + 1];
            i += 2;
            continue;
        }
        return usage(command, 1);
    }
    int operands = argc - i;
    if (operands < 1 || operands > 1)
        return usage(command, 1);
    const char *path = argv[i];
    if (!strcmp(command, "list"))
        return report(arc_list_ex(path, &options, print_entry, NULL));
    if (!strcmp(command, "info")) {
        ArcInfo info;
        ArcResult r = arc_inspect_ex(path, &options, &info);
        if (report(r))
            return 1;
        printf("Archive bytes: %" PRIu64 "\nContent bytes: %" PRIu64 "\nFiles: %" PRIu64
               "\nDirectories: %" PRIu64 "\nSymbolic links: %" PRIu64 "\nHard links: %" PRIu64 "\n",
               info.archive_bytes, info.content_bytes, info.file_count, info.directory_count,
               info.symbolic_link_count, info.hard_link_count);
        return ferror(stdout) ? 1 : 0;
    }
    ArcResult r = arc_verify_ex(path, &options);
    if (!report(r))
        message(MESSAGE_SUCCESS, "Content readable", "; available integrity checks passed.");
    return r.code == ARC_OK ? 0 : 1;
}

static int finish(int status) {
    if (fflush(stdout) == EOF || ferror(stdout)) {
        fprintf(stderr, "Error: cannot write standard output.\n");
        if (!status)
            status = 1;
    }
    return status;
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    char **args = calloc((size_t)argc + 1, sizeof *args);
    if (!args) {
        message(MESSAGE_ERROR, "Error:", " cannot allocate arguments.");
        return 1;
    }
    int status = 0;
    for (int i = 0; i < argc; ++i) {
        args[i] = from_wide(argv[i]);
        if (!args[i]) {
            status = 1;
            break;
        }
    }
    if (!status)
        status = application(argc, args);
    else
        message(MESSAGE_ERROR, "Error:", " cannot convert Unicode arguments.");
    for (int i = 0; i < argc; ++i)
        free(args[i]);
    free(args);
    return finish(status);
}
#else
int main(int argc, char **argv) {
    return finish(application(argc, argv));
}
#endif
