# Using arc

This guide covers everyday CLI tasks and getting started with the C library.
For CLI installation, see the [installation guide](install.md).
For source builds and library integration, see [Building arc](build.md).

## Command line

Use the same command to unpack different archive formats:

```sh
arc unpack project.tar.gz
arc unpack project.zip
```

The archive format is detected from its contents.

By default, this creates a new directory named after the archive. You can also choose where to unpack it:

```sh
arc unpack project.tar.gz ./restored
```

The destination you specify can be an existing directory. Existing files and links are never overwritten. Paths inside the archive stay as they are unless you explicitly use `--strip-root`:

```sh
arc unpack --strip-root project.zip ./restored
```

This maps `project/src/main.c` to `./restored/src/main.c`. All archive entries
must share one root directory; mixed roots, root-level files and empty archives
are rejected before creating the destination. A lone empty root creates an empty
destination. Selection with `--entry` still uses original names such as
`project/src`. Hard-link targets are adjusted; relative symbolic-link text is
preserved without resolving the target, even when it points outside the stripped
tree. Hard links that escape it are rejected.
The extra metadata pass can require decompressing compressed input again.

### Creating archives

To create an archive:

```sh
arc pack ppa.tar ./ppa
arc pack ppa.tar.gz ./ppa
arc pack ppa.zip ./ppa
```

The output suffix determines the format and compression. If you pass a single directory, its contents are packed, including hidden files and empty directories. To keep the directory itself, use `--keep-root`:

```sh
arc pack --keep-root ppa.tar ./ppa
```

If you don't specify sources, `pack` uses a directory in the current working directory named after the archive, with the full recognized suffix removed:

```sh
arc pack docs.zip              # contents of ./docs
arc pack docs.tar.gz           # contents of ./docs
arc pack --keep-root docs.zip  # includes docs/ itself
arc pack ./out/docs.zip        # contents of ./docs, output in ./out
```

The inferred source must be a directory; otherwise, specify sources explicitly. With `--format` and no recognized suffix, the full archive filename is used as the directory name.

You can pack several sources or specify the format when the filename has no recognized suffix:

```sh
arc pack backup.tar ./docs ./notes.txt
arc pack --format tar backup ./docs
```

### Inspecting and selecting entries

```sh
arc list ppa.tar
arc list --tree ppa.tar
arc info ppa.tar
arc verify ppa.tar
arc unpack --entry docs --entry notes.txt ppa.tar ./selected
```

`list` shows the entries. `list --tree` groups paths, including implicit parent directories; repeated records remain visible. `info` shows sizes and entry counts. `verify` reads all entry data and runs the integrity checks available for the format. Plain TAR has no per-file data checksum, so verification cannot catch every change to its contents.

`--entry` selects a path and its subtree. It does not flatten paths or automatically include hard-link targets; those must be selected too.

Options go before paths. Use `--` before paths beginning with `-`. For the command syntax, run `arc --help` or `arc COMMAND --help`.

### Password-protected archives

Password-protected creation uses ZIP AES-256:

```sh
arc pack --password secret backup.zip ./docs
arc unpack --password secret backup.zip ./restored
arc verify --password secret backup.zip
```

`list` and `info` also accept `--password`. Encryption protects nonempty
ordinary-file contents. ZIP names, metadata and symbolic-link targets remain
visible; empty files and directories are not encrypted by libarchive.
Metadata listing does not validate the password. Reading supports ZIP AES and
ZipCrypto where available in the backend build. Password-protected creation
with another format fails before creating the output. The command-line password
can appear in shell history and process arguments.

## Use it from C

Include [arc.h](../arc.h). No libarchive headers or constants are needed in your code.

Most operations return an `ArcResult`: an error code and a short message. Check the code to decide what to do; the message is there to explain what went wrong.

The everyday API performs complete operations:

