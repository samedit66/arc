/*
 * Arc command-line application.
 *
 * Parses commands and options, selects default source and destination
 * paths, invokes archive operations, and presents results to the user.
 * Terminal output and progress handling live in adjacent CLI modules.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "arc.h"
#include "output.h"
#include "progress.h"
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

static const char ARC_PROGRAM_NAME[] = "arc";

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
    if (!command) {
        fprintf(out,
                "Usage: %s COMMAND [OPTIONS]\n\n"
                "Commands:\n"
                "  pack     Create an archive from files or a directory\n"
                "  unpack   Extract without overwriting files or links\n"
                "  list     Show archive entries; --tree shows a tree\n"
                "  info     Show sizes and entry counts\n"
                "  verify   Read content and run available integrity checks\n\n"
                "Examples:\n  %s pack project.zip ./project\n"
                "  %s unpack project.zip\n\n"
                "Use %s COMMAND --help for details.\n"
                "Usage: %s --help\nUsage: %s --version\n",
                ARC_PROGRAM_NAME, ARC_PROGRAM_NAME, ARC_PROGRAM_NAME, ARC_PROGRAM_NAME,
                ARC_PROGRAM_NAME, ARC_PROGRAM_NAME);
    } else if (!strcmp(command, "pack")) {
        fprintf(
            out,
            "Usage: %s pack [--format FORMAT] [--keep-root] [--password VALUE] ARCHIVE "
            "[SOURCE...]\n\n"
            "The output suffix selects the format. --format supplies an explicit format.\n"
            "A single directory contributes its contents; --keep-root retains its name.\n"
            "Multiple sources retain their basenames. Existing archives are never overwritten.\n"
            "Without SOURCE, use the directory named after ARCHIVE in the current directory.\n"
            "Example: %s pack docs.zip packs the contents of ./docs.\n"
            "--password writes ZIP AES-256; names remain visible.\n",
            ARC_PROGRAM_NAME, ARC_PROGRAM_NAME);
    } else if (!strcmp(command, "unpack")) {
        fprintf(out,
                "Usage: %s unpack [--strip-root] [--entry PATH]... [--password VALUE] ARCHIVE "
                "[DESTINATION]\n\n"
                "Without DESTINATION, create a new directory named after the archive in the "
                "current directory.\n"
                "An explicit destination may merge directories. Files and links are never "
                "overwritten.\n"
                "Stored paths are preserved. --strip-root removes one common root directory;\n"
                "mixed roots and root-level files are rejected before creating the destination.\n"
                "--entry selects an original archive path and its subtree, before root stripping.\n"
                "Extraction failures may leave partial output.\n"
                "Example: %s unpack --strip-root project.zip ./restored\n",
                ARC_PROGRAM_NAME, ARC_PROGRAM_NAME);
    } else {
        fprintf(out, "Usage: %s %s %s[--password VALUE] ARCHIVE\n\n", ARC_PROGRAM_NAME, command,
                !strcmp(command, "list") ? "[--tree] " : "");
        if (!strcmp(command, "list"))
            fputs("Show entries without extracting. --tree groups paths; repeated entries remain "
                  "visible.\n",
                  out);
        else if (!strcmp(command, "info"))
            fputs("Show archive size, content size and counts by entry type.\n", out);
        else
            fputs("Read every entry and run available integrity checks.\n"
                  "Verification does not establish safe extraction or detect every TAR data "
                  "change.\n",
                  out);
    }
    fputs("\nOptions precede operands. Use -- before paths beginning with '-'.\n", out);
    return error ? 2 : 0;
}

static int argument_error(const char *command, const char *detail, const char *value) {
    message(MESSAGE_ERROR, "Error:", " %s%s%s", detail, value ? ": " : "", value ? value : "");
    fprintf(stderr, "Use %s%s%s --help for usage.\n", ARC_PROGRAM_NAME, command ? " " : "",
            command ? command : "");
    return 2;
}

static int report_operation(ArcResult r, const char *action, const char *archive,
                            const char *destination) {
    if (arc_ok(r))
        return 0;
    message(MESSAGE_ERROR, action, " \"%s\"", archive);
    fputc('\n', stderr);
    const char *reason = NULL, *hint = NULL;
    switch (r.reason) {
    case ARC_REASON_DESTINATION_EXISTS:
        reason = "Destination already exists:";
        hint = destination ? "Choose another destination." : "Choose another archive path.";
        break;
    case ARC_REASON_ENTRY_EXISTS:
        reason = "An entry already exists at the destination:";
        hint = destination ? "Choose another destination." : "Choose another archive path.";
        break;
    case ARC_REASON_ARCHIVE_CONFLICT:
        reason = "Conflicting archive entries:";
        hint = destination ? "Use an archive with distinct, compatible entry paths."
                           : "Choose sources with distinct, compatible archive paths.";
        break;
    case ARC_REASON_UNSAFE_PATH:
        reason = "Unsafe archive path:";
        break;
    case ARC_REASON_STRIP_ROOT:
        hint = "Extract without --strip-root, or use an archive with one root directory.";
        break;
    default:
        break;
    }
    if (reason)
        fprintf(stderr, "%s\n", reason);
    else {
        display_text(stderr, r.message, 1);
        fputc('\n', stderr);
    }
    if (*r.path) {
        fputs("  ", stderr);
        display_text(stderr, r.path, 1);
        fputs(r.path_truncated ? " [path truncated]\n" : "\n", stderr);
    }
    if (r.reason == ARC_REASON_DESTINATION_EXISTS || r.reason == ARC_REASON_ENTRY_EXISTS)
        fprintf(stderr, "\n%s does not overwrite existing files or links.\n", ARC_PROGRAM_NAME);
    if (r.code == ARC_PASSWORD_REQUIRED)
        hint = "Supply a password with --password VALUE.";
    else if (r.code == ARC_PASSWORD_REJECTED)
        hint = "Check the password and try again.";
    if (hint)
        fprintf(stderr, "%s\n", hint);
    if (r.partial_output) {
        fputs(destination ? "\nSome files may have been extracted to:\n  "
                          : "\nAn incomplete archive may remain:\n  ",
              stderr);
        display_text(stderr, destination ? destination : archive, 1);
        fputc('\n', stderr);
    }
    if (*r.cleanup_message) {
        fputs("\nCleanup also failed:\n  ", stderr);
        display_text(stderr, r.cleanup_message, 1);
        fputc('\n', stderr);
    }
    return 1;
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
static ArcResult source_directory(const char *path, int *directory) {
    ArcFsInfo info = {0};
    ArcResult r = ar_from_fs(arc_fs_info(path, &info));
    if (arc_ok(r) && !info.exists)
        r = ar_result(ARC_NOT_FOUND, "Source directory not found: '%s'.", path);
    if (arc_ok(r))
        *directory = info.directory && !info.symbolic;
    return r;
}

static int pack_sources(const char *output, const ArcOptions *options, int keep_root, int sources,
                        char **args) {
    ArcPackOptions pack_options = {.configuration = options, .keep_root = keep_root};
    Progress *progress = progress_start("Creating", output);
    ArcResult r = arc_pack_ex(output, (const char *const *)args, (size_t)sources, &pack_options);
    progress_stop(progress);
    if (report_operation(r, "Cannot create", output, NULL)) {
        if (r.code == ARC_INVALID_ARGUMENT) {
            fprintf(stderr, "Use %s pack --help for usage.\n", ARC_PROGRAM_NAME);
            return 2;
        }
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
        return argument_error("pack",
                              (!strcmp(args[i], "--password") || !strcmp(args[i], "--format")) &&
                                      i + 1 == count
                                  ? "Missing option value"
                                  : "Unknown or repeated option",
                              args[i]);
    }
    if (count - i < 1)
        return argument_error("pack", "Missing archive path", NULL);
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
        return report((ArcResult){.code = ARC_OUT_OF_MEMORY,
                                  .message = "Cannot determine source directory."});
    if (suffix)
        source[strlen(source) - strlen(suffix->suffix)] = 0;
    if (!*source || !strcmp(source, ".") || !strcmp(source, "..")) {
        free(source);
        message(MESSAGE_ERROR,
                "Error:", " cannot derive a source directory; specify sources explicitly.");
        return 2;
    }
    int directory = 0;
    ArcResult inspection = source_directory(source, &directory);
    if (report_operation(inspection, "Cannot create", output, NULL)) {
        fprintf(stderr, "Specify sources explicitly.\n");
        free(source);
        return 1;
    }
    if (!directory) {
        report_operation(ar_result(ARC_INVALID_PATH,
                                   "'%s' is not a directory; specify sources explicitly.", source),
                         "Cannot create", output, NULL);
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
        return (ArcResult){.code = ARC_IO_ERROR, .message = "Cannot write entry listing."};
    return (ArcResult){.code = ARC_OK, .message = {0}};
}

typedef struct {
    char *path, *target;
    ArcEntryType type;
    int implicit;
} TreeRow;

typedef struct {
    TreeRow *rows;
    size_t count, capacity;
} Tree;

static ArcResult tree_row(Tree *tree, const char *path, const char *target, ArcEntryType type,
                          int implicit) {
    if (tree->count == tree->capacity) {
        size_t capacity = tree->capacity ? tree->capacity * 2 : 32;
        if (capacity < tree->capacity || capacity > SIZE_MAX / sizeof(TreeRow))
            return (ArcResult){.code = ARC_OVERFLOW, .message = "Too many tree entries."};
        TreeRow *rows = realloc(tree->rows, capacity * sizeof *rows);
        if (!rows)
            return (ArcResult){.code = ARC_OUT_OF_MEMORY, .message = "Cannot allocate tree."};
        tree->rows = rows;
        tree->capacity = capacity;
    }
    char *name = duplicate(path), *link = target ? duplicate(target) : NULL;
    if (!name || (target && !link)) {
        free(name);
        free(link);
        return (ArcResult){.code = ARC_OUT_OF_MEMORY, .message = "Cannot retain tree entry."};
    }
    tree->rows[tree->count++] = (TreeRow){name, link, type, implicit};
    return (ArcResult){.code = ARC_OK};
}

static ArcResult collect_tree(const ArcEntry *entry, void *context) {
    Tree *tree = context;
    char *name = duplicate(entry->path);
    if (!name)
        return (ArcResult){.code = ARC_OUT_OF_MEMORY, .message = "Cannot retain tree path."};
    /* Display literal components: listing does not reinterpret extraction safety. */
    size_t n = strlen(name);
    while (n > 1 && name[n - 1] == '/')
        name[--n] = 0;
    ArcResult r = tree_row(tree, name, entry->link_target, entry->type, 0);
    for (char *p = name; *p && arc_ok(r); ++p) {
        if (*p != '/' || p == name)
            continue;
        *p = 0;
        r = tree_row(tree, name, NULL, ARC_ENTRY_DIRECTORY, 1);
        *p = '/';
    }
    free(name);
    return r;
}

