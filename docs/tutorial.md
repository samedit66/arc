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

The destination you specify can be an existing directory. Existing files and links are never overwritten. Paths inside the archive stay as they are.

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
arc info ppa.tar
arc verify ppa.tar
arc unpack --entry docs --entry notes.txt ppa.tar ./selected
```

`list` shows the entries. `info` shows sizes and entry counts. `verify` reads all entry data and runs the integrity checks available for the format. Plain TAR has no per-file data checksum, so verification cannot catch every change to its contents.

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

Use `arc_add_file` for an ordinary file or `arc_add_path` for a directory tree or symbolic link. For binary data supplied in chunks, use `arc_begin_file`, `arc_write_chunk` and `arc_end_file`. The declared size must match the number of bytes written.

A writer belongs to the caller until `arc_close` or `arc_discard`. Both consume the pointer, even if they fail. After a content operation fails, the writer can only be closed or discarded. Close finalizes the archive; discard removes the unfinished output.

Reading does not require extraction. `arc_list` visits entry metadata, while the `arc_reader_*` functions let you read entry data in chunks. `arc_inspect` returns sizes and counts, and `arc_verify` reads the contents to check them.

To extract everything, use `arc_unpack`; to select paths, use `arc_unpack_ex`. Neither overwrites existing files or links. Unsafe paths and traversal through links are rejected. A failed extraction may leave partial output.

Paths use UTF-8. Support for formats, compression and links depends on the backend build and the host. Use `arc_get_capabilities` to check a requested configuration. Unsupported features return an error rather than silently choosing something else.

For password-protected ZIP creation, set `password` in `ArcOptions`. For
extraction, use `ArcUnpackOptions` or `ArcExtractOptions`. To verify encrypted
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
Examples that require an existing writer, reader or callback state that prerequisite. Arc reports its own errors and errors returned by the backend through result values; fatal termination inside libarchive is outside that guarantee.