```c
ArcResult result = arc_pack("backup.zip", "./backup");
if (!arc_ok(result)) {
    fprintf(stderr, "%s\n", result.message);
    if (*result.cleanup_message)
        fprintf(stderr, "Cleanup: %s\n", result.cleanup_message);
    return 1;
}
result = arc_unpack("backup.zip", "./restored"); /* destination must be new */
```

A single source directory contributes its contents. A file or symbolic link
retains its basename. Sources are explicit; the library does not infer them
from the archive name. The whole-operation helper removes incomplete output on
failure where possible. `partial_output` and `cleanup_message` identify remaining
output and cleanup errors.

For multiple sources or a retained directory root:

```c
const char *sources[] = {"./src", "./README.md"};
ArcResult result = arc_pack_ex("project.zip", sources, 2, NULL);
```

`ArcPackOptions.keep_root` requires one actual directory.
`ArcPackOptions` directly accepts `format`, `compression`, `password` and
`observer`. With no explicit format, the output suffix chooses it and the default
outer compression. With an explicit format, omitted compression means `"none"`.
An explicit compression always overrides its default. Empty identifiers are errors.
For example, `ArcPackOptions options = {.password = "secret"};` is sufficient
when packing to `backup.zip`. `ArcOptions` remains the explicit configuration
for advanced writer creation and capability queries.

For selected extraction and root stripping:

```c
const char *paths[] = {"project/src"};
ArcUnpackOptions options = {
    .destination_mode = ARC_DESTINATION_NEW,
    .paths = paths,
    .path_count = 1,
    .strip_root = 1
};
ArcResult result = arc_unpack_ex("project.zip", "./restored", &options);
```

Use `ARC_DESTINATION_MERGE` to create or reuse an actual directory. Neither mode
permits file/link overwrites. `ArcResult.reason` and `path` provide diagnostic
context without parsing messages. `path_truncated` must be shown when displaying
an incomplete path. Extraction failures with `partial_output` may have created
files or directories; there is no automatic extraction rollback.

### Advanced writing and reading

When creating an archive, the format and outer compression are inferred from the output suffix. Unknown suffixes are rejected. To configure these explicitly, use `arc_create_ex(path, options, out)`, which ignores the suffix.

Here is how to create an archive with a text file inside:

```c
#include "arc.h"
#include <stdio.h>

int main(void) {
    ArcWriter *writer = NULL;
    ArcResult result = arc_create("hello.tar.gz", &writer);
    if (result.code != ARC_OK) {
        fprintf(stderr, "%s\n", result.message);
        return 1;
    }

    result = arc_add_text(writer, "hello.txt", "Hello!\n");
    if (result.code != ARC_OK) {
        fprintf(stderr, "%s\n", result.message);
        ArcResult cleanup = arc_discard(&writer);
        if (cleanup.code != ARC_OK)
            fprintf(stderr, "%s\n", cleanup.message);
        return 1;
    }

    result = arc_close(&writer);
    if (result.code != ARC_OK)
        fprintf(stderr, "%s\n", result.message);
    return result.code != ARC_OK;
}
```

Use `arc_add_file` for an ordinary file or `arc_add_path` for a directory tree or symbolic link. For binary data supplied in chunks, use `arc_begin_file`, `arc_write_chunk` and `arc_end_file`. The declared size must match the number of bytes written. Every available
creation format accepts this chunk API; the format matrix tests it with multiple
chunks. This does not promise incremental backend output: ISO9660 creation
stages file contents in temporary storage, and other backend formats can also
buffer data. `arc` does not currently provide unknown-size entries, pipe output
or custom I/O callbacks. Unsupported representations return `ARC_UNSUPPORTED`
instead of silently changing the format.

A writer belongs to the caller until `arc_close` or `arc_discard`. Both consume the pointer, even if they fail. After a content operation fails, the writer can only be closed or discarded. Close finalizes the archive; discard removes the unfinished output.