static int compare_tree_rows(const void *left, const void *right) {
    const TreeRow *a = left, *b = right;
    /* Sort components before their children (including names such as a.txt). */
    const unsigned char *x = (const unsigned char *)a->path;
    const unsigned char *y = (const unsigned char *)b->path;
    while (*x && *x == *y) {
        ++x;
        ++y;
    }
    int order = *x == *y ? 0 : !*x ? -1 : !*y ? 1 : *x == '/' ? -1 : *y == '/' ? 1 : *x - *y;
    return order ? order : a->implicit - b->implicit;
}

static ArcResult list_tree(const char *path, const ArcReadOptions *options) {
    Tree tree = {0};
    Progress *progress = progress_start("Reading", path);
    ArcResult r = arc_list_ex(path, options, collect_tree, &tree);
    progress_stop(progress);
    if (arc_ok(r) && tree.count) {
        qsort(tree.rows, tree.count, sizeof *tree.rows, compare_tree_rows);
        for (size_t i = 0; i < tree.count;) {
            size_t end = i + 1;
            while (end < tree.count && !strcmp(tree.rows[i].path, tree.rows[end].path))
                ++end;
            for (size_t j = i; j < end; ++j) {
                TreeRow *row = &tree.rows[j];
                if (row->implicit && j != i)
                    continue;
                const char *name = row->path;
                for (const char *p = row->path; *p; ++p)
                    if (*p == '/' && p != row->path) {
                        fputs("  ", stdout);
                        name = p + 1;
                    }
                fputs("|-- ", stdout);
                escaped(name);
                if (row->type == ARC_ENTRY_DIRECTORY)
                    putchar('/');
                if (row->target) {
                    fputs(row->type == ARC_ENTRY_HARD_LINK ? " => " : " -> ", stdout);
                    escaped(row->target);
                }
                if (end - i > 1 && !row->implicit && !tree.rows[i + 1].implicit)
                    fputs(" [repeated path]", stdout);
                putchar('\n');
            }
            i = end;
        }
        if (ferror(stdout))
            r = (ArcResult){.code = ARC_IO_ERROR, .message = "Cannot write tree listing."};
    }
    for (size_t i = 0; i < tree.count; ++i) {
        free(tree.rows[i].path);
        free(tree.rows[i].target);
    }
    free(tree.rows);
    return r;
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
        return argument_error("unpack", "Missing archive path", NULL);
    const char **selected = calloc((size_t)count, sizeof *selected);
    if (!selected) {
        message(MESSAGE_ERROR, "Error:", " cannot allocate selected paths.");
        return 1;
    }
    const char *password = NULL;
    int strip_root = 0;
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
        if (!strcmp(args[i], "--strip-root") && !strip_root) {
            strip_root = 1;
            ++i;
        } else if (!strcmp(args[i], "--password") && !password && i + 1 < count) {
            password = args[i + 1];
            i += 2;
        } else if (!strcmp(args[i], "--entry")) {
            if (++i == count) {
                free(selected);
                return argument_error("unpack", "Missing option value", "--entry");
            }
            selected[selected_count++] = args[i++];
        } else if (args[i][0] == '-') {
            free(selected);
            return argument_error("unpack",
                                  !strcmp(args[i], "--password") && i + 1 == count
                                      ? "Missing option value"
                                      : "Unknown or repeated option",
                                  args[i]);
        } else
            break;
    }
    int operands = count - i;
    if (operands < 1 || operands > 2) {
        free(selected);
        return argument_error("unpack", operands < 1 ? "Missing archive path" : "Too many operands",
                              NULL);
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
    ArcExtractOptions options = {.destination_mode =
                                     operands == 1 ? ARC_DESTINATION_NEW : ARC_DESTINATION_MERGE,
                                 .paths = selected,
                                 .path_count = selected_count,
                                 .password = password,
                                 .strip_root = strip_root};
    Progress *progress = progress_start("Extracting", path);
    ArcResult r = arc_unpack_ex(path, destination, &options);
    progress_stop(progress);
    if (!report_operation(r, "Cannot extract", path, destination))
        message(MESSAGE_SUCCESS, "Extracted to", " %s", destination);
    free(generated);
    free(selected);
    return r.code == ARC_OK ? 0 : 1;
}

