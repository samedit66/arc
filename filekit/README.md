# filekit

Small C11 filesystem sublibrary for macOS, Linux and Windows (MSYS2 UCRT64).
It has its own public header and error values, builds to `libfilekit.a`, and
requires no libarchive, pkg-config, locale setup or third-party production dependency.

## Build and verify

From the arc repository:

```sh
mise run filekit:build
mise run filekit:verify
```

Or independently, with only this directory:

```sh
mise -C filekit run build
mise -C filekit run verify
```

`FILEKIT_BUILD_DIR` selects the independent output directory. Arc builds its
linked copy in `$ARC_BUILD_DIR/filekit`; independent filekit tasks use their own
temporary build directory. `CC` and `AR` select the toolchain.
`FILEKIT_SANITIZE=1` enables ASan/UBSan for an independent build; repository builds
also honor `ARC_SANITIZE=1`. Binaries and fixtures stay outside the source tree.
To use the library, include `filekit.h` and link `libfilekit.a` from a compatible
C runtime/toolchain. No arc header or object is required.

## Public interface

[filekit.h](filekit.h) is the authoritative contract. Every public function has a
Markdown description, argument list and fenced C example. Snippets state their
required descriptors or inspected objects; callers must check returned results. Each operation returns a self-contained
`FilekitResult` with a `FilekitCode` and a bounded diagnostic message. Callers use
the code to decide what to do. Outputs are unchanged on failure; missing paths
are a successful `filekit_info` result with `exists == 0`.

| Operations | Responsibility |
| --- | --- |
| `filekit_info`, `filekit_same_identity`, `filekit_compare_identity` | Inspect the final object without following its symbolic link; compare native identities |
| `filekit_absolute_path` | Bind a UTF-8 path to the current directory, without requiring existence |
| `filekit_create_file_exclusive`, `filekit_write_all`, `filekit_close` | Create without overwriting and write through an owned native C-runtime descriptor |
| `filekit_create_directory_exclusive`, `filekit_remove_file` | Exclusive directory creation and removal of files/file links |
| `filekit_restrict_permissions` | Remove POSIX permission bits; never broaden permissions |
| `filekit_set_mtime` | Set modification time without following the final symbolic link |
| `filekit_create_hard_link`, `filekit_create_symbolic_link` | Create native links without archive-specific target validation |

Example with cleanup and separate reporting of write/close failures:

```c
#include "filekit.h"
#include <stdio.h>

int main(void) {
    int fd;
    FilekitResult r = filekit_create_file_exclusive("hello.txt", 0644, &fd);
    if (r.code != FILEKIT_OK) {
        fprintf(stderr, "%s\n", r.message);
        return 1;
    }
    r = filekit_write_all(fd, "hello\n", 6);
    FilekitResult closed = filekit_close(fd, "hello.txt");
    if (r.code != FILEKIT_OK) fprintf(stderr, "%s\n", r.message);
    if (closed.code != FILEKIT_OK) fprintf(stderr, "%s\n", closed.message);
    return r.code != FILEKIT_OK || closed.code != FILEKIT_OK;
}
```

All path operands are nonempty UTF-8. Returned absolute strings belong to the
caller and are released with `free`. Descriptors belong to the caller until
`filekit_close`; a failed close must not be retried. A failed write can leave
partial output, and cleanup/rollback policy belongs to the client.

## Platform semantics

- POSIX permission modes obey `umask` at creation. Restricting permissions rejects
  a final symbolic link and only removes bits from ordinary files/directories.
- POSIX inspection, permission restriction and modification-time updates ignore
  trailing slashes, preserving `/`, so directory links are not dereferenced.
- Windows uses inherited ACLs and reports `FILEKIT_UNSUPPORTED` for POSIX permission
  restriction. `FilekitInfo.mode` is synthetic on Windows, not an ACL description.
- Windows paths use Unicode APIs and extended absolute paths, including UNC paths.
  Windows junctions and other non-symbolic reparse points are rejected by inspection.
- Symbolic-link target text is not confined to a root by filekit. Windows requires
  a known file/directory target kind and may require privileges. POSIX ignores kind.
- Identity values are meaningful on the same host and are not persistent IDs.
- POSIX absolute paths preserve dot components. Neither absolute-path anchoring
  nor preflight inspection prevents concurrent replacement of ancestor directories.
- Recursive traversal, general reading, copying, recursive deletion and directory
  removal are outside this API. Arc currently uses libarchive for traversal.

## Structure and arc integration

```text
filekit/
  filekit.h                  public filesystem values and operations
  file_system_unix.c         POSIX operations
  file_system_windows.c      Win32 / CRT operations
  windows_text.c             Windows Unicode and native path conversion
  support.c                  errors, strings and UTF-8 validation
  filekit_internal.h         private implementation/in-repository integration hooks
  tests.c                    standalone API contract checks
  mise.toml                  independent build and verification tasks
  build.sh                   C11 static-library build
  verify.py                  temporary API and build-failure checks
```

Filekit never includes arc headers. Writer, reader and extraction modules
call filekit directly. `ar_from_filekit` in `internal/support.c` maps filekit
errors to the unchanged archive error contract. `unpack.c` owns archive metadata
policy: restrict newly created POSIX directory permissions, then restore time
when present; Windows restoration only sets time. `ArchiveMetadata` stays private
to arc and is populated by its backend adapter.

Archive names, selectors, duplicate/prefix conflicts, extraction destination
rules, deferred link graphs, writer/reader state and cleanup precedence remain in
arc. Thread-local locale scopes remain there because libarchive needs them;
filekit never changes locale. Arc's private text/support modules reuse
filekit's private Unicode and UTF-8 hooks within this repository; those hooks are
not a compatibility promise for external filekit consumers.

The root build embeds filekit objects in libarc.a. Independent filekit checks
exercise real filesystem effects without linking libarchive; arc regression
checks exercise the CLI and public C API.

On Windows, failure to allocate a CRT descriptor after native file creation
closes the native handle and removes the newly created file. A removal failure
preserves the primary error and reports the incomplete output. Arc's
Windows API regression harness exercises these failure boundaries.