Reading does not require extraction. `arc_list` visits entry metadata, while the `arc_reader_*` functions let you read entry data in chunks. `arc_inspect` returns sizes and counts, and `arc_verify` reads the contents to check them.

To extract everything, use `arc_unpack`; to select paths, use `arc_unpack_ex`. Neither overwrites existing files or links. Unsafe paths and traversal through links are rejected. Symbolic-link targets are preserved without filesystem lookup; on POSIX they may be absolute, external, dangling or cyclic. Windows requires a file/directory kind from archive metadata or selected archive records; an unknown kind returns `ARC_UNSUPPORTED`. A failed extraction may leave partial output.

Paths use UTF-8. Support for formats, compression and links depends on the backend build and the host. Use `arc_get_capabilities` to check a requested configuration. Unsupported features return an error rather than silently choosing something else.

For password-protected ZIP creation, set `password` in `ArcOptions`. For
extraction, use `ArcUnpackOptions`. To verify encrypted
content without extracting it:

```c
ArcReadOptions options = {.password = "secret"};
ArcResult result = arc_verify_ex("backup.zip", &options);
```

A NULL password means no password; empty or invalid UTF-8 passwords are rejected.
Password errors can occur during content reading, even after a successful open.
See [arc.h](../arc.h) for the individual operation contracts and error codes.
The [CLI password section](#password-protected-archives) explains what ZIP
encryption protects and what remains visible.

The header contains full contracts and examples for every public function.
Examples that require an existing writer, reader or callback state that prerequisite. arc reports its own errors and errors returned by the backend through result values; fatal termination inside libarchive is outside that guarantee.

### Activity display

Long-running `pack`, `unpack`, `verify`, `info`, and `list --tree` operations
show phase, encountered entry count, processed content size and current path on
terminal stderr. The display appears after 200 ms, refreshes at most every 100 ms
when callbacks arrive, and disappears before results or errors. Blocking backend
calls can pause updates. Overall completion percentages are not estimated.
Plain `list` prints entries directly. Redirected stderr and `TERM=dumb` disable
progress automatically. Counters and phase labels use ASCII.

### Observe progress

Whole-operation options accept an optional synchronous observer:

```c
static void show_progress(const ArcProgress *progress, void *context) {
    (void)context;
    fprintf(stderr, "Phase %d: %llu records, %llu content bytes\n",
            (int)progress->phase, (unsigned long long)progress->entries_seen,
            (unsigned long long)progress->content_bytes);
}

/* Inside a caller function: */
const char *sources[] = {"./backup"};
ArcPackOptions options = {.observer = {show_progress, NULL}};
ArcResult result = arc_pack_ex("backup.tar.gz", sources, 1, &options);
```

`ArcUnpackOptions` and `ArcReadOptions` accept the same observer. `arc_unpack_ex`
also accepts NULL options for a new destination with default settings.

Callbacks run on the calling thread. Snapshots and entry paths are borrowed for
the callback only; copy them to retain or queue them to a UI thread. Do not reuse
an active handle, change cwd or modify input/output files from a callback.
A sequential reader copies its observer at opening; its context must remain alive
until close. Other operations borrow context only until they return.

The phases are preparing, processing and finalizing. `entries_seen` counts records
encountered in the current phase, including unselected records; it resets on a
phase change. `content_bytes` is cumulative and counts bytes accepted by the pack
backend, written by unpack, returned by reader reads, or read by verify. It excludes
skips and strip-root preflight; listing and inspection do not count declared sizes
as bytes read. Hard-link aliases add no duplicate content when packing/unpacking.
`entry_bytes` belongs to the current record; `entry_size` is usable only when
`entry_size_known` is nonzero. Paths use original archive names before root stripping,
or are NULL when unavailable or between entries.

There is no overall percentage or callback timing guarantee: backend calls can
block. Finalization may fail after all content was processed. Always check the
returned result. Observers do not cancel operations or return errors.