static int application(int argc, char **argv) {
    setlocale(LC_ALL, "");
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        printf("%s %s\n", ARC_PROGRAM_NAME, ARC_VERSION);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--help"))
        return usage(NULL, 0);
    if (argc < 2)
        return argument_error(NULL, "Missing command", NULL);
    const char *command = argv[1];
    if (!strcmp(command, "pack"))
        return pack(argc - 2, argv + 2);
    if (!strcmp(command, "unpack"))
        return unpack(argc - 2, argv + 2);
    if (strcmp(command, "verify") && strcmp(command, "list") && strcmp(command, "info"))
        return argument_error(NULL, "Unknown command", command);
    int i = 2;
    int tree = 0;
    ArcReadOptions options = {0};
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "--help"))
            return usage(command, 0);
        if (!strcmp(argv[i], "--")) {
            ++i;
            break;
        }
        if (!strcmp(command, "list") && !strcmp(argv[i], "--tree") && !tree) {
            tree = 1;
            ++i;
            continue;
        }
        if (!strcmp(argv[i], "--password") && !options.password && i + 1 < argc) {
            options.password = argv[i + 1];
            i += 2;
            continue;
        }
        return argument_error(command,
                              !strcmp(argv[i], "--password") && i + 1 == argc
                                  ? "Missing option value"
                                  : "Unknown or repeated option",
                              argv[i]);
    }
    int operands = argc - i;
    if (operands < 1 || operands > 1)
        return argument_error(command, operands < 1 ? "Missing archive path" : "Too many operands",
                              NULL);
    const char *path = argv[i];
    if (!strcmp(command, "list"))
        return report_operation(tree ? list_tree(path, &options)
                                     : arc_list_ex(path, &options, print_entry, NULL),
                                "Cannot list", path, NULL);
    if (!strcmp(command, "info")) {
        ArcInfo info;
        Progress *progress = progress_start("Inspecting", path);
        ArcResult r = arc_inspect_ex(path, &options, &info);
        progress_stop(progress);
        if (report_operation(r, "Cannot inspect", path, NULL))
            return 1;
        printf("Archive bytes: %" PRIu64 "\nContent bytes: %" PRIu64 "\nFiles: %" PRIu64
               "\nDirectories: %" PRIu64 "\nSymbolic links: %" PRIu64 "\nHard links: %" PRIu64 "\n",
               info.archive_bytes, info.content_bytes, info.file_count, info.directory_count,
               info.symbolic_link_count, info.hard_link_count);
        return ferror(stdout) ? 1 : 0;
    }
    Progress *progress = progress_start("Verifying", path);
    ArcResult r = arc_verify_ex(path, &options);
    progress_stop(progress);
    if (!report_operation(r, "Cannot verify", path, NULL))
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
